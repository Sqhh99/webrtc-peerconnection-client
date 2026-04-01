#include <mutex>
#include <string>
#include <unordered_map>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "callmanager.h"
#include "signaling_codec.h"
#include "tests/support/callmanager_test_support.h"

namespace {

using json = nlohmann::json;

class InMemoryCallSignalingBus {
 public:
  void AttachPeer(const std::string& uid,
                  FakeCallSignalingTransport* transport,
                  CallManager* manager) {
    std::lock_guard<std::mutex> lock(mutex_);
    managers_[uid] = manager;
    transport->SetSendHook([this](const OutgoingCallSignal& signal) {
      Deliver(signal.raw_message);
    });
  }

 private:
  void Deliver(const std::string& raw_message) {
    std::string target_uid;
    try {
      const json envelope = json::parse(raw_message);
      target_uid = envelope.value("to", "");
    } catch (...) {
      return;
    }
    if (target_uid.empty()) {
      return;
    }

    CallManager* target_manager = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = managers_.find(target_uid);
      if (it == managers_.end()) {
        return;
      }
      target_manager = it->second;
    }

    ParsedSignalingMessage parsed;
    std::string error;
    if (!ParseSignalingMessage(raw_message, &parsed, &error)) {
      return;
    }

    switch (parsed.type) {
      case SignalingMessageType::CallRequest:
        target_manager->HandleCallRequest(parsed.from, parsed.call_id);
        break;
      case SignalingMessageType::CallResponse:
        target_manager->HandleCallResponse(parsed.from, parsed.call_id,
                                           parsed.accepted, parsed.reason);
        break;
      case SignalingMessageType::CallCancel:
        target_manager->HandleCallCancel(parsed.from, parsed.call_id,
                                         parsed.reason);
        break;
      case SignalingMessageType::CallEnd:
        target_manager->HandleCallEnd(parsed.from, parsed.call_id, parsed.reason);
        break;
      default:
        break;
    }
  }

  std::mutex mutex_;
  std::unordered_map<std::string, CallManager*> managers_;
};

TEST(CallFlowIntegrationTest, EndToEndCallLifecycleOverCodecRoundTrip) {
  CallManager alice_manager;
  CallManager bob_manager;
  FakeCallSignalingTransport alice_transport("alice");
  FakeCallSignalingTransport bob_transport("bob");
  TestCallManagerObserver alice_observer;
  TestCallManagerObserver bob_observer;
  InMemoryCallSignalingBus bus;

  alice_manager.SetSignalTransport(&alice_transport);
  bob_manager.SetSignalTransport(&bob_transport);
  alice_manager.RegisterObserver(&alice_observer);
  bob_manager.RegisterObserver(&bob_observer);
  bus.AttachPeer("alice", &alice_transport, &alice_manager);
  bus.AttachPeer("bob", &bob_transport, &bob_manager);

  ASSERT_TRUE(alice_manager.InitiateCall("bob"));
  EXPECT_EQ(alice_manager.GetCallState(), CallState::Calling);
  EXPECT_EQ(bob_manager.GetCallState(), CallState::Receiving);
  ASSERT_EQ(bob_observer.incoming_callers.size(), 1u);
  EXPECT_EQ(bob_observer.incoming_callers[0], "alice");

  bob_manager.AcceptCall();
  EXPECT_EQ(bob_manager.GetCallState(), CallState::Connecting);
  EXPECT_EQ(alice_manager.GetCallState(), CallState::Connecting);

  alice_manager.NotifyPeerConnectionEstablished();
  bob_manager.NotifyPeerConnectionEstablished();
  EXPECT_EQ(alice_manager.GetCallState(), CallState::Connected);
  EXPECT_EQ(bob_manager.GetCallState(), CallState::Connected);

  alice_manager.EndCall();
  EXPECT_EQ(alice_manager.GetCallState(), CallState::Idle);
  EXPECT_EQ(bob_manager.GetCallState(), CallState::Idle);
  EXPECT_EQ(bob_observer.need_close_peer_connection_count, 1);
  ASSERT_EQ(bob_observer.ended.size(), 1u);
  EXPECT_EQ(bob_observer.ended[0].first, "alice");
}

}  // namespace
