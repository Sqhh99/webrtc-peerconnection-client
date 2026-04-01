#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "callmanager.h"
#include "tests/support/callmanager_test_support.h"

namespace {

using namespace std::chrono_literals;

TEST(CallManagerTest, InitiateCallTransitionsToCallingAndSendsRequest) {
  CallManager manager;
  FakeCallSignalingTransport transport("alice");
  TestCallManagerObserver observer;

  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  ASSERT_TRUE(manager.InitiateCall("bob"));
  EXPECT_EQ(manager.GetCallState(), CallState::Calling);
  EXPECT_EQ(manager.GetCurrentPeer(), "bob");
  EXPECT_FALSE(manager.GetCurrentCallId().empty());

  const auto sent = transport.SentSignals();
  ASSERT_EQ(sent.size(), 1u);
  EXPECT_EQ(sent[0].type, SignalingMessageType::CallRequest);
  EXPECT_EQ(sent[0].from, "alice");
  EXPECT_EQ(sent[0].to, "bob");
  EXPECT_EQ(sent[0].call_id, manager.GetCurrentCallId());
}

TEST(CallManagerTest, IncomingCallWhenIdleTransitionsToReceiving) {
  CallManager manager;
  FakeCallSignalingTransport transport("bob");
  TestCallManagerObserver observer;
  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  manager.HandleCallRequest("alice", "call-1");

  EXPECT_EQ(manager.GetCallState(), CallState::Receiving);
  EXPECT_EQ(manager.GetCurrentPeer(), "alice");
  EXPECT_EQ(manager.GetCurrentCallId(), "call-1");

  ASSERT_EQ(observer.incoming_callers.size(), 1u);
  EXPECT_EQ(observer.incoming_callers[0], "alice");
}

TEST(CallManagerTest, IncomingCallWhenBusyReturnsBusyResponse) {
  CallManager manager;
  FakeCallSignalingTransport transport("bob");
  TestCallManagerObserver observer;
  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  ASSERT_TRUE(manager.InitiateCall("charlie"));
  manager.HandleCallRequest("alice", "call-2");

  EXPECT_EQ(manager.GetCallState(), CallState::Calling);

  const auto sent = transport.SentSignals();
  ASSERT_GE(sent.size(), 2u);
  const OutgoingCallSignal& last = sent.back();
  EXPECT_EQ(last.type, SignalingMessageType::CallResponse);
  EXPECT_EQ(last.to, "alice");
  EXPECT_EQ(last.call_id, "call-2");
  EXPECT_FALSE(last.accepted);
  EXPECT_EQ(last.reason, "busy");
}

TEST(CallManagerTest, AcceptCallTransitionsToConnectingAndRequestsPeerConnection) {
  CallManager manager;
  FakeCallSignalingTransport transport("bob");
  TestCallManagerObserver observer;
  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  manager.HandleCallRequest("alice", "call-3");
  manager.AcceptCall();

  EXPECT_EQ(manager.GetCallState(), CallState::Connecting);

  const auto sent = transport.SentSignals();
  ASSERT_EQ(sent.size(), 1u);
  EXPECT_EQ(sent[0].type, SignalingMessageType::CallResponse);
  EXPECT_EQ(sent[0].to, "alice");
  EXPECT_TRUE(sent[0].accepted);
  EXPECT_EQ(sent[0].call_id, "call-3");

  ASSERT_EQ(observer.create_peer_connection.size(), 1u);
  EXPECT_EQ(observer.create_peer_connection[0].first, "alice");
  EXPECT_FALSE(observer.create_peer_connection[0].second);
}

TEST(CallManagerTest, EndCallSendsCallEndAndResetsState) {
  CallManager manager;
  FakeCallSignalingTransport transport("alice");
  TestCallManagerObserver observer;
  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  ASSERT_TRUE(manager.InitiateCall("bob"));
  manager.HandleCallResponse("bob", manager.GetCurrentCallId(), true, "");
  manager.NotifyPeerConnectionEstablished();
  ASSERT_EQ(manager.GetCallState(), CallState::Connected);

  manager.EndCall();

  EXPECT_EQ(manager.GetCallState(), CallState::Idle);
  EXPECT_TRUE(manager.GetCurrentPeer().empty());
  EXPECT_TRUE(manager.GetCurrentCallId().empty());

  const auto sent = transport.SentSignals();
  ASSERT_GE(sent.size(), 2u);
  const OutgoingCallSignal& last = sent.back();
  EXPECT_EQ(last.type, SignalingMessageType::CallEnd);
  EXPECT_EQ(last.to, "bob");
  EXPECT_EQ(last.reason, "hangup");

  EXPECT_EQ(observer.need_close_peer_connection_count, 1);
  ASSERT_EQ(observer.ended.size(), 1u);
  EXPECT_EQ(observer.ended[0].first, "bob");
  EXPECT_EQ(observer.ended[0].second, "hangup");
}

TEST(CallManagerTest, CallRequestTimeoutTriggersObserverAndCleanup) {
  CallManager manager(std::chrono::milliseconds(150));
  FakeCallSignalingTransport transport("alice");
  TestCallManagerObserver observer;
  manager.SetSignalTransport(&transport);
  manager.RegisterObserver(&observer);

  ASSERT_TRUE(manager.InitiateCall("bob"));

  ASSERT_TRUE(observer.WaitFor(
      [](const TestCallManagerObserver& o) { return o.timeout_count == 1; },
      1500ms));
  ASSERT_TRUE(observer.WaitFor(
      [](const TestCallManagerObserver& o) {
        for (const auto& state_change : o.state_changes) {
          if (state_change.first == CallState::Idle) {
            return true;
          }
        }
        return false;
      },
      1000ms));
  EXPECT_EQ(manager.GetCallState(), CallState::Idle);
  EXPECT_TRUE(manager.GetCurrentPeer().empty());
  EXPECT_TRUE(manager.GetCurrentCallId().empty());
}

}  // namespace
