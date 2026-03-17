/*
 *  WebRTC引擎实现 - 封装所有WebRTC核心逻辑，与UI完全解耦
 */

#include "webrtcengine_internal.h"

#include <utility>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
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
#include "modules/audio_device/win/audio_device_module_win.h"
#include "modules/audio_device/win/core_audio_output_win.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

WebRTCEngine::WebRTCEngine(const webrtc::Environment& env)
    : env_(env),
      observer_(nullptr),
      local_media_pipeline_(std::make_unique<LocalMediaPipeline>(env)),
      telemetry_(std::make_unique<EngineTelemetry>()),
      is_creating_offer_(false) {}

WebRTCEngine::~WebRTCEngine() {
  Shutdown();
}

void WebRTCEngine::SetObserver(WebRTCEngineObserver* observer) {
  observer_ = observer;
}

void WebRTCEngine::SetIceServers(
    const std::vector<IceServerConfig>& ice_servers) {
  ice_servers_ = ice_servers;
  RTC_LOG(LS_INFO) << "Updated ICE servers configuration, count: "
                   << ice_servers_.size();
}

std::weak_ptr<void> WebRTCEngine::GetPeerConnectionCallbackGuard() const {
  std::lock_guard<std::mutex> lock(callback_guard_mutex_);
  return peer_connection_callback_guard_;
}

void WebRTCEngine::RenewPeerConnectionCallbackGuard() {
  std::lock_guard<std::mutex> lock(callback_guard_mutex_);
  peer_connection_callback_guard_ = std::make_shared<int>(0);
}

void WebRTCEngine::ResetPeerConnectionCallbackGuard() {
  std::lock_guard<std::mutex> lock(callback_guard_mutex_);
  peer_connection_callback_guard_.reset();
}

bool WebRTCEngine::ValidateLocalVideoSourceConfig(
    const LocalVideoSourceConfig& config,
    std::string* error_message) const {
  return local_media_pipeline_->ValidateConfig(config, error_message);
}

webrtc::scoped_refptr<ManagedVideoTrackSource>
WebRTCEngine::CreateVideoSourceForConfig(const LocalVideoSourceConfig& config,
                                         std::string* error_message) {
  return local_media_pipeline_->CreateSourceForConfig(
      config, audio_input_switcher_, error_message);
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
  audio_device_module_->RecordingIsAvailable(&recording_available);
  bool playout_available = false;
  audio_device_module_->PlayoutIsAvailable(&playout_available);
  telemetry_->SetDeviceAvailability(true, recording_available,
                                    playout_available);
  RTC_LOG(LS_INFO) << "Audio device module ready: recording_available="
                   << recording_available
                   << ", playout_available=" << playout_available;

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

void WebRTCEngine::Shutdown() {
  if (shutdown_started_.exchange(true)) {
    return;
  }
  RTC_LOG(LS_INFO) << "Shutting down WebRTC Engine...";

  ClosePeerConnection();

  peer_connection_factory_ = nullptr;
  audio_device_module_ = nullptr;
  audio_input_switcher_ = nullptr;
  com_initializer_.reset();
  telemetry_->Reset();

  if (signaling_thread_) {
    signaling_thread_->Stop();
    signaling_thread_ = nullptr;
  }
  pc_observer_.reset();
  local_media_pipeline_->MarkInactive();

  RTC_LOG(LS_INFO) << "WebRTC Engine shutdown complete";
}
