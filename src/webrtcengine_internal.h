#ifndef WEBRTCENGINE_INTERNAL_H_GUARD
#define WEBRTCENGINE_INTERNAL_H_GUARD

#include "webrtcengine.h"

#include <filesystem>
#include <optional>
#include <utility>

#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/test/create_frame_generator.h"
#include "ffmpeg_file_source.h"
#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/logging.h"
#include "switchable_audio_input_win.h"
#include "system_wrappers/include/clock.h"
#include "test/frame_generator.h"
#include "test/frame_generator_capturer.h"
#include "test/platform_video_capturer.h"
#include "test/test_video_capturer.h"

namespace webrtcengine_internal {

using webrtc::test::TestVideoCapturer;
namespace fs = std::filesystem;

inline const char* BoolToString(bool value) {
  return value ? "true" : "false";
}

inline bool HasLocalSenderTrack(
    const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>&
        peer_connection,
    const char* kind) {
  if (!peer_connection) {
    return false;
  }

  for (const auto& sender : peer_connection->GetSenders()) {
    if (!sender) {
      continue;
    }
    auto track = sender->track();
    if (track && track->kind() == kind) {
      return true;
    }
  }
  return false;
}

inline std::string GetSourceDisplayName(const LocalVideoSourceConfig& config) {
  if (config.kind == LocalVideoSourceKind::File && !config.file_path.empty()) {
    const fs::path path(config.file_path);
    if (!path.filename().empty()) {
      return path.filename().string();
    }
    return config.file_path;
  }
  return LocalVideoSourceKindToString(config.kind);
}

inline std::unique_ptr<TestVideoCapturer> CreateCapturer(
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

  ~CapturerTrackSource() override { Stop(); }

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

class SetRemoteDescriptionObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  static webrtc::scoped_refptr<SetRemoteDescriptionObserver> Create(
      std::function<void(webrtc::RTCError)> callback) {
    return webrtc::make_ref_counted<SetRemoteDescriptionObserver>(callback);
  }

  explicit SetRemoteDescriptionObserver(
      std::function<void(webrtc::RTCError)> callback)
      : callback_(std::move(callback)) {}

  void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
    if (callback_) {
      callback_(error);
    }
  }

 private:
  std::function<void(webrtc::RTCError)> callback_;
};

class SetLocalDescriptionObserver
    : public webrtc::SetLocalDescriptionObserverInterface {
 public:
  static webrtc::scoped_refptr<SetLocalDescriptionObserver> Create(
      std::function<void(webrtc::RTCError)> callback) {
    return webrtc::make_ref_counted<SetLocalDescriptionObserver>(callback);
  }

  explicit SetLocalDescriptionObserver(
      std::function<void(webrtc::RTCError)> callback)
      : callback_(std::move(callback)) {}

  void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
    if (callback_) {
      callback_(error);
    }
  }

 private:
  std::function<void(webrtc::RTCError)> callback_;
};

}  // namespace webrtcengine_internal

class WebRTCEngine::PeerConnectionObserverImpl
    : public webrtc::PeerConnectionObserver {
 public:
  explicit PeerConnectionObserverImpl(WebRTCEngine* engine,
                                      std::weak_ptr<void> callback_guard)
      : engine_(engine), callback_guard_(std::move(callback_guard)) {}

  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState) override {}

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {
    if (callback_guard_.expired()) {
      return;
    }
    engine_->OnPeerConnectionTrack(transceiver.get());
  }

  void OnAddTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&) override {
    if (callback_guard_.expired()) {
      return;
    }
    engine_->OnPeerConnectionAddTrack(receiver.get());
  }

  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {
    if (callback_guard_.expired()) {
      return;
    }
    engine_->OnPeerConnectionRemoveTrack(receiver.get());
  }

  void OnDataChannel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
  void OnRenegotiationNeeded() override {}

  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override {
    if (callback_guard_.expired()) {
      return;
    }
    engine_->OnPeerConnectionIceConnectionChange(new_state);
  }

  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState) override {}

  void OnIceCandidate(const webrtc::IceCandidate* candidate) override {
    if (callback_guard_.expired()) {
      return;
    }
    engine_->OnPeerConnectionIceCandidate(candidate);
  }

  void OnIceConnectionReceivingChange(bool) override {}
  void OnIceCandidateRemoved(const webrtc::IceCandidate*) override {}

 private:
  WebRTCEngine* engine_;
  std::weak_ptr<void> callback_guard_;
};

class WebRTCEngine::CreateSessionDescriptionObserverImpl
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  static webrtc::scoped_refptr<CreateSessionDescriptionObserverImpl> Create(
      WebRTCEngine* engine,
      std::weak_ptr<void> callback_guard,
      bool is_offer) {
    return webrtc::make_ref_counted<CreateSessionDescriptionObserverImpl>(
        engine, std::move(callback_guard), is_offer);
  }

  explicit CreateSessionDescriptionObserverImpl(
      WebRTCEngine* engine,
      std::weak_ptr<void> callback_guard,
      bool is_offer)
      : engine_(engine),
        callback_guard_(std::move(callback_guard)),
        is_offer_(is_offer) {}

  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
    if (callback_guard_.expired()) {
      delete desc;
      return;
    }
    RTC_LOG(LS_INFO)
        << "=== CreateSessionDescriptionObserver::OnSuccess called, is_offer: "
        << is_offer_ << " ===";
    engine_->OnSessionDescriptionSuccess(desc, is_offer_);
  }

  void OnFailure(webrtc::RTCError error) override {
    if (callback_guard_.expired()) {
      return;
    }
    RTC_LOG(LS_ERROR)
        << "=== CreateSessionDescriptionObserver::OnFailure called: "
        << error.message() << " ===";
    engine_->OnSessionDescriptionFailure(error.message());
  }

 private:
  WebRTCEngine* engine_;
  std::weak_ptr<void> callback_guard_;
  bool is_offer_;
};

class WebRTCEngine::StatsCollectorCallback
    : public webrtc::RTCStatsCollectorCallback {
 public:
  explicit StatsCollectorCallback(std::function<void(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback)
      : callback_(std::move(callback)) {}

  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
      override {
    if (callback_) {
      callback_(report);
    }
  }

 private:
  std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)>
      callback_;
};

class WebRTCEngine::LocalMediaPipeline {
 public:
  explicit LocalMediaPipeline(const webrtc::Environment& env) : env_(env) {
    state_ = BuildState(config_, false);
  }

  bool ValidateConfig(const LocalVideoSourceConfig& config,
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

  webrtc::scoped_refptr<ManagedVideoTrackSource> CreateSourceForConfig(
      const LocalVideoSourceConfig& config,
      SwitchableAudioInput* audio_input_switcher,
      std::string* error_message) const {
    switch (config.kind) {
      case LocalVideoSourceKind::Camera:
        return webrtcengine_internal::CapturerTrackSource::Create(
            env_.task_queue_factory());
      case LocalVideoSourceKind::File:
        return FfmpegFileSource::Create(env_, config.file_path,
                                        audio_input_switcher, error_message);
      default:
        if (error_message) {
          *error_message =
              std::string(LocalVideoSourceKindToString(config.kind)) +
              " source is not implemented yet.";
        }
        return nullptr;
    }
  }

  LocalVideoSourceConfig GetConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
  }

  LocalVideoSourceState GetState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  void SetConfig(const LocalVideoSourceConfig& config, bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    state_ = BuildState(config_, active);
  }

  void ConfigureAudioInputForSource(
      const LocalVideoSourceConfig& config,
      SwitchableAudioInput* audio_input_switcher) const {
    if (!audio_input_switcher) {
      return;
    }
    if (config.kind == LocalVideoSourceKind::File) {
      audio_input_switcher->UseSyntheticPcm(48000, 2);
      audio_input_switcher->ClearSyntheticAudio();
      return;
    }
    audio_input_switcher->UseMicrophone();
    audio_input_switcher->ClearSyntheticAudio();
  }

  void MarkInactive() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = BuildState(config_, false);
  }

 private:
  static LocalVideoSourceState BuildState(const LocalVideoSourceConfig& config,
                                          bool active) {
    LocalVideoSourceState state;
    state.kind = config.kind;
    state.file_path = config.file_path;
    state.display_name = webrtcengine_internal::GetSourceDisplayName(config);
    state.active = active;
    return state;
  }

  const webrtc::Environment& env_;
  mutable std::mutex mutex_;
  LocalVideoSourceConfig config_;
  LocalVideoSourceState state_;
};

class WebRTCEngine::EngineTelemetry {
 public:
  void SetDeviceAvailability(bool audio_device_module_available,
                             bool recording_available,
                             bool playout_available) {
    audio_device_module_available_.store(audio_device_module_available);
    recording_available_.store(recording_available);
    playout_available_.store(playout_available);
  }

  void SetLocalAudioTrackAttached(bool attached) {
    local_audio_track_attached_.store(attached);
  }

  void SetRemoteAudioTrackAttached(bool attached) {
    remote_audio_track_attached_.store(attached);
  }

  void Reset() {
    SetDeviceAvailability(false, false, false);
    SetLocalAudioTrackAttached(false);
    SetRemoteAudioTrackAttached(false);
  }

  WebRTCEngine::AudioTransportState Snapshot(
      const webrtc::scoped_refptr<webrtc::AudioDeviceModule>&
          audio_device_module) const {
    WebRTCEngine::AudioTransportState state;
    state.audio_device_module_available =
        audio_device_module_available_.load();
    state.recording_available = recording_available_.load();
    state.playout_available = playout_available_.load();
    state.local_audio_track_attached = local_audio_track_attached_.load();
    state.remote_audio_track_attached = remote_audio_track_attached_.load();
    if (audio_device_module) {
      state.recording_active = audio_device_module->Recording();
      state.playout_active = audio_device_module->Playing();
    }
    return state;
  }

 private:
  std::atomic<bool> audio_device_module_available_{false};
  std::atomic<bool> recording_available_{false};
  std::atomic<bool> playout_available_{false};
  std::atomic<bool> local_audio_track_attached_{false};
  std::atomic<bool> remote_audio_track_attached_{false};
};

#endif  // WEBRTCENGINE_INTERNAL_H_GUARD
