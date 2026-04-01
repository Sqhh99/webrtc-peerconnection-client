#include "signaling_codec.h"

#include <chrono>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

SessionDescriptionPayload ParseSessionDescription(const json& payload,
                                                  const std::string& fallback_type) {
  SessionDescriptionPayload result;
  result.call_id = payload.value("callId", payload.value("call_id", ""));
  result.type = fallback_type;

  if (payload.contains("sdp")) {
    const json& sdp_value = payload.at("sdp");
    if (sdp_value.is_object()) {
      result.type = sdp_value.value("type", fallback_type);
      result.sdp = sdp_value.value("sdp", "");
      return result;
    }
    if (sdp_value.is_string()) {
      result.type = payload.value("type", fallback_type);
      result.sdp = sdp_value.get<std::string>();
      return result;
    }
  }

  result.type = payload.value("type", fallback_type);
  result.sdp = payload.value("sdp", "");
  return result;
}

IceCandidatePayload ParseIceCandidate(const json& payload) {
  IceCandidatePayload result;
  result.call_id = payload.value("callId", payload.value("call_id", ""));

  const json* candidate_payload = &payload;
  if (payload.contains("candidate") && payload.at("candidate").is_object()) {
    candidate_payload = &payload.at("candidate");
  }

  result.sdp_mid = candidate_payload->value("sdpMid", "");
  if (result.sdp_mid.empty()) {
    result.sdp_mid = candidate_payload->value("sdp_mid", "");
  }

  if (candidate_payload->contains("sdpMLineIndex")) {
    result.sdp_mline_index = candidate_payload->at("sdpMLineIndex").get<int>();
  } else if (candidate_payload->contains("sdpMlineIndex")) {
    result.sdp_mline_index = candidate_payload->at("sdpMlineIndex").get<int>();
  } else if (candidate_payload->contains("sdp_mline_index")) {
    result.sdp_mline_index = candidate_payload->at("sdp_mline_index").get<int>();
  }

  result.candidate = candidate_payload->value("candidate", "");
  return result;
}

std::vector<IceServerConfig> ParseIceServers(const json& payload) {
  std::vector<IceServerConfig> ice_servers;
  if (!payload.contains("iceServers") || !payload.at("iceServers").is_array()) {
    return ice_servers;
  }

  for (const auto& server_value : payload.at("iceServers")) {
    if (!server_value.is_object()) {
      continue;
    }

    IceServerConfig config;
    if (server_value.contains("urls")) {
      const auto& urls = server_value.at("urls");
      if (urls.is_array()) {
        for (const auto& url_value : urls) {
          if (url_value.is_string()) {
            config.urls.push_back(url_value.get<std::string>());
          }
        }
      } else if (urls.is_string()) {
        config.urls.push_back(urls.get<std::string>());
      }
    }

    config.username = server_value.value("username", "");
    config.credential = server_value.value("credential", "");
    ice_servers.push_back(std::move(config));
  }

  return ice_servers;
}

std::vector<ClientInfo> ParseClientList(const json& payload) {
  std::vector<ClientInfo> clients;
  if (!payload.contains("clients") || !payload.at("clients").is_array()) {
    return clients;
  }

  for (const auto& client_value : payload.at("clients")) {
    ClientInfo client;
    if (client_value.is_object()) {
      client.id = client_value.value("id", "");
    } else if (client_value.is_string()) {
      client.id = client_value.get<std::string>();
    }
    if (!client.id.empty()) {
      clients.push_back(std::move(client));
    }
  }
  return clients;
}

SignalingMessageType ParseMessageType(const std::string& type_str) {
  if (type_str == "registered") return SignalingMessageType::Registered;
  if (type_str == "client-list") return SignalingMessageType::ClientList;
  if (type_str == "user-offline") return SignalingMessageType::UserOffline;
  if (type_str == "call-request") return SignalingMessageType::CallRequest;
  if (type_str == "call-response") return SignalingMessageType::CallResponse;
  if (type_str == "call-cancel") return SignalingMessageType::CallCancel;
  if (type_str == "call-end") return SignalingMessageType::CallEnd;
  if (type_str == "offer") return SignalingMessageType::Offer;
  if (type_str == "answer") return SignalingMessageType::Answer;
  if (type_str == "ice-candidate") return SignalingMessageType::IceCandidate;
  return SignalingMessageType::Unknown;
}

json BuildMessageEnvelope(const std::string& type,
                          const std::string& from,
                          const std::string& to,
                          json payload) {
  json message = {{"type", type}, {"from", from}};
  if (!to.empty()) {
    message["to"] = to;
  }
  if (!payload.is_null()) {
    message["payload"] = std::move(payload);
  }
  return message;
}

}  // namespace

bool ParseSignalingMessage(const std::string& message,
                           ParsedSignalingMessage* parsed,
                           std::string* error_message) {
  if (!parsed) {
    if (error_message) {
      *error_message = "ParsedSignalingMessage output parameter is null.";
    }
    return false;
  }

  json envelope;
  try {
    envelope = json::parse(message);
  } catch (const std::exception& ex) {
    if (error_message) {
      *error_message = std::string("Invalid JSON from signaling server: ") +
                       ex.what();
    }
    return false;
  }

  if (!envelope.is_object()) {
    *parsed = ParsedSignalingMessage{};
    parsed->type = SignalingMessageType::Unknown;
    if (error_message) {
      error_message->clear();
    }
    return true;
  }

  const json payload =
      envelope.contains("payload") && envelope.at("payload").is_object()
          ? envelope.at("payload")
          : json::object();

  ParsedSignalingMessage result;
  result.type = ParseMessageType(envelope.value("type", ""));
  result.from = envelope.value("from", "");

  switch (result.type) {
    case SignalingMessageType::Registered:
      result.ice_servers = ParseIceServers(payload);
      break;
    case SignalingMessageType::ClientList:
      result.clients = ParseClientList(payload);
      break;
    case SignalingMessageType::UserOffline:
      result.offline_client_id = payload.value("clientId", "");
      break;
    case SignalingMessageType::CallRequest:
      result.call_id = payload.value("callId", payload.value("call_id", ""));
      break;
    case SignalingMessageType::CallResponse:
      result.call_id = payload.value("callId", payload.value("call_id", ""));
      result.accepted = payload.value("accepted", false);
      result.reason = payload.value("reason", "");
      break;
    case SignalingMessageType::CallCancel:
    case SignalingMessageType::CallEnd:
      result.call_id = payload.value("callId", payload.value("call_id", ""));
      result.reason = payload.value("reason", "");
      break;
    case SignalingMessageType::Offer:
      result.session_description = ParseSessionDescription(payload, "offer");
      break;
    case SignalingMessageType::Answer:
      result.session_description = ParseSessionDescription(payload, "answer");
      break;
    case SignalingMessageType::IceCandidate:
      result.ice_candidate = ParseIceCandidate(payload);
      break;
    case SignalingMessageType::Unknown:
      break;
  }

  *parsed = std::move(result);
  if (error_message) {
    error_message->clear();
  }
  return true;
}

std::string BuildRegisterMessage(const std::string& from) {
  return BuildMessageEnvelope("register", from, "", nullptr).dump();
}

std::string BuildListClientsMessage(const std::string& from) {
  return BuildMessageEnvelope("list-clients", from, "", nullptr).dump();
}

std::string BuildCallRequestMessage(const std::string& from,
                                    const std::string& to,
                                    const std::string& call_id) {
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  json payload = {{"callId", call_id}, {"timestamp", now_ms}};
  return BuildMessageEnvelope("call-request", from, to, std::move(payload)).dump();
}

std::string BuildCallResponseMessage(const std::string& from,
                                     const std::string& to,
                                     const std::string& call_id,
                                     bool accepted,
                                     const std::string& reason) {
  json payload = {{"callId", call_id}, {"accepted", accepted}};
  if (!reason.empty()) {
    payload["reason"] = reason;
  }
  return BuildMessageEnvelope("call-response", from, to, std::move(payload)).dump();
}

std::string BuildCallCancelMessage(const std::string& from,
                                   const std::string& to,
                                   const std::string& call_id,
                                   const std::string& reason) {
  json payload = {{"callId", call_id}};
  if (!reason.empty()) {
    payload["reason"] = reason;
  }
  return BuildMessageEnvelope("call-cancel", from, to, std::move(payload)).dump();
}

std::string BuildCallEndMessage(const std::string& from,
                                const std::string& to,
                                const std::string& call_id,
                                const std::string& reason) {
  json payload = {{"callId", call_id}};
  if (!reason.empty()) {
    payload["reason"] = reason;
  }
  return BuildMessageEnvelope("call-end", from, to, std::move(payload)).dump();
}

std::string BuildOfferMessage(const std::string& from,
                              const std::string& to,
                              const SessionDescriptionPayload& sdp) {
  json payload = {{"callId", sdp.call_id},
                  {"sdp", {{"type", sdp.type}, {"sdp", sdp.sdp}}}};
  return BuildMessageEnvelope("offer", from, to, std::move(payload)).dump();
}

std::string BuildAnswerMessage(const std::string& from,
                               const std::string& to,
                               const SessionDescriptionPayload& sdp) {
  json payload = {{"callId", sdp.call_id},
                  {"sdp", {{"type", sdp.type}, {"sdp", sdp.sdp}}}};
  return BuildMessageEnvelope("answer", from, to, std::move(payload)).dump();
}

std::string BuildIceCandidateMessage(const std::string& from,
                                     const std::string& to,
                                     const IceCandidatePayload& candidate) {
  json payload = {
      {"callId", candidate.call_id},
      {"candidate",
       {{"sdpMid", candidate.sdp_mid},
        {"sdpMLineIndex", candidate.sdp_mline_index},
        {"candidate", candidate.candidate}}}};
  return BuildMessageEnvelope("ice-candidate", from, to, std::move(payload))
      .dump();
}
