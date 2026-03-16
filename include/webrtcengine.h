#ifndef WEBRTCENGINE_H_GUARD
#define WEBRTCENGINE_H_GUARD

#include <atomic>
#include <functional>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "api/audio/audio_device.h"
#include "api/environment/environment.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "managed_video_track_source.h"
#include "rtc_base/win/scoped_com_initializer.h"
#include "rtc_base/thread.h"
#include "local_media_source.h"
#include "signal_types.h"

// WebRTC引擎观察者接口 - 业务层只需实现这个接口即可
class WebRTCEngineObserver {
 public:
  virtual ~WebRTCEngineObserver() = default;
  
  // 轨道事件
  virtual void OnLocalVideoTrackAdded(webrtc::VideoTrackInterface* track) = 0;
  virtual void OnRemoteVideoTrackAdded(webrtc::VideoTrackInterface* track) = 0;
  virtual void OnRemoteVideoTrackRemoved() = 0;
  
  // 连接状态
  virtual void OnIceConnectionStateChanged(webrtc::PeerConnectionInterface::IceConnectionState state) = 0;
  
  // SDP和ICE候选
  virtual void OnOfferCreated(const std::string& sdp) = 0;
  virtual void OnAnswerCreated(const std::string& sdp) = 0;
  virtual void OnIceCandidateGenerated(const std::string& sdp_mid, int sdp_mline_index, const std::string& candidate) = 0;
  
  // 错误处理
  virtual void OnError(const std::string& error) = 0;
};

class SwitchableAudioInput;

// WebRTC引擎 - 封装所有WebRTC相关逻辑，与UI完全解耦
class WebRTCEngine {
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

  explicit WebRTCEngine(const webrtc::Environment& env);
  ~WebRTCEngine();
  
  // 禁止拷贝和移动
  WebRTCEngine(const WebRTCEngine&) = delete;
  WebRTCEngine& operator=(const WebRTCEngine&) = delete;
  
  // 设置观察者
  void SetObserver(WebRTCEngineObserver* observer);
  
  // 设置 ICE 服务器配置
  void SetIceServers(const std::vector<IceServerConfig>& ice_servers);
  
  // 初始化
  bool Initialize();
  
  // 创建/关闭对等连接
  bool CreatePeerConnection();
  void ClosePeerConnection();
  
  // 添加媒体轨道
  bool AddTracks();
  
  // SDP操作
  void CreateOffer();
  void CreateAnswer();
  void SetRemoteOffer(const std::string& sdp);
  void SetRemoteAnswer(const std::string& sdp);
  
  // ICE候选操作
  void AddIceCandidate(const std::string& sdp_mid, int sdp_mline_index, const std::string& candidate);

  // 查询状态
  bool IsConnected() const;
  bool HasPeerConnection() const { return peer_connection_ != nullptr; }
  void CollectStats(std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback);
  AudioTransportState GetAudioTransportState() const;
  bool SetLocalVideoSource(const LocalVideoSourceConfig& config,
                           std::string* error_message);
  LocalVideoSourceState GetLocalVideoSourceState() const;

  // 生命周期
  void Shutdown();

 private:
  // 内部观察者类声明
  class PeerConnectionObserverImpl;
  class CreateSessionDescriptionObserverImpl;
  class StatsCollectorCallback;

  void SetRemoteDescription(const std::string& type, const std::string& sdp);
  void ProcessPendingIceCandidates();
  void OnPeerConnectionIceCandidate(const webrtc::IceCandidate* candidate);
  void OnPeerConnectionIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state);
  void OnPeerConnectionAddTrack(webrtc::RtpReceiverInterface* receiver);
  void OnPeerConnectionRemoveTrack(webrtc::RtpReceiverInterface* receiver);
  void OnSessionDescriptionSuccess(webrtc::SessionDescriptionInterface* desc, bool is_offer);
  void OnSessionDescriptionFailure(const std::string& error);
  bool ValidateLocalVideoSourceConfig(const LocalVideoSourceConfig& config,
                                      std::string* error_message) const;
  webrtc::scoped_refptr<ManagedVideoTrackSource> CreateVideoSourceForConfig(
      const LocalVideoSourceConfig& config,
      std::string* error_message);
  LocalVideoSourceState BuildLocalVideoSourceState(
      const LocalVideoSourceConfig& config,
      bool active) const;

  const webrtc::Environment env_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;
  std::unique_ptr<webrtc::ScopedCOMInitializer> com_initializer_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_module_;
  SwitchableAudioInput* audio_input_switcher_ = nullptr;

  // 媒体轨道和源引用 - 用于显式停止
  webrtc::scoped_refptr<ManagedVideoTrackSource> video_source_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> local_video_track_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> local_audio_track_;
  webrtc::scoped_refptr<webrtc::RtpSenderInterface> video_sender_;
  webrtc::scoped_refptr<webrtc::RtpSenderInterface> audio_sender_;
  
  // 内部观察者对象 - 必须保持存活
  std::unique_ptr<PeerConnectionObserverImpl> pc_observer_;
  
  WebRTCEngineObserver* observer_;
  std::deque<webrtc::IceCandidate*> pending_ice_candidates_;
  std::vector<IceServerConfig> ice_servers_;  // ICE 服务器配置

  mutable std::mutex video_source_mutex_;
  LocalVideoSourceConfig local_video_source_config_;
  LocalVideoSourceState local_video_source_state_;
  bool is_creating_offer_;
  std::atomic<bool> audio_device_module_available_{false};
  std::atomic<bool> recording_available_{false};
  std::atomic<bool> playout_available_{false};
  std::atomic<bool> local_audio_track_attached_{false};
  std::atomic<bool> remote_audio_track_attached_{false};
};

#endif  // WEBRTCENGINE_H_GUARD
