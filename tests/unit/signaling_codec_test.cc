#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "signaling_codec.h"

namespace {

using json = nlohmann::json;

TEST(SignalingCodecTest, ParseNonObjectEnvelopeResetsOutput) {
  ParsedSignalingMessage parsed;
  parsed.from = "stale-from";
  parsed.call_id = "stale-call";
  parsed.reason = "stale-reason";

  std::string error;
  ASSERT_TRUE(ParseSignalingMessage("[]", &parsed, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(parsed.type, SignalingMessageType::Unknown);
  EXPECT_TRUE(parsed.from.empty());
  EXPECT_TRUE(parsed.call_id.empty());
  EXPECT_TRUE(parsed.reason.empty());
}

TEST(SignalingCodecTest, ParseOfferSupportsNestedSdpObject) {
  const std::string message = R"({
    "type":"offer",
    "from":"alice",
    "payload":{"callId":"c1","sdp":{"type":"offer","sdp":"v=0"}}
  })";

  ParsedSignalingMessage parsed;
  std::string error;
  ASSERT_TRUE(ParseSignalingMessage(message, &parsed, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(parsed.type, SignalingMessageType::Offer);
  EXPECT_EQ(parsed.from, "alice");
  EXPECT_EQ(parsed.session_description.call_id, "c1");
  EXPECT_EQ(parsed.session_description.type, "offer");
  EXPECT_EQ(parsed.session_description.sdp, "v=0");
}

TEST(SignalingCodecTest, ParseIceCandidateSupportsNestedCandidateObject) {
  const std::string message = R"({
    "type":"ice-candidate",
    "from":"alice",
    "payload":{
      "callId":"c2",
      "candidate":{"sdpMid":"0","sdpMLineIndex":1,"candidate":"cand"}
    }
  })";

  ParsedSignalingMessage parsed;
  std::string error;
  ASSERT_TRUE(ParseSignalingMessage(message, &parsed, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(parsed.type, SignalingMessageType::IceCandidate);
  EXPECT_EQ(parsed.ice_candidate.call_id, "c2");
  EXPECT_EQ(parsed.ice_candidate.sdp_mid, "0");
  EXPECT_EQ(parsed.ice_candidate.sdp_mline_index, 1);
  EXPECT_EQ(parsed.ice_candidate.candidate, "cand");
}

TEST(SignalingCodecTest, BuildCallResponseRoundTrip) {
  const std::string encoded =
      BuildCallResponseMessage("alice", "bob", "call-42", false, "busy");

  const json envelope = json::parse(encoded);
  EXPECT_EQ(envelope.value("type", ""), "call-response");
  EXPECT_EQ(envelope.value("from", ""), "alice");
  EXPECT_EQ(envelope.value("to", ""), "bob");

  ParsedSignalingMessage parsed;
  std::string error;
  ASSERT_TRUE(ParseSignalingMessage(encoded, &parsed, &error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(parsed.type, SignalingMessageType::CallResponse);
  EXPECT_EQ(parsed.from, "alice");
  EXPECT_EQ(parsed.call_id, "call-42");
  EXPECT_FALSE(parsed.accepted);
  EXPECT_EQ(parsed.reason, "busy");
}

}  // namespace
