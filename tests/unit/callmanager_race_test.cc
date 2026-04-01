#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "callmanager.h"
#include "tests/support/callmanager_test_support.h"

namespace {

using namespace std::chrono_literals;

TEST(CallManagerRaceTest, AcceptedResponseAfterTimeoutIsIgnored) {
  CallManager manager(std::chrono::milliseconds(120));
  FakeCallSignalingTransport transport("alice");
  TestCallManagerObserver observer;
  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  ASSERT_TRUE(manager.InitiateCall("bob"));
  const std::string timed_out_call_id = manager.GetCurrentCallId();

  ASSERT_TRUE(observer.WaitFor(
      [](const TestCallManagerObserver& o) { return o.timeout_count == 1; },
      1500ms));
  EXPECT_EQ(manager.GetCallState(), CallState::Idle);

  manager.HandleCallResponse("bob", timed_out_call_id, true, "");
  EXPECT_EQ(manager.GetCallState(), CallState::Idle);
  EXPECT_TRUE(observer.create_peer_connection.empty());
}

TEST(CallManagerRaceTest, ConcurrentLocalAndRemoteEndConvergeToSingleClose) {
  CallManager manager;
  FakeCallSignalingTransport transport("alice");
  TestCallManagerObserver observer;
  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  ASSERT_TRUE(manager.InitiateCall("bob"));
  const std::string call_id = manager.GetCurrentCallId();
  manager.HandleCallResponse("bob", call_id, true, "");
  manager.NotifyPeerConnectionEstablished();
  ASSERT_EQ(manager.GetCallState(), CallState::Connected);

  std::thread local_end([&]() { manager.EndCall(); });
  std::thread remote_end(
      [&]() { manager.HandleCallEnd("bob", call_id, "remote-hangup"); });

  local_end.join();
  remote_end.join();

  EXPECT_EQ(manager.GetCallState(), CallState::Idle);
  EXPECT_EQ(observer.need_close_peer_connection_count, 1);
}

}  // namespace
