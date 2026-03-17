#ifndef SIGNALING_CODEC_H_GUARD
#define SIGNALING_CODEC_H_GUARD

#include <string>
#include <vector>

#include "signal_types.h"

enum class SignalingMessageType {
  Registered,
  ClientList,
  UserOffline,
  CallRequest,
  CallResponse,
  CallCancel,
  CallEnd,
  Offer,
  Answer,
  IceCandidate,
  Unknown
};

struct ParsedSignalingMessage {
  SignalingMessageType type = SignalingMessageType::Unknown;
  std::string from;
  std::vector<IceServerConfig> ice_servers;
  std::vector<ClientInfo> clients;
  std::string offline_client_id;
  std::string call_id;
  bool accepted = false;
  std::string reason;
  SessionDescriptionPayload session_description;
  IceCandidatePayload ice_candidate;
};

bool ParseSignalingMessage(const std::string& message,
                           ParsedSignalingMessage* parsed,
                           std::string* error_message);

std::string BuildRegisterMessage(const std::string& from);
std::string BuildListClientsMessage(const std::string& from);
std::string BuildCallRequestMessage(const std::string& from,
                                    const std::string& to,
                                    const std::string& call_id);
std::string BuildCallResponseMessage(const std::string& from,
                                     const std::string& to,
                                     const std::string& call_id,
                                     bool accepted,
                                     const std::string& reason);
std::string BuildCallCancelMessage(const std::string& from,
                                   const std::string& to,
                                   const std::string& call_id,
                                   const std::string& reason);
std::string BuildCallEndMessage(const std::string& from,
                                const std::string& to,
                                const std::string& call_id,
                                const std::string& reason);
std::string BuildOfferMessage(const std::string& from,
                              const std::string& to,
                              const SessionDescriptionPayload& sdp);
std::string BuildAnswerMessage(const std::string& from,
                               const std::string& to,
                               const SessionDescriptionPayload& sdp);
std::string BuildIceCandidateMessage(const std::string& from,
                                     const std::string& to,
                                     const IceCandidatePayload& candidate);

#endif  // SIGNALING_CODEC_H_GUARD
