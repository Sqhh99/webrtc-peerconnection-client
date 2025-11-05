#ifndef ICALL_OBSERVER_H_GUARD
#define ICALL_OBSERVER_H_GUARD

#include <string>
#include <cstdint>
#include "api/media_stream_interface.h"
#include "callmanager.h"
#include <QJsonArray>

// UI观察者接口 - 定义UI层需要实现的回调方法
// 这样Coordinator就不需要依赖具体的UI实现
class ICallUIObserver {
 public:
  virtual ~ICallUIObserver() = default;
  
  // 视频渲染回调
  virtual void OnStartLocalRenderer(webrtc::VideoTrackInterface* track) = 0;
  virtual void OnStopLocalRenderer() = 0;
  virtual void OnStartRemoteRenderer(webrtc::VideoTrackInterface* track) = 0;
  virtual void OnStopRemoteRenderer() = 0;
  
  // 日志和消息回调
  virtual void OnLogMessage(const std::string& message, const std::string& level) = 0;
  virtual void OnShowError(const std::string& title, const std::string& message) = 0;
  virtual void OnShowInfo(const std::string& title, const std::string& message) = 0;
  
  // 连接状态回调
  virtual void OnSignalConnected(const std::string& client_id) = 0;
  virtual void OnSignalDisconnected() = 0;
  virtual void OnSignalError(const std::string& error) = 0;
  
  // 客户端列表更新
  virtual void OnClientListUpdate(const QJsonArray& clients) = 0;
  
  // 呼叫状态回调
  virtual void OnCallStateChanged(CallState state, const std::string& peer_id) = 0;
  virtual void OnIncomingCall(const std::string& caller_id) = 0;
};

// 业务控制接口 - 定义UI层可以调用的业务方法
// UI层通过这个接口与业务层交互，而不是直接调用内部组件
struct RtcStatsSnapshot {
  bool valid = false;
  std::string ice_state;
  std::string local_candidate_summary;
  std::string remote_candidate_summary;
  double outbound_bitrate_kbps = 0.0;
  double inbound_bitrate_kbps = 0.0;
  double current_rtt_ms = 0.0;
  double inbound_audio_jitter_ms = 0.0;
  double inbound_audio_packet_loss_percent = 0.0;
  double inbound_video_packet_loss_percent = 0.0;
  double inbound_video_fps = 0.0;
  int inbound_video_width = 0;
  int inbound_video_height = 0;
  uint64_t timestamp_ms = 0;
};

class ICallController {
 public:
  virtual ~ICallController() = default;
  
  // 初始化和清理
  virtual bool Initialize() = 0;
  virtual void Shutdown() = 0;
  
  // 信令连接
  virtual void ConnectToSignalServer(const std::string& url, const std::string& client_id) = 0;
  virtual void DisconnectFromSignalServer() = 0;
  
  // 呼叫控制
  virtual void StartCall(const std::string& peer_id) = 0;
  virtual void AcceptCall() = 0;
  virtual void RejectCall(const std::string& reason) = 0;
  virtual void EndCall() = 0;
  
  // 状态查询
  virtual bool IsConnectedToSignalServer() const = 0;
  virtual bool IsInCall() const = 0;
  virtual CallState GetCallState() const = 0;
  virtual std::string GetCurrentPeerId() const = 0;
  virtual std::string GetClientId() const = 0;
  
  // WebRTC实时数据
  virtual RtcStatsSnapshot GetLatestRtcStats() = 0;
};

#endif  // ICALL_OBSERVER_H_GUARD
