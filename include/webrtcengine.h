#ifndef WEBRTCENGINE_H_GUARD
#define WEBRTCENGINE_H_GUARD

#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include "api/environment/environment.h"
#include "api/peer_connection_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "rtc_base/thread.h"
#include "signalclient.h"  // 包含 IceServerConfig 定义

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

// WebRTC引擎 - 封装所有WebRTC相关逻辑，与UI完全解耦
class WebRTCEngine {
 public:
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
  
  const webrtc::Environment env_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
  
  // 媒体轨道和源引用 - 用于显式停止
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> local_video_track_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> local_audio_track_;
  
  // 内部观察者对象 - 必须保持存活
  std::unique_ptr<PeerConnectionObserverImpl> pc_observer_;
  
  WebRTCEngineObserver* observer_;
  std::deque<webrtc::IceCandidate*> pending_ice_candidates_;
  std::vector<IceServerConfig> ice_servers_;  // ICE 服务器配置
  
  bool is_creating_offer_;
};

#endif  // WEBRTCENGINE_H_GUARD
