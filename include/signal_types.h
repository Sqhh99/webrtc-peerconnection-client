#ifndef SIGNAL_TYPES_H_GUARD
#define SIGNAL_TYPES_H_GUARD

#include <string>
#include <vector>

struct ClientInfo {
  std::string id;
};

struct IceServerConfig {
  std::vector<std::string> urls;
  std::string username;
  std::string credential;
};

struct SessionDescriptionPayload {
  std::string call_id;
  std::string type;
  std::string sdp;
};

struct IceCandidatePayload {
  std::string call_id;
  std::string sdp_mid;
  int sdp_mline_index = -1;
  std::string candidate;
};

#endif  // SIGNAL_TYPES_H_GUARD
