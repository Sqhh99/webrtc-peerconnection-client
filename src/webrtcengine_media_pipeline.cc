#include "webrtcengine_internal.h"

#include <optional>

#include "api/audio_options.h"
#include "api/test/create_frame_generator.h"
#include "modules/video_capture/video_capture_factory.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"
#include "media_support/frame_generator.h"
#include "media_support/frame_generator_capturer.h"
#include "media_support/platform_video_capturer.h"
#include "media_support/test_video_capturer.h"

namespace {

using webrtc::test::TestVideoCapturer;

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

  const int num_devices = info->NumberOfDevices();
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
    if (!capturer) {
      return nullptr;
    }
    capturer->Start();
    return webrtc::make_ref_counted<CapturerTrackSource>(std::move(capturer));
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

}  // namespace

namespace webrtcengine_internal {

webrtc::scoped_refptr<ManagedVideoTrackSource> CreateCapturerTrackSource(
    webrtc::TaskQueueFactory& task_queue_factory) {
  return CapturerTrackSource::Create(task_queue_factory);
}

}  // namespace webrtcengine_internal

bool WebRTCEngine::AddTracks() {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot add tracks: no peer connection";
    return false;
  }

  const bool has_video_sender = webrtcengine_internal::HasLocalSenderTrack(
      peer_connection_, webrtc::MediaStreamTrackInterface::kVideoKind);
  const bool has_audio_sender = webrtcengine_internal::HasLocalSenderTrack(
      peer_connection_, webrtc::MediaStreamTrackInterface::kAudioKind);
  if (has_video_sender && has_audio_sender) {
    RTC_LOG(LS_INFO) << "Local audio/video tracks already attached";
    return true;
  }

  const LocalVideoSourceConfig source_config = local_media_pipeline_->GetConfig();
  local_media_pipeline_->ConfigureAudioInputForSource(source_config,
                                                      audio_input_switcher_);

  if (!has_video_sender) {
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
    local_media_pipeline_->SetConfig(source_config, true);
    if (observer_) {
      observer_->OnLocalVideoTrackAdded(local_video_track_.get());
    }
  }

  if (!has_audio_sender) {
    webrtc::AudioOptions audio_options;
    auto audio_source =
        peer_connection_factory_->CreateAudioSource(audio_options);
    local_audio_track_ =
        peer_connection_factory_->CreateAudioTrack("audio_label",
                                                   audio_source.get());
    auto result_or_error =
        peer_connection_->AddTrack(local_audio_track_, {"stream_id"});

    if (!result_or_error.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to add audio track: "
                        << result_or_error.error().message();
      if (observer_) {
        observer_->OnError("Failed to add audio track");
      }
      return false;
    }

    audio_sender_ = result_or_error.value();
    telemetry_->SetLocalAudioTrackAttached(true);
    RTC_LOG(LS_INFO) << "Local audio track added successfully";
  }

  return true;
}

void WebRTCEngine::LogRemoteMediaState(const char* reason) const {
  if (!peer_connection_) {
    RTC_LOG(LS_INFO) << "Remote media state [" << reason
                     << "]: no peer connection";
    return;
  }

  const auto receivers = peer_connection_->GetReceivers();
  RTC_LOG(LS_INFO) << "Remote media state [" << reason
                   << "]: receivers=" << receivers.size()
                   << ", has_remote_description="
                   << webrtcengine_internal::BoolToString(
                          peer_connection_->remote_description() != nullptr)
                   << ", published_remote_video_track="
                   << (remote_video_track_ ? remote_video_track_->id()
                                           : std::string("<none>"));
  for (size_t i = 0; i < receivers.size(); ++i) {
    const auto& receiver = receivers[i];
    if (!receiver) {
      RTC_LOG(LS_INFO) << "  receiver[" << i << "]=<null>";
      continue;
    }

    auto track = receiver->track();
    RTC_LOG(LS_INFO) << "  receiver[" << i << "] id=" << receiver->id()
                     << ", has_track="
                     << webrtcengine_internal::BoolToString(track != nullptr);
    if (track) {
      RTC_LOG(LS_INFO) << "    track kind=" << track->kind()
                       << ", id=" << track->id()
                       << ", enabled="
                       << webrtcengine_internal::BoolToString(track->enabled())
                       << ", state=" << static_cast<int>(track->state());
    }
  }

  const auto transceivers = peer_connection_->GetTransceivers();
  RTC_LOG(LS_INFO) << "  transceivers=" << transceivers.size();
  for (size_t i = 0; i < transceivers.size(); ++i) {
    const auto& transceiver = transceivers[i];
    if (!transceiver) {
      RTC_LOG(LS_INFO) << "  transceiver[" << i << "]=<null>";
      continue;
    }

    auto receiver = transceiver->receiver();
    auto sender = transceiver->sender();
    auto receiver_track = receiver ? receiver->track() : nullptr;
    auto sender_track = sender ? sender->track() : nullptr;
    RTC_LOG(LS_INFO) << "  transceiver[" << i << "] mid="
                     << (transceiver->mid() ? *transceiver->mid()
                                            : std::string("<unset>"))
                     << ", receiver_id="
                     << (receiver ? receiver->id() : std::string("<none>"))
                     << ", receiver_track_kind="
                     << (receiver_track ? receiver_track->kind()
                                        : std::string("<none>"))
                     << ", receiver_track_id="
                     << (receiver_track ? receiver_track->id()
                                        : std::string("<none>"))
                     << ", sender_track_kind="
                     << (sender_track ? sender_track->kind()
                                      : std::string("<none>"))
                     << ", sender_track_id="
                     << (sender_track ? sender_track->id()
                                      : std::string("<none>"));
  }
}

void WebRTCEngine::PublishRemoteVideoTrack(webrtc::VideoTrackInterface* track,
                                           const char* reason) {
  if (!track) {
    return;
  }

  if (remote_video_track_.get() == track) {
    RTC_LOG(LS_INFO) << "Remote video track already published via " << reason
                     << ": " << track->id();
    return;
  }

  remote_video_track_ = track;
  RTC_LOG(LS_INFO) << "Publishing remote video track via " << reason << ": "
                   << track->id();
  if (observer_) {
    observer_->OnRemoteVideoTrackAdded(track);
  }
}

void WebRTCEngine::PublishRemoteTracks(const char* reason) {
  if (!peer_connection_) {
    return;
  }

  bool found_remote_video = false;
  for (const auto& receiver : peer_connection_->GetReceivers()) {
    if (!receiver) {
      continue;
    }

    auto track = receiver->track();
    if (!track) {
      continue;
    }

    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
      auto* video_track = static_cast<webrtc::VideoTrackInterface*>(track.get());
      found_remote_video = true;
      PublishRemoteVideoTrack(video_track, reason);
      break;
    }
  }

  if (!found_remote_video && remote_video_track_) {
    RTC_LOG(LS_INFO) << "No remote video receiver found via " << reason
                     << ", clearing published remote track";
    remote_video_track_ = nullptr;
    if (observer_) {
      observer_->OnRemoteVideoTrackRemoved();
    }
  }
}

bool WebRTCEngine::SetLocalVideoSource(const LocalVideoSourceConfig& config,
                                       std::string* error_message) {
  if (!ValidateLocalVideoSourceConfig(config, error_message)) {
    return false;
  }

  if (!peer_connection_ || !peer_connection_factory_ || !video_sender_) {
    local_media_pipeline_->SetConfig(config, false);
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

  local_media_pipeline_->ConfigureAudioInputForSource(config,
                                                      audio_input_switcher_);

  auto previous_source = video_source_;
  auto previous_track = local_video_track_;
  video_source_ = new_source;
  local_video_track_ = new_track;
  local_media_pipeline_->SetConfig(config, true);

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
  return local_media_pipeline_->GetState();
}

void WebRTCEngine::OnPeerConnectionTrack(
    webrtc::RtpTransceiverInterface* transceiver) {
  if (!transceiver) {
    RTC_LOG(LS_WARNING) << "OnTrack fired without a transceiver";
    return;
  }

  auto receiver = transceiver->receiver();
  if (!receiver) {
    RTC_LOG(LS_WARNING) << "OnTrack fired without a receiver";
    return;
  }

  auto track = receiver->track();
  if (!track) {
    RTC_LOG(LS_WARNING) << "OnTrack fired without a media track";
    return;
  }

  RTC_LOG(LS_INFO) << "OnTrack fired for kind=" << track->kind()
                   << ", id=" << track->id();
  LogRemoteMediaState("on-track");
  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    PublishRemoteVideoTrack(static_cast<webrtc::VideoTrackInterface*>(track.get()),
                            "on-track");
  } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
    telemetry_->SetRemoteAudioTrackAttached(true);
    RTC_LOG(LS_INFO) << "Remote audio track attached via on-track: "
                     << track->id();
  }
}

void WebRTCEngine::OnPeerConnectionAddTrack(
    webrtc::RtpReceiverInterface* receiver) {
  RTC_LOG(LS_INFO) << "Track added: " << receiver->id();
  auto track_ref = receiver->track();
  if (!track_ref) {
    RTC_LOG(LS_WARNING) << "Track added without a media track";
    return;
  }
  auto* track = track_ref.get();
  LogRemoteMediaState("on-add-track");

  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    PublishRemoteVideoTrack(static_cast<webrtc::VideoTrackInterface*>(track),
                            "on-add-track");
  } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
    telemetry_->SetRemoteAudioTrackAttached(true);
    RTC_LOG(LS_INFO) << "Remote audio track added";
  }
}

void WebRTCEngine::OnPeerConnectionRemoveTrack(
    webrtc::RtpReceiverInterface* receiver) {
  RTC_LOG(LS_INFO) << "Track removed: " << receiver->id();
  auto track_ref = receiver->track();
  if (!track_ref) {
    RTC_LOG(LS_WARNING) << "Track removed without a media track";
    return;
  }
  auto* track = track_ref.get();
  LogRemoteMediaState("on-remove-track");

  if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    remote_video_track_ = nullptr;
    if (observer_) {
      observer_->OnRemoteVideoTrackRemoved();
    }
  } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
    telemetry_->SetRemoteAudioTrackAttached(false);
    RTC_LOG(LS_INFO) << "Remote audio track removed";
  }
}
