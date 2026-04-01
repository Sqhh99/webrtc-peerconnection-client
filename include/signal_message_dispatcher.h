#ifndef SIGNAL_MESSAGE_DISPATCHER_H_GUARD
#define SIGNAL_MESSAGE_DISPATCHER_H_GUARD

#include <string>
#include <vector>

#include "signal_types.h"
#include "signalclient.h"

struct SignalMessageDispatchOutcome {
  bool success = true;
  std::string error;
  bool has_ice_servers = false;
  bool request_client_list = false;
  std::vector<IceServerConfig> ice_servers;
};

SignalMessageDispatchOutcome DispatchSignalingMessage(
    const std::string& message,
    SignalClientObserver* observer);

#endif  // SIGNAL_MESSAGE_DISPATCHER_H_GUARD
