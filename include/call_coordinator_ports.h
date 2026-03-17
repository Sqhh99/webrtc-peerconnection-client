#ifndef CALL_COORDINATOR_PORTS_H_GUARD
#define CALL_COORDINATOR_PORTS_H_GUARD

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "api/scoped_refptr.h"
#include "call_signaling_transport.h"
#include "callmanager.h"
#include "local_media_source.h"
#include "signal_types.h"
#include "signalclient.h"
#include "webrtcengine.h"

namespace webrtc {
class RTCStatsReport;
}

class IWebRTCEnginePort {
 public:
  struct AudioTransportState {
    bool audio_device_module_available = false;
    bool recording_available = false;
    bool playout_available = false;
    bool local_audio_track_attached = false;
    bool remote_audio_track_attached = false;
    bool recording_active = false;
    bool playout_active = false;
  };

  virtual ~IWebRTCEnginePort() = default;

  virtual void SetObserver(WebRTCEngineObserver* observer) = 0;
  virtual void SetIceServers(const std::vector<IceServerConfig>& ice_servers) = 0;
  virtual bool Initialize() = 0;
  virtual bool CreatePeerConnection() = 0;
  virtual void ClosePeerConnection() = 0;
  virtual bool AddTracks() = 0;
  virtual void CreateOffer() = 0;
  virtual void CreateAnswer() = 0;
  virtual void SetRemoteOffer(const std::string& sdp) = 0;
  virtual void SetRemoteAnswer(const std::string& sdp) = 0;
  virtual void AddIceCandidate(const std::string& sdp_mid,
                               int sdp_mline_index,
                               const std::string& candidate) = 0;
  virtual bool HasPeerConnection() const = 0;
  virtual void CollectStats(
      std::function<void(
          const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback) = 0;
  virtual AudioTransportState GetAudioTransportState() const = 0;
  virtual bool SetLocalVideoSource(const LocalVideoSourceConfig& config,
                                   std::string* error_message) = 0;
  virtual LocalVideoSourceState GetLocalVideoSourceState() const = 0;
  virtual void Shutdown() = 0;
};

class ISignalClientPort : public CallSignalingTransport {
 public:
  ~ISignalClientPort() override = default;

  virtual void Connect(const std::string& server_url,
                       const std::string& client_id) = 0;
  virtual void Disconnect() = 0;
  virtual std::string GetClientId() const = 0;
  virtual std::vector<IceServerConfig> GetIceServers() const = 0;
  virtual void RegisterObserver(SignalClientObserver* observer) = 0;
  virtual void SendOffer(const std::string& to,
                         const SessionDescriptionPayload& sdp) = 0;
  virtual void SendAnswer(const std::string& to,
                          const SessionDescriptionPayload& sdp) = 0;
  virtual void SendIceCandidate(const std::string& to,
                                const IceCandidatePayload& candidate) = 0;
  virtual void RequestClientList() = 0;
  virtual bool InvokeOnIoThread(std::function<void()> task) = 0;
};

class ICallManagerPort {
 public:
  virtual ~ICallManagerPort() = default;

  virtual void SetSignalTransport(CallSignalingTransport* signal_transport) = 0;
  virtual void RegisterObserver(CallManagerObserver* observer) = 0;
  virtual bool InitiateCall(const std::string& target_client_id) = 0;
  virtual void AcceptCall() = 0;
  virtual void RejectCall(const std::string& reason) = 0;
  virtual void EndCall() = 0;
  virtual CallState GetCallState() const = 0;
  virtual std::string GetCurrentCallId() const = 0;
  virtual bool IsInCall() const = 0;
  virtual void NotifyPeerConnectionEstablished() = 0;
  virtual void HandleCallRequest(const std::string& from,
                                 const std::string& call_id) = 0;
  virtual void HandleCallResponse(const std::string& from,
                                  const std::string& call_id,
                                  bool accepted,
                                  const std::string& reason) = 0;
  virtual void HandleCallCancel(const std::string& from,
                                const std::string& call_id,
                                const std::string& reason) = 0;
  virtual void HandleCallEnd(const std::string& from,
                             const std::string& call_id,
                             const std::string& reason) = 0;
};

class IIceDisconnectWatchdogPort {
 public:
  virtual ~IIceDisconnectWatchdogPort() = default;

  virtual void Arm(std::function<void()> on_timeout) = 0;
  virtual void Disarm() = 0;
};

std::unique_ptr<IWebRTCEnginePort> CreateDefaultWebRTCEnginePort(
    const webrtc::Environment& env);
std::unique_ptr<ISignalClientPort> CreateDefaultSignalClientPort();
std::unique_ptr<ICallManagerPort> CreateDefaultCallManagerPort();
std::unique_ptr<IIceDisconnectWatchdogPort> CreateDefaultIceDisconnectWatchdogPort(
    std::chrono::milliseconds timeout);

#endif  // CALL_COORDINATOR_PORTS_H_GUARD
