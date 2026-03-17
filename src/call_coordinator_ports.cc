#include "call_coordinator.h"

#include <chrono>
#include <memory>
#include <utility>

#include "ice_disconnect_watchdog.h"

namespace {

class DefaultWebRTCEnginePort : public IWebRTCEnginePort {
 public:
  explicit DefaultWebRTCEnginePort(const webrtc::Environment& env)
      : impl_(std::make_unique<WebRTCEngine>(env)) {}

  void SetObserver(WebRTCEngineObserver* observer) override {
    impl_->SetObserver(observer);
  }

  void SetIceServers(const std::vector<IceServerConfig>& ice_servers) override {
    impl_->SetIceServers(ice_servers);
  }

  bool Initialize() override { return impl_->Initialize(); }
  bool CreatePeerConnection() override { return impl_->CreatePeerConnection(); }
  void ClosePeerConnection() override { impl_->ClosePeerConnection(); }
  bool AddTracks() override { return impl_->AddTracks(); }
  void CreateOffer() override { impl_->CreateOffer(); }
  void CreateAnswer() override { impl_->CreateAnswer(); }
  void SetRemoteOffer(const std::string& sdp) override {
    impl_->SetRemoteOffer(sdp);
  }
  void SetRemoteAnswer(const std::string& sdp) override {
    impl_->SetRemoteAnswer(sdp);
  }
  void AddIceCandidate(const std::string& sdp_mid,
                       int sdp_mline_index,
                       const std::string& candidate) override {
    impl_->AddIceCandidate(sdp_mid, sdp_mline_index, candidate);
  }
  bool HasPeerConnection() const override { return impl_->HasPeerConnection(); }
  void CollectStats(
      std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)>
          callback) override {
    impl_->CollectStats(std::move(callback));
  }
  AudioTransportState GetAudioTransportState() const override {
    const auto state = impl_->GetAudioTransportState();
    AudioTransportState result;
    result.audio_device_module_available = state.audio_device_module_available;
    result.recording_available = state.recording_available;
    result.playout_available = state.playout_available;
    result.local_audio_track_attached = state.local_audio_track_attached;
    result.remote_audio_track_attached = state.remote_audio_track_attached;
    result.recording_active = state.recording_active;
    result.playout_active = state.playout_active;
    return result;
  }
  bool SetLocalVideoSource(const LocalVideoSourceConfig& config,
                           std::string* error_message) override {
    return impl_->SetLocalVideoSource(config, error_message);
  }
  LocalVideoSourceState GetLocalVideoSourceState() const override {
    return impl_->GetLocalVideoSourceState();
  }
  void Shutdown() override { impl_->Shutdown(); }

 private:
  std::unique_ptr<WebRTCEngine> impl_;
};

class DefaultSignalClientPort : public ISignalClientPort {
 public:
  DefaultSignalClientPort() : impl_(std::make_unique<SignalClient>()) {}

  bool IsConnected() const override { return impl_->IsConnected(); }
  void Connect(const std::string& server_url,
               const std::string& client_id) override {
    impl_->Connect(server_url, client_id);
  }
  void Disconnect() override { impl_->Disconnect(); }
  std::string GetClientId() const override { return impl_->GetClientId(); }
  std::vector<IceServerConfig> GetIceServers() const override {
    return impl_->GetIceServers();
  }
  void RegisterObserver(SignalClientObserver* observer) override {
    impl_->RegisterObserver(observer);
  }
  void SendCallRequest(const std::string& to,
                       const std::string& call_id) override {
    impl_->SendCallRequest(to, call_id);
  }
  void SendCallResponse(const std::string& to,
                        const std::string& call_id,
                        bool accepted,
                        const std::string& reason) override {
    impl_->SendCallResponse(to, call_id, accepted, reason);
  }
  void SendCallCancel(const std::string& to,
                      const std::string& call_id,
                      const std::string& reason) override {
    impl_->SendCallCancel(to, call_id, reason);
  }
  void SendCallEnd(const std::string& to,
                   const std::string& call_id,
                   const std::string& reason) override {
    impl_->SendCallEnd(to, call_id, reason);
  }
  void SendOffer(const std::string& to,
                 const SessionDescriptionPayload& sdp) override {
    impl_->SendOffer(to, sdp);
  }
  void SendAnswer(const std::string& to,
                  const SessionDescriptionPayload& sdp) override {
    impl_->SendAnswer(to, sdp);
  }
  void SendIceCandidate(const std::string& to,
                        const IceCandidatePayload& candidate) override {
    impl_->SendIceCandidate(to, candidate);
  }
  void RequestClientList() override { impl_->RequestClientList(); }
  bool InvokeOnIoThread(std::function<void()> task) override {
    return impl_->InvokeOnIoThread(std::move(task));
  }

 private:
  std::unique_ptr<SignalClient> impl_;
};

class DefaultCallManagerPort : public ICallManagerPort {
 public:
  DefaultCallManagerPort() : impl_(std::make_unique<CallManager>()) {}

  void SetSignalTransport(CallSignalingTransport* signal_transport) override {
    impl_->SetSignalTransport(signal_transport);
  }
  void RegisterObserver(CallManagerObserver* observer) override {
    impl_->RegisterObserver(observer);
  }
  bool InitiateCall(const std::string& target_client_id) override {
    return impl_->InitiateCall(target_client_id);
  }
  void AcceptCall() override { impl_->AcceptCall(); }
  void RejectCall(const std::string& reason) override {
    impl_->RejectCall(reason);
  }
  void EndCall() override { impl_->EndCall(); }
  CallState GetCallState() const override { return impl_->GetCallState(); }
  std::string GetCurrentCallId() const override {
    return impl_->GetCurrentCallId();
  }
  bool IsInCall() const override { return impl_->IsInCall(); }
  void NotifyPeerConnectionEstablished() override {
    impl_->NotifyPeerConnectionEstablished();
  }
  void HandleCallRequest(const std::string& from,
                         const std::string& call_id) override {
    impl_->HandleCallRequest(from, call_id);
  }
  void HandleCallResponse(const std::string& from,
                          const std::string& call_id,
                          bool accepted,
                          const std::string& reason) override {
    impl_->HandleCallResponse(from, call_id, accepted, reason);
  }
  void HandleCallCancel(const std::string& from,
                        const std::string& call_id,
                        const std::string& reason) override {
    impl_->HandleCallCancel(from, call_id, reason);
  }
  void HandleCallEnd(const std::string& from,
                     const std::string& call_id,
                     const std::string& reason) override {
    impl_->HandleCallEnd(from, call_id, reason);
  }

 private:
  std::unique_ptr<CallManager> impl_;
};

class DefaultIceDisconnectWatchdogPort : public IIceDisconnectWatchdogPort {
 public:
  explicit DefaultIceDisconnectWatchdogPort(std::chrono::milliseconds timeout)
      : impl_(timeout) {}

  void Arm(std::function<void()> on_timeout) override {
    impl_.Arm(std::move(on_timeout));
  }

  void Disarm() override { impl_.Disarm(); }

 private:
  IceDisconnectWatchdog impl_;
};

}  // namespace

std::unique_ptr<IWebRTCEnginePort> CreateDefaultWebRTCEnginePort(
    const webrtc::Environment& env) {
  return std::make_unique<DefaultWebRTCEnginePort>(env);
}

std::unique_ptr<ISignalClientPort> CreateDefaultSignalClientPort() {
  return std::make_unique<DefaultSignalClientPort>();
}

std::unique_ptr<ICallManagerPort> CreateDefaultCallManagerPort() {
  return std::make_unique<DefaultCallManagerPort>();
}

std::unique_ptr<IIceDisconnectWatchdogPort> CreateDefaultIceDisconnectWatchdogPort(
    std::chrono::milliseconds timeout) {
  return std::make_unique<DefaultIceDisconnectWatchdogPort>(timeout);
}

CallCoordinator::CallCoordinator(const webrtc::Environment& env)
    : CallCoordinator(env,
                      CreateDefaultWebRTCEnginePort(env),
                      CreateDefaultSignalClientPort(),
                      CreateDefaultCallManagerPort(),
                      CreateDefaultIceDisconnectWatchdogPort(
                          std::chrono::milliseconds(kIceDisconnectTimeoutMs))) {}
