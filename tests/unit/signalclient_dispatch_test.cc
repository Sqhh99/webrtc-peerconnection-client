#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "signal_message_dispatcher.h"

namespace {

class RecordingSignalClientObserver : public SignalClientObserver {
 public:
  void OnConnected(const std::string& client_id) override {
    connected_ids.push_back(client_id);
  }
  void OnDisconnected() override { ++disconnected_count; }
  void OnConnectionError(const std::string& error) override {
    connection_errors.push_back(error);
  }
  void OnIceServersReceived(
      const std::vector<IceServerConfig>& servers) override {
    ice_servers_batches.push_back(servers);
  }
  void OnClientListUpdate(const std::vector<ClientInfo>& clients) override {
    client_list_batches.push_back(clients);
  }
  void OnUserOffline(const std::string& client_id) override {
    offline_ids.push_back(client_id);
  }
  void OnCallRequest(const std::string& from,
                     const std::string& call_id) override {
    call_requests.emplace_back(from, call_id);
  }
  void OnCallResponse(const std::string& from,
                      const std::string& call_id,
                      bool accepted,
                      const std::string& reason) override {
    call_responses.push_back({from, call_id, accepted, reason});
  }
  void OnCallCancel(const std::string& from,
                    const std::string& call_id,
                    const std::string& reason) override {
    call_cancels.push_back({from, call_id, reason});
  }
  void OnCallEnd(const std::string& from,
                 const std::string& call_id,
                 const std::string& reason) override {
    call_ends.push_back({from, call_id, reason});
  }
  void OnOffer(const std::string& from,
               const SessionDescriptionPayload& sdp) override {
    offers.push_back({from, sdp.call_id, sdp.type, sdp.sdp});
  }
  void OnAnswer(const std::string& from,
                const SessionDescriptionPayload& sdp) override {
    answers.push_back({from, sdp.call_id, sdp.type, sdp.sdp});
  }
  void OnIceCandidate(const std::string& from,
                      const IceCandidatePayload& candidate) override {
    candidates.push_back(
        {from, candidate.call_id, candidate.sdp_mid, candidate.candidate});
  }

  struct CallResponseRecord {
    std::string from;
    std::string call_id;
    bool accepted;
    std::string reason;
  };
  struct CallReasonRecord {
    std::string from;
    std::string call_id;
    std::string reason;
  };
  struct SessionRecord {
    std::string from;
    std::string call_id;
    std::string type;
    std::string sdp;
  };
  struct CandidateRecord {
    std::string from;
    std::string call_id;
    std::string sdp_mid;
    std::string candidate;
  };

  std::vector<std::string> connected_ids;
  int disconnected_count = 0;
  std::vector<std::string> connection_errors;
  std::vector<std::vector<IceServerConfig>> ice_servers_batches;
  std::vector<std::vector<ClientInfo>> client_list_batches;
  std::vector<std::string> offline_ids;
  std::vector<std::pair<std::string, std::string>> call_requests;
  std::vector<CallResponseRecord> call_responses;
  std::vector<CallReasonRecord> call_cancels;
  std::vector<CallReasonRecord> call_ends;
  std::vector<SessionRecord> offers;
  std::vector<SessionRecord> answers;
  std::vector<CandidateRecord> candidates;
};

TEST(SignalMessageDispatcherTest, RegisteredDispatchesIceServersAndRequestsList) {
  RecordingSignalClientObserver observer;
  const std::string message = R"({
    "type":"registered",
    "from":"server",
    "payload":{"iceServers":[{"urls":["stun:stun.l.google.com:19302"]}]}
  })";

  const SignalMessageDispatchOutcome outcome =
      DispatchSignalingMessage(message, &observer);

  EXPECT_TRUE(outcome.success);
  EXPECT_TRUE(outcome.has_ice_servers);
  EXPECT_TRUE(outcome.request_client_list);
  ASSERT_EQ(observer.ice_servers_batches.size(), 1u);
  ASSERT_EQ(observer.ice_servers_batches[0].size(), 1u);
  ASSERT_EQ(observer.ice_servers_batches[0][0].urls.size(), 1u);
  EXPECT_EQ(observer.ice_servers_batches[0][0].urls[0],
            "stun:stun.l.google.com:19302");
}

TEST(SignalMessageDispatcherTest, InvalidJsonReturnsErrorWithoutCallbacks) {
  RecordingSignalClientObserver observer;
  const SignalMessageDispatchOutcome outcome =
      DispatchSignalingMessage("{bad-json", &observer);

  EXPECT_FALSE(outcome.success);
  EXPECT_FALSE(outcome.error.empty());
  EXPECT_TRUE(observer.ice_servers_batches.empty());
  EXPECT_TRUE(observer.call_requests.empty());
  EXPECT_TRUE(observer.offline_ids.empty());
}

TEST(SignalMessageDispatcherTest, UnknownTypeIsIgnoredSafely) {
  RecordingSignalClientObserver observer;
  const SignalMessageDispatchOutcome outcome =
      DispatchSignalingMessage(R"({"type":"unknown-message","payload":{}})",
                               &observer);

  EXPECT_TRUE(outcome.success);
  EXPECT_FALSE(outcome.has_ice_servers);
  EXPECT_FALSE(outcome.request_client_list);
  EXPECT_TRUE(observer.call_requests.empty());
  EXPECT_TRUE(observer.offline_ids.empty());
}

TEST(SignalMessageDispatcherTest, MixedValidMessagesDispatchExpectedCallbacks) {
  RecordingSignalClientObserver observer;

  const SignalMessageDispatchOutcome request_outcome = DispatchSignalingMessage(
      R"({"type":"call-request","from":"alice","payload":{"callId":"c-1"}})",
      &observer);
  const SignalMessageDispatchOutcome response_outcome = DispatchSignalingMessage(
      R"({"type":"call-response","from":"bob","payload":{"callId":"c-1","accepted":false,"reason":"busy"}})",
      &observer);
  const SignalMessageDispatchOutcome offline_outcome = DispatchSignalingMessage(
      R"({"type":"user-offline","payload":{"clientId":"charlie"}})", &observer);

  EXPECT_TRUE(request_outcome.success);
  EXPECT_TRUE(response_outcome.success);
  EXPECT_TRUE(offline_outcome.success);
  ASSERT_EQ(observer.call_requests.size(), 1u);
  EXPECT_EQ(observer.call_requests[0].first, "alice");
  EXPECT_EQ(observer.call_requests[0].second, "c-1");
  ASSERT_EQ(observer.call_responses.size(), 1u);
  EXPECT_EQ(observer.call_responses[0].from, "bob");
  EXPECT_FALSE(observer.call_responses[0].accepted);
  EXPECT_EQ(observer.call_responses[0].reason, "busy");
  ASSERT_EQ(observer.offline_ids.size(), 1u);
  EXPECT_EQ(observer.offline_ids[0], "charlie");
}

}  // namespace
