#include "signal_message_dispatcher.h"

#include "signaling_codec.h"

SignalMessageDispatchOutcome DispatchSignalingMessage(
    const std::string& message,
    SignalClientObserver* observer) {
  SignalMessageDispatchOutcome outcome;

  ParsedSignalingMessage parsed;
  std::string parse_error;
  if (!ParseSignalingMessage(message, &parsed, &parse_error)) {
    outcome.success = false;
    outcome.error =
        parse_error.empty() ? "Failed to parse signaling message." : parse_error;
    return outcome;
  }

  switch (parsed.type) {
    case SignalingMessageType::Registered:
      outcome.has_ice_servers = true;
      outcome.ice_servers = parsed.ice_servers;
      outcome.request_client_list = true;
      if (observer) {
        observer->OnIceServersReceived(parsed.ice_servers);
      }
      break;
    case SignalingMessageType::ClientList:
      if (observer) {
        observer->OnClientListUpdate(parsed.clients);
      }
      break;
    case SignalingMessageType::UserOffline:
      if (observer) {
        observer->OnUserOffline(parsed.offline_client_id);
      }
      break;
    case SignalingMessageType::CallRequest:
      if (observer) {
        observer->OnCallRequest(parsed.from, parsed.call_id);
      }
      break;
    case SignalingMessageType::CallResponse:
      if (observer) {
        observer->OnCallResponse(parsed.from, parsed.call_id, parsed.accepted,
                                 parsed.reason);
      }
      break;
    case SignalingMessageType::CallCancel:
      if (observer) {
        observer->OnCallCancel(parsed.from, parsed.call_id, parsed.reason);
      }
      break;
    case SignalingMessageType::CallEnd:
      if (observer) {
        observer->OnCallEnd(parsed.from, parsed.call_id, parsed.reason);
      }
      break;
    case SignalingMessageType::Offer:
      if (observer) {
        observer->OnOffer(parsed.from, parsed.session_description);
      }
      break;
    case SignalingMessageType::Answer:
      if (observer) {
        observer->OnAnswer(parsed.from, parsed.session_description);
      }
      break;
    case SignalingMessageType::IceCandidate:
      if (observer) {
        observer->OnIceCandidate(parsed.from, parsed.ice_candidate);
      }
      break;
    case SignalingMessageType::Unknown:
      break;
  }

  return outcome;
}
