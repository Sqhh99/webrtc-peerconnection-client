#ifndef CALL_COORDINATOR_H_GUARD
#define CALL_COORDINATOR_H_GUARD

#include <memory>
#include <string>
#include <mutex>

// Fix Qt emit macro conflict with WebRTC sigslot
#ifdef emit
#undef emit
#define QT_NO_EMIT_DEFINED
#endif

#include "api/environment/environment.h"
#include "api/peer_connection_interface.h"
#include "webrtcengine.h"

#ifdef QT_NO_EMIT_DEFINED
#define emit
#undef QT_NO_EMIT_DEFINED
#endif

#include "icall_observer.h"
#include "signalclient.h"
#include "callmanager.h"
#include <QJsonObject>
#include <QJsonArray>

namespace webrtc {
class RTCStatsReport;
}

// CallCoordinator - 业务协调器（原Conductor的重构版）
// 职责：协调WebRTC引擎、信令客户端和呼叫管理器
// 优点：完全与UI解耦，通过接口与UI通信，可独立测试和复用
class CallCoordinator : public WebRTCEngineObserver,
                        public SignalClientObserver,
                        public CallManagerObserver,
                        public ICallController {
 public:
  explicit CallCoordinator(const webrtc::Environment& env);
  ~CallCoordinator();
  
  // 设置UI观察者
  void SetUIObserver(ICallUIObserver* ui_observer);
  
  // ICallController 实现
  bool Initialize() override;
  void Shutdown() override;
  void ConnectToSignalServer(const std::string& url, const std::string& client_id) override;
  void DisconnectFromSignalServer() override;
  void StartCall(const std::string& peer_id) override;
  void AcceptCall() override;
  void RejectCall(const std::string& reason) override;
  void EndCall() override;
  bool IsConnectedToSignalServer() const override;
  bool IsInCall() const override;
  CallState GetCallState() const override;
  std::string GetCurrentPeerId() const override;
  std::string GetClientId() const override;
  RtcStatsSnapshot GetLatestRtcStats() override;

 private:
  // WebRTCEngineObserver 实现
  void OnLocalVideoTrackAdded(webrtc::VideoTrackInterface* track) override;
  void OnRemoteVideoTrackAdded(webrtc::VideoTrackInterface* track) override;
  void OnRemoteVideoTrackRemoved() override;
  void OnIceConnectionStateChanged(webrtc::PeerConnectionInterface::IceConnectionState state) override;
  void OnOfferCreated(const std::string& sdp) override;
  void OnAnswerCreated(const std::string& sdp) override;
  void OnIceCandidateGenerated(const std::string& sdp_mid, int sdp_mline_index, const std::string& candidate) override;
  void OnError(const std::string& error) override;
  
  // SignalClientObserver 实现
  void OnConnected(const std::string& client_id) override;
  void OnDisconnected() override;
  void OnConnectionError(const std::string& error) override;
  void OnIceServersReceived(const std::vector<IceServerConfig>& ice_servers) override;
  void OnClientListUpdate(const QJsonArray& clients) override;
  void OnUserOffline(const std::string& client_id) override;
  void OnCallRequest(const std::string& from, const QJsonObject& payload) override;
  void OnCallResponse(const std::string& from, bool accepted, const std::string& reason) override;
  void OnCallCancel(const std::string& from, const std::string& reason) override;
  void OnCallEnd(const std::string& from, const std::string& reason) override;
  void OnOffer(const std::string& from, const QJsonObject& sdp) override;
  void OnAnswer(const std::string& from, const QJsonObject& sdp) override;
  void OnIceCandidate(const std::string& from, const QJsonObject& candidate) override;

  // CallManagerObserver 实现
  void OnCallStateChanged(CallState state, const std::string& peer_id) override;
  void OnIncomingCall(const std::string& caller_id) override;
  void OnCallAccepted(const std::string& peer_id) override;
  void OnCallRejected(const std::string& peer_id, const std::string& reason) override;
  void OnCallCancelled(const std::string& peer_id, const std::string& reason) override;
  void OnCallEnded(const std::string& peer_id, const std::string& reason) override;
  void OnCallTimeout() override;
  void OnNeedCreatePeerConnection(const std::string& peer_id, bool is_caller) override;
  void OnNeedClosePeerConnection() override;

 private:
  void ProcessOffer(const std::string& from, const QJsonObject& sdp);
  void ProcessAnswer(const std::string& from, const QJsonObject& sdp);
  void ProcessIceCandidate(const std::string& from, const QJsonObject& candidate);
  void ExtractAndStoreRtcStats(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report);
  std::string IceStateToString(webrtc::PeerConnectionInterface::IceConnectionState state) const;

  // 组件
  const webrtc::Environment env_;
  std::unique_ptr<WebRTCEngine> webrtc_engine_;
  std::unique_ptr<SignalClient> signal_client_;
  std::unique_ptr<CallManager> call_manager_;
  
  // 观察者
  ICallUIObserver* ui_observer_;
  
  // 状态
  std::string current_peer_id_;
  bool is_caller_;
  std::vector<IceServerConfig> ice_servers_;
  std::string last_ice_state_;

  mutable std::mutex stats_mutex_;
  RtcStatsSnapshot last_stats_;
  bool has_stats_ = false;
  struct RateSample {
    uint64_t inbound_bytes = 0;
    uint64_t outbound_bytes = 0;
    uint64_t timestamp_ms = 0;
    bool valid = false;
  };
  RateSample last_rate_sample_;
};

#endif  // CALL_COORDINATOR_H_GUARD
