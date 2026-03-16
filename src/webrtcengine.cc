/*
 *  WebRTC引擎实现 - 封装所有WebRTC核心逻辑，与UI完全解耦
 */

#include "webrtcengine.h"

#include <filesystem>
#include <optional>
#include <utility>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/test/create_frame_generator.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "media/engine/adm_helpers.h"
#include "modules/audio_device/include/audio_device_factory.h"
#include "modules/audio_device/win/audio_device_module_win.h"
#include "modules/audio_device/win/core_audio_output_win.h"
#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/win/scoped_com_initializer.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "system_wrappers/include/clock.h"
#include "test/frame_generator.h"
#include "test/frame_generator_capturer.h"
#include "test/platform_video_capturer.h"
#include "test/test_video_capturer.h"
#include "ffmpeg_file_source.h"
#include "switchable_audio_input_win.h"

namespace {

using webrtc::test::TestVideoCapturer;
namespace fs = std::filesystem;

// 设置远程描述的观察者
class SetRemoteDescriptionObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  static webrtc::scoped_refptr<SetRemoteDescriptionObserver> Create(
      std::function<void(webrtc::RTCError)> callback) {
    return webrtc::make_ref_counted<SetRemoteDescriptionObserver>(callback);
  }
  
  explicit SetRemoteDescriptionObserver(std::function<void(webrtc::RTCError)> callback)
      : callback_(callback) {}
  
  void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
    if (callback_) {
      callback_(error);
    }
  }
  
 private:
  std::function<void(webrtc::RTCError)> callback_;
};

// 设置本地描述的观察者
class SetLocalDescriptionObserver
    : public webrtc::SetLocalDescriptionObserverInterface {
 public:
  static webrtc::scoped_refptr<SetLocalDescriptionObserver> Create(
      std::function<void(webrtc::RTCError)> callback) {
    return webrtc::make_ref_counted<SetLocalDescriptionObserver>(callback);
  }
  
  explicit SetLocalDescriptionObserver(std::function<void(webrtc::RTCError)> callback)
      : callback_(callback) {}
  
  void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
    if (callback_) {
      callback_(error);
    }
  }
  
 private:
  std::function<void(webrtc::RTCError)> callback_;
};

std::string GetSourceDisplayName(const LocalVideoSourceConfig& config) {
  if (config.kind == LocalVideoSourceKind::File && !config.file_path.empty()) {
    const fs::path path(config.file_path);
    if (!path.filename().empty()) {
      return path.filename().string();
    }
    return config.file_path;
  }
  return LocalVideoSourceKindToString(config.kind);
}

// 创建视频捕获器
std::unique_ptr<TestVideoCapturer> CreateCapturer(
    webrtc::TaskQueueFactory& task_queue_factory) {
  const size_t kWidth = 640;
  const size_t kHeight = 480;
  const size_t kFps = 30;
  
  std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
      webrtc::VideoCaptureFactory::CreateDeviceInfo());
  if (!info) {
    return nullptr;
  }
  
  int num_devices = info->NumberOfDevices();
  for (int i = 0; i < num_devices; ++i) {
    std::unique_ptr<TestVideoCapturer> capturer =
        webrtc::test::CreateVideoCapturer(kWidth, kHeight, kFps, i);
    if (capturer) {
      return capturer;
    }
  }
  
  auto frame_generator = webrtc::test::CreateSquareFrameGenerator(
      kWidth, kHeight, std::nullopt, std::nullopt);
  return std::make_unique<webrtc::test::FrameGeneratorCapturer>(
      webrtc::Clock::GetRealTimeClock(), std::move(frame_generator), kFps,
      task_queue_factory);
}

// CapturerTrackSource - 视频采集源包装器
class CapturerTrackSource : public ManagedVideoTrackSource {
 public:
  static webrtc::scoped_refptr<ManagedVideoTrackSource> Create(
      webrtc::TaskQueueFactory& task_queue_factory) {
    std::unique_ptr<TestVideoCapturer> capturer =
        CreateCapturer(task_queue_factory);
    if (capturer) {
      capturer->Start();
      return webrtc::make_ref_counted<CapturerTrackSource>(std::move(capturer));
    }
    return nullptr;
  }
  
  ~CapturerTrackSource() override {
    Stop();
  }
  
  void Stop() override {
    if (capturer_) {
      capturer_->Stop();
    }
  }

 protected:
  explicit CapturerTrackSource(std::unique_ptr<TestVideoCapturer> capturer)
      : capturer_(std::move(capturer)) {}

 private:
  webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return capturer_.get();
  }

  std::unique_ptr<TestVideoCapturer> capturer_;
};

}  // namespace

// ============================================================================
// 内部观察者类实现
// ============================================================================

// PeerConnectionObserver的内部实现
class WebRTCEngine::PeerConnectionObserverImpl : public webrtc::PeerConnectionObserver {
 public:
  explicit PeerConnectionObserverImpl(WebRTCEngine* engine) : engine_(engine) {}
  
  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnAddTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                  const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override {
    engine_->OnPeerConnectionAddTrack(receiver.get());
  }
  void OnRemoveTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {
    engine_->OnPeerConnectionRemoveTrack(receiver.get());
  }
  void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override {
    engine_->OnPeerConnectionIceConnectionChange(new_state);
  }
  void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override {
    engine_->OnPeerConnectionIceCandidate(candidate);
  }
  void OnIceConnectionReceivingChange(bool receiving) override {}
  void OnIceCandidateRemoved(const webrtc::IceCandidate* candidate) override {}
  
 private:
  WebRTCEngine* engine_;
};

// CreateSessionDescriptionObserver的内部实现
class WebRTCEngine::CreateSessionDescriptionObserverImpl : public webrtc::CreateSessionDescriptionObserver {
 public:
  static webrtc::scoped_refptr<CreateSessionDescriptionObserverImpl> Create(
      WebRTCEngine* engine, bool is_offer) {
    return webrtc::make_ref_counted<CreateSessionDescriptionObserverImpl>(engine, is_offer);
  }
  
  explicit CreateSessionDescriptionObserverImpl(WebRTCEngine* engine, bool is_offer)
      : engine_(engine), is_offer_(is_offer) {}
  
  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
    RTC_LOG(LS_INFO) << "=== CreateSessionDescriptionObserver::OnSuccess called, is_offer: " << is_offer_ << " ===";
    engine_->OnSessionDescriptionSuccess(desc, is_offer_);
  }
  
  void OnFailure(webrtc::RTCError error) override {
    RTC_LOG(LS_ERROR) << "=== CreateSessionDescriptionObserver::OnFailure called: " << error.message() << " ===";
    engine_->OnSessionDescriptionFailure(error.message());
  }
  
 private:
  WebRTCEngine* engine_;
  bool is_offer_;
};

class WebRTCEngine::StatsCollectorCallback : public webrtc::RTCStatsCollectorCallback {
 public:
  explicit StatsCollectorCallback(
      std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback)
      : callback_(std::move(callback)) {}

  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
    if (callback_) {
      callback_(report);
    }
  }

 private:
  std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback_;
};

// ============================================================================
// WebRTCEngine 实现
// ============================================================================

WebRTCEngine::WebRTCEngine(const webrtc::Environment& env)
    : env_(env), observer_(nullptr), is_creating_offer_(false) {
  local_video_source_state_ =
      BuildLocalVideoSourceState(local_video_source_config_, false);
}

WebRTCEngine::~WebRTCEngine() {
  Shutdown();
}

void WebRTCEngine::SetObserver(WebRTCEngineObserver* observer) {
  observer_ = observer;
}

void WebRTCEngine::SetIceServers(const std::vector<IceServerConfig>& ice_servers) {
  ice_servers_ = ice_servers;
  RTC_LOG(LS_INFO) << "Updated ICE servers configuration, count: " << ice_servers_.size();
}

bool WebRTCEngine::ValidateLocalVideoSourceConfig(
    const LocalVideoSourceConfig& config,
    std::string* error_message) const {
  if (!IsImplementedLocalVideoSourceKind(config.kind)) {
    if (error_message) {
      *error_message =
          std::string(LocalVideoSourceKindToString(config.kind)) +
          " source is not implemented yet.";
    }
    return false;
  }

  if (config.kind != LocalVideoSourceKind::File) {
    return true;
  }

  if (config.file_path.empty()) {
    if (error_message) {
      *error_message = "A file path is required for file video source.";
    }
    return false;
  }

  return FfmpegFileSource::ProbeFile(config.file_path, error_message);
}

webrtc::scoped_refptr<ManagedVideoTrackSource>
WebRTCEngine::CreateVideoSourceForConfig(const LocalVideoSourceConfig& config,
                                         std::string* error_message) {
  switch (config.kind) {
    case LocalVideoSourceKind::Camera:
      return CapturerTrackSource::Create(env_.task_queue_factory());
    case LocalVideoSourceKind::File:
      return FfmpegFileSource::Create(env_, config.file_path,
                                      audio_input_switcher_, error_message);
    default:
      if (error_message) {
        *error_message =
            std::string(LocalVideoSourceKindToString(config.kind)) +
            " source is not implemented yet.";
      }
      return nullptr;
  }
}

LocalVideoSourceState WebRTCEngine::BuildLocalVideoSourceState(
    const LocalVideoSourceConfig& config,
    bool active) const {
  LocalVideoSourceState state;
  state.kind = config.kind;
  state.file_path = config.file_path;
  state.display_name = GetSourceDisplayName(config);
  state.active = active;
  return state;
}

bool WebRTCEngine::Initialize() {
  RTC_DCHECK(!peer_connection_factory_);
  RTC_LOG(LS_INFO) << "Initializing WebRTC Engine...";

  if (!signaling_thread_) {
    signaling_thread_ = webrtc::Thread::CreateWithSocketServer();
    signaling_thread_->Start();
  }

  webrtc::PeerConnectionFactoryDependencies deps;
  deps.signaling_thread = signaling_thread_.get();
  deps.env = env_;
  com_initializer_ = std::make_unique<webrtc::ScopedCOMInitializer>(
      webrtc::ScopedCOMInitializer::kMTA);
  if (!com_initializer_->Succeeded()) {
    RTC_LOG(LS_ERROR) << "Failed to initialize COM for audio";
    com_initializer_.reset();
    return false;
  }

  auto audio_input =
      std::make_unique<SwitchableAudioInput>(env_, /*automatic_restart=*/true);
  audio_input_switcher_ = audio_input.get();
  audio_device_module_ =
      webrtc::webrtc_win::CreateWindowsCoreAudioAudioDeviceModuleFromInputAndOutput(
          env_, std::move(audio_input),
          std::make_unique<webrtc::webrtc_win::CoreAudioOutput>(
              env_, /*automatic_restart=*/true));
  if (!audio_device_module_) {
    RTC_LOG(LS_ERROR) << "Failed to create Windows Core Audio device module";
    audio_input_switcher_ = nullptr;
    return false;
  }

  const int32_t record_device_result = audio_device_module_->SetRecordingDevice(
      webrtc::AudioDeviceModule::kDefaultCommunicationDevice);
  const int32_t playout_device_result = audio_device_module_->SetPlayoutDevice(
      webrtc::AudioDeviceModule::kDefaultDevice);
  RTC_LOG(LS_INFO) << "Selected default input/output devices. record_result="
                   << record_device_result
                   << ", playout_result=" << playout_device_result;

  webrtc::adm_helpers::Init(audio_device_module_.get());
  deps.adm = audio_device_module_;

  bool recording_available = false;
  if (audio_device_module_->RecordingIsAvailable(&recording_available) == 0) {
    recording_available_.store(recording_available);
  }
  bool playout_available = false;
  if (audio_device_module_->PlayoutIsAvailable(&playout_available) == 0) {
    playout_available_.store(playout_available);
  }
  audio_device_module_available_.store(true);
  RTC_LOG(LS_INFO) << "Audio device module ready: recording_available="
                   << recording_available_.load()
                   << ", playout_available=" << playout_available_.load();

  deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
  deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
  deps.video_encoder_factory =
      std::make_unique<webrtc::VideoEncoderFactoryTemplate<
          webrtc::LibvpxVp8EncoderTemplateAdapter,
          webrtc::LibvpxVp9EncoderTemplateAdapter,
          webrtc::OpenH264EncoderTemplateAdapter,
          webrtc::LibaomAv1EncoderTemplateAdapter>>();
  deps.video_decoder_factory =
      std::make_unique<webrtc::VideoDecoderFactoryTemplate<
          webrtc::LibvpxVp8DecoderTemplateAdapter,
          webrtc::LibvpxVp9DecoderTemplateAdapter,
          webrtc::OpenH264DecoderTemplateAdapter,
          webrtc::Dav1dDecoderTemplateAdapter>>();
  webrtc::EnableMedia(deps);

  peer_connection_factory_ =
      webrtc::CreateModularPeerConnectionFactory(std::move(deps));

  if (!peer_connection_factory_) {
    RTC_LOG(LS_ERROR) << "Failed to initialize PeerConnectionFactory";
    return false;
  }

  RTC_LOG(LS_INFO) << "WebRTC Engine initialized successfully";
  return true;
}

bool WebRTCEngine::CreatePeerConnection() {
  RTC_DCHECK(peer_connection_factory_);
  RTC_DCHECK(!peer_connection_);

  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  
  // 使用从信令服务器接收的 ICE 服务器配置，如果没有则使用默认配置
  if (!ice_servers_.empty()) {
    RTC_LOG(LS_INFO) << "Using " << ice_servers_.size() << " ICE servers from signaling server";
    for (const auto& ice_config : ice_servers_) {
      webrtc::PeerConnectionInterface::IceServer ice_server;
      ice_server.urls = ice_config.urls;
      
      if (!ice_config.username.empty()) {
        ice_server.username = ice_config.username;
      }
      if (!ice_config.credential.empty()) {
        ice_server.password = ice_config.credential;
      }
      
      config.servers.push_back(ice_server);
      
      // 记录服务器信息
      for (const auto& url : ice_server.urls) {
        RTC_LOG(LS_INFO) << "  ICE Server: " << url
                        << (ice_server.username.empty() ? "" : " (with auth)");
      }
    }
  } else {
    // 如果没有从服务器接收配置，使用默认 STUN 服务器
    RTC_LOG(LS_WARNING) << "No ICE servers from signaling server, using default STUN";
    webrtc::PeerConnectionInterface::IceServer stun_server;
    stun_server.uri = "stun:stun.l.google.com:19302";
    config.servers.push_back(stun_server);
  }
  
  // ICE 传输策略：all 表示允许使用所有候选（包括 TURN）
  config.type = webrtc::PeerConnectionInterface::kAll;
  
  // 启用 ICE 连续收集模式
  config.continual_gathering_policy = 
      webrtc::PeerConnectionInterface::GATHER_CONTINUALLY;

  // 创建并保存内部观察者 - 必须保持存活!
  pc_observer_ = std::make_unique<PeerConnectionObserverImpl>(this);
  webrtc::PeerConnectionDependencies pc_dependencies(pc_observer_.get());
  auto error_or_peer_connection =
      peer_connection_factory_->CreatePeerConnectionOrError(
          config, std::move(pc_dependencies));
          
  if (error_or_peer_connection.ok()) {
    peer_connection_ = std::move(error_or_peer_connection.value());
    RTC_LOG(LS_INFO) << "PeerConnection created successfully";
    return true;
  } else {
    RTC_LOG(LS_ERROR) << "CreatePeerConnection failed: "
                      << error_or_peer_connection.error().message();
    if (observer_) {
      observer_->OnError(error_or_peer_connection.error().message());
    }
    pc_observer_.reset();  // 清理观察者
    return false;
  }
}

void WebRTCEngine::ClosePeerConnection() {
  RTC_LOG(LS_INFO) << "Closing peer connection...";

  // 第一步: 显式停止摄像头采集(最重要!)
  if (video_source_) {
    video_source_->Stop();
    RTC_LOG(LS_INFO) << "Video source stopped";
  }
  
  // 第二步: 禁用本地媒体轨道
  if (local_video_track_) {
    local_video_track_->set_enabled(false);
    RTC_LOG(LS_INFO) << "Local video track disabled";
  }
  
  if (local_audio_track_) {
    local_audio_track_->set_enabled(false);
    RTC_LOG(LS_INFO) << "Local audio track disabled";
  }
  
  // 第三步: 移除 PeerConnection 中的所有 senders (释放对track的引用)
  if (peer_connection_) {
    auto senders = peer_connection_->GetSenders();
    for (const auto& sender : senders) {
      peer_connection_->RemoveTrackOrError(sender);
    }
    RTC_LOG(LS_INFO) << "Removed " << senders.size() << " senders from peer connection";
    
    // 关闭连接
    peer_connection_->Close();
    peer_connection_ = nullptr;
    RTC_LOG(LS_INFO) << "Peer connection closed";
  }
  
  // 第四步: 释放本地媒体轨道 (释放对source的引用)
  local_video_track_ = nullptr;
  local_audio_track_ = nullptr;
  video_sender_ = nullptr;
  audio_sender_ = nullptr;
  local_audio_track_attached_.store(false);
  remote_audio_track_attached_.store(false);

  // 第五步: 释放 video_source (现在引用计数应该为0,触发析构)
  video_source_ = nullptr;
  {
    std::lock_guard<std::mutex> lock(video_source_mutex_);
    local_video_source_state_ =
        BuildLocalVideoSourceState(local_video_source_config_, false);
  }
  if (audio_input_switcher_) {
    audio_input_switcher_->UseMicrophone();
    audio_input_switcher_->ClearSyntheticAudio();
  }
  RTC_LOG(LS_INFO) << "Video source released";
  
  // 第六步: 清理观察者和其他资源
  pc_observer_.reset();
  pending_ice_candidates_.clear();
  
  RTC_LOG(LS_INFO) << "Peer connection closed successfully";
}

bool WebRTCEngine::AddTracks() {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot add tracks: no peer connection";
    return false;
  }

  if (!peer_connection_->GetSenders().empty()) {
    RTC_LOG(LS_WARNING) << "Tracks already added";
    return true;
  }

  LocalVideoSourceConfig source_config;
  {
    std::lock_guard<std::mutex> lock(video_source_mutex_);
    source_config = local_video_source_config_;
  }

  if (audio_input_switcher_) {
    if (source_config.kind == LocalVideoSourceKind::File) {
      audio_input_switcher_->UseSyntheticPcm(48000, 2);
      audio_input_switcher_->ClearSyntheticAudio();
    } else {
      audio_input_switcher_->UseMicrophone();
      audio_input_switcher_->ClearSyntheticAudio();
    }
  }

  std::string video_source_error;
  video_source_ = CreateVideoSourceForConfig(source_config, &video_source_error);
  if (!video_source_) {
    RTC_LOG(LS_ERROR) << "Failed to create local video source: "
                      << video_source_error;
    if (observer_) {
      observer_->OnError(video_source_error.empty()
                             ? "Failed to create local video source"
                             : video_source_error);
    }
    return false;
  }

  local_video_track_ =
      peer_connection_factory_->CreateVideoTrack(video_source_, "video_label");
  auto video_result_or_error =
      peer_connection_->AddTrack(local_video_track_, {"stream_id"});
  if (!video_result_or_error.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to add video track: "
                      << video_result_or_error.error().message();
    if (observer_) {
      observer_->OnError("Failed to add video track");
    }
    return false;
  }

  video_sender_ = video_result_or_error.value();
  {
    std::lock_guard<std::mutex> lock(video_source_mutex_);
    local_video_source_state_ =
        BuildLocalVideoSourceState(source_config, true);
  }
  if (observer_) {
    observer_->OnLocalVideoTrackAdded(local_video_track_.get());
  }

  // 添加音频轨道
  webrtc::AudioOptions audio_options;
  auto audio_source = peer_connection_factory_->CreateAudioSource(audio_options);
  local_audio_track_ = peer_connection_factory_->CreateAudioTrack("audio_label", audio_source.get());
  auto result_or_error = peer_connection_->AddTrack(local_audio_track_, {"stream_id"});

  if (!result_or_error.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to add audio track: "
                      << result_or_error.error().message();
    if (observer_) {
      observer_->OnError("Failed to add audio track");
    }
    return false;
  }

  audio_sender_ = result_or_error.value();
  local_audio_track_attached_.store(true);
  RTC_LOG(LS_INFO) << "Local audio track added successfully";

  return true;
}

void WebRTCEngine::CreateOffer() {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot create offer: no peer connection";
    return;
  }

  RTC_LOG(LS_INFO) << "=== Creating Offer ===";
  is_creating_offer_ = true;
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = true;
  options.offer_to_receive_video = true;
  
  auto observer = CreateSessionDescriptionObserverImpl::Create(this, true);
  peer_connection_->CreateOffer(observer.get(), options);
  RTC_LOG(LS_INFO) << "CreateOffer called on peer_connection";
}

void WebRTCEngine::CreateAnswer() {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot create answer: no peer connection";
    return;
  }

  RTC_LOG(LS_INFO) << "=== Creating Answer ===";
  is_creating_offer_ = false;
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  
  auto observer = CreateSessionDescriptionObserverImpl::Create(this, false);
  peer_connection_->CreateAnswer(observer.get(), options);
  RTC_LOG(LS_INFO) << "CreateAnswer called on peer_connection";
}

void WebRTCEngine::SetRemoteOffer(const std::string& sdp) {
  SetRemoteDescription("offer", sdp);
}

void WebRTCEngine::SetRemoteAnswer(const std::string& sdp) {
  SetRemoteDescription("answer", sdp);
}

void WebRTCEngine::SetRemoteDescription(const std::string& type, const std::string& sdp) {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot set remote description: no peer connection";
    return;
  }

  webrtc::SdpType sdp_type = (type == "offer") ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer;
  webrtc::SdpParseError error;
  auto session_desc = webrtc::CreateSessionDescription(sdp_type, sdp, &error);
  
  if (!session_desc) {
    RTC_LOG(LS_ERROR) << "Failed to parse SDP: " << error.description;
    if (observer_) {
      observer_->OnError("Failed to parse SDP: " + error.description);
    }
    return;
  }

  auto observer = SetRemoteDescriptionObserver::Create([this](webrtc::RTCError error) {
    if (!error.ok()) {
      RTC_LOG(LS_ERROR) << "SetRemoteDescription failed: " << error.message();
      if (observer_) {
        observer_->OnError(std::string("SetRemoteDescription failed: ") + error.message());
      }
    } else {
      RTC_LOG(LS_INFO) << "SetRemoteDescription succeeded";
      ProcessPendingIceCandidates();
    }
  });

  peer_connection_->SetRemoteDescription(std::move(session_desc), observer);
}

void WebRTCEngine::AddIceCandidate(const std::string& sdp_mid, 
                                    int sdp_mline_index, 
                                    const std::string& candidate) {
  if (!peer_connection_) {
    RTC_LOG(LS_WARNING) << "Cannot add ICE candidate: no peer connection";
    return;
  }

  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::IceCandidate> ice_candidate(
      webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate, &error));
      
  if (!ice_candidate) {
    RTC_LOG(LS_ERROR) << "Failed to parse ICE candidate: " << error.description;
    return;
  }

  if (!peer_connection_->remote_description()) {
    RTC_LOG(LS_INFO) << "Remote description not set yet, queueing ICE candidate";
    pending_ice_candidates_.push_back(ice_candidate.release());
    return;
  }

  if (!peer_connection_->AddIceCandidate(ice_candidate.get())) {
    RTC_LOG(LS_ERROR) << "Failed to add ICE candidate";
  }
}

void WebRTCEngine::ProcessPendingIceCandidates() {
  if (!peer_connection_ || !peer_connection_->remote_description()) {
    return;
  }

  for (const auto* candidate : pending_ice_candidates_) {
    if (!peer_connection_->AddIceCandidate(candidate)) {
      RTC_LOG(LS_ERROR) << "Failed to add pending ICE candidate";
    }
    delete candidate;
  }
  pending_ice_candidates_.clear();
}

bool WebRTCEngine::IsConnected() const {
  if (!peer_connection_) {
    return false;
  }
  
  auto state = peer_connection_->ice_connection_state();
  return state == webrtc::PeerConnectionInterface::kIceConnectionConnected ||
         state == webrtc::PeerConnectionInterface::kIceConnectionCompleted;
}

void WebRTCEngine::CollectStats(
    std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback) {
  if (!peer_connection_) {
    if (callback) {
      callback(nullptr);
    }
    return;
  }
  peer_connection_->GetStats(new webrtc::RefCountedObject<StatsCollectorCallback>(std::move(callback)));
}

bool WebRTCEngine::SetLocalVideoSource(const LocalVideoSourceConfig& config,
                                       std::string* error_message) {
  if (!ValidateLocalVideoSourceConfig(config, error_message)) {
    return false;
  }

  if (!peer_connection_ || !peer_connection_factory_ || !video_sender_) {
    std::lock_guard<std::mutex> lock(video_source_mutex_);
    local_video_source_config_ = config;
    local_video_source_state_ = BuildLocalVideoSourceState(config, false);
    return true;
  }

  auto new_source = CreateVideoSourceForConfig(config, error_message);
  if (!new_source) {
    if (error_message && error_message->empty()) {
      *error_message = "Failed to create the requested local video source.";
    }
    return false;
  }

  auto new_track =
      peer_connection_factory_->CreateVideoTrack(new_source, "video_label");
  if (!video_sender_->SetTrack(new_track.get())) {
    new_source->Stop();
    if (error_message) {
      *error_message = "Failed to replace the local video track.";
    }
    return false;
  }

  if (audio_input_switcher_) {
    if (config.kind == LocalVideoSourceKind::File) {
      audio_input_switcher_->UseSyntheticPcm(48000, 2);
      audio_input_switcher_->ClearSyntheticAudio();
    } else {
      audio_input_switcher_->UseMicrophone();
      audio_input_switcher_->ClearSyntheticAudio();
    }
  }

  auto previous_source = video_source_;
  auto previous_track = local_video_track_;
  {
    std::lock_guard<std::mutex> lock(video_source_mutex_);
    local_video_source_config_ = config;
    local_video_source_state_ = BuildLocalVideoSourceState(config, true);
    video_source_ = new_source;
    local_video_track_ = new_track;
  }

  if (previous_track) {
    previous_track->set_enabled(false);
  }
  if (previous_source) {
    previous_source->Stop();
  }
  if (observer_) {
    observer_->OnLocalVideoTrackAdded(local_video_track_.get());
  }
  return true;
}

LocalVideoSourceState WebRTCEngine::GetLocalVideoSourceState() const {
  std::lock_guard<std::mutex> lock(video_source_mutex_);
  return local_video_source_state_;
}

void WebRTCEngine::Shutdown() {
  RTC_LOG(LS_INFO) << "Shutting down WebRTC Engine...";

  // 关闭对等连接
  ClosePeerConnection();

  // 释放工厂（这会停止所有线程）
  peer_connection_factory_ = nullptr;
  audio_device_module_ = nullptr;
  audio_input_switcher_ = nullptr;
  com_initializer_.reset();
  audio_device_module_available_.store(false);
  recording_available_.store(false);
  playout_available_.store(false);
  local_audio_track_attached_.store(false);
  remote_audio_track_attached_.store(false);

  // 停止信令线程
  if (signaling_thread_) {
    signaling_thread_->Stop();
    signaling_thread_ = nullptr;
  }

  {
    std::lock_guard<std::mutex> lock(video_source_mutex_);
    local_video_source_state_ =
        BuildLocalVideoSourceState(local_video_source_config_, false);
  }

  RTC_LOG(LS_INFO) << "WebRTC Engine shutdown complete";
}

WebRTCEngine::AudioTransportState WebRTCEngine::GetAudioTransportState() const {
  AudioTransportState state;
  state.audio_device_module_available = audio_device_module_available_.load();
  state.recording_available = recording_available_.load();
  state.playout_available = playout_available_.load();
  state.local_audio_track_attached = local_audio_track_attached_.load();
  state.remote_audio_track_attached = remote_audio_track_attached_.load();
  if (audio_device_module_) {
    state.recording_active = audio_device_module_->Recording();
    state.playout_active = audio_device_module_->Playing();
  }
  return state;
}

// ============================================================================
// 内部回调方法 - 从观察者类调用
// ============================================================================

void WebRTCEngine::OnPeerConnectionAddTrack(webrtc::RtpReceiverInterface* receiver) {
  RTC_LOG(LS_INFO) << "Track added: " << receiver->id();
  auto* track = receiver->track().get();

  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    auto* video_track = static_cast<webrtc::VideoTrackInterface*>(track);
    if (observer_) {
      observer_->OnRemoteVideoTrackAdded(video_track);
    }
  } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
    remote_audio_track_attached_.store(true);
    RTC_LOG(LS_INFO) << "Remote audio track added";
  }
}

void WebRTCEngine::OnPeerConnectionRemoveTrack(webrtc::RtpReceiverInterface* receiver) {
  RTC_LOG(LS_INFO) << "Track removed: " << receiver->id();
  auto* track = receiver->track().get();
  
  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    if (observer_) {
      observer_->OnRemoteVideoTrackRemoved();
    }
  } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
    remote_audio_track_attached_.store(false);
    RTC_LOG(LS_INFO) << "Remote audio track removed";
  }
}

void WebRTCEngine::OnPeerConnectionIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  RTC_LOG(LS_INFO) << "ICE connection state changed: " << new_state;
  
  if (observer_) {
    observer_->OnIceConnectionStateChanged(new_state);
  }
}

void WebRTCEngine::OnPeerConnectionIceCandidate(const webrtc::IceCandidate* candidate) {
  RTC_LOG(LS_INFO) << "ICE candidate generated: " << candidate->sdp_mline_index();
  
  std::string candidate_str;
  if (candidate->ToString(&candidate_str)) {
    if (observer_) {
      observer_->OnIceCandidateGenerated(
          candidate->sdp_mid(), 
          candidate->sdp_mline_index(), 
          candidate_str);
    }
  }
}

void WebRTCEngine::OnSessionDescriptionSuccess(webrtc::SessionDescriptionInterface* desc, bool is_offer) {
  RTC_LOG(LS_INFO) << "=== OnSessionDescriptionSuccess called, is_offer: " << is_offer << " ===";
  
  std::string sdp;
  desc->ToString(&sdp);

  auto observer = SetLocalDescriptionObserver::Create([this, sdp, is_offer](webrtc::RTCError error) {
    if (!error.ok()) {
      RTC_LOG(LS_ERROR) << "SetLocalDescription failed: " << error.message();
      if (observer_) {
        observer_->OnError(std::string("SetLocalDescription failed: ") + error.message());
      }
    } else {
      RTC_LOG(LS_INFO) << "SetLocalDescription succeeded, is_offer: " << is_offer;
      
      if (observer_) {
        if (is_offer) {
          RTC_LOG(LS_INFO) << "Calling observer_->OnOfferCreated()";
          observer_->OnOfferCreated(sdp);
        } else {
          RTC_LOG(LS_INFO) << "Calling observer_->OnAnswerCreated()";
          observer_->OnAnswerCreated(sdp);
        }
      } else {
        RTC_LOG(LS_ERROR) << "observer_ is null!";
      }
    }
  });

  RTC_LOG(LS_INFO) << "Calling SetLocalDescription...";
  peer_connection_->SetLocalDescription(
      std::unique_ptr<webrtc::SessionDescriptionInterface>(desc), observer);
}

void WebRTCEngine::OnSessionDescriptionFailure(const std::string& error) {
  RTC_LOG(LS_ERROR) << "Create session description failed: " << error;
  
  if (observer_) {
    observer_->OnError("Create session description failed: " + error);
  }
}
