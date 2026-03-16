#ifndef FFMPEG_FILE_SOURCE_H_GUARD
#define FFMPEG_FILE_SOURCE_H_GUARD

#include <atomic>
#include <string>
#include <thread>

#include "api/environment/environment.h"
#include "api/scoped_refptr.h"
#include "managed_video_track_source.h"
#include "media/base/video_broadcaster.h"

class SwitchableAudioInput;

class FfmpegFileSource : public ManagedVideoTrackSource {
 public:
  static bool ProbeFile(const std::string& file_path, std::string* error_message);
  static webrtc::scoped_refptr<FfmpegFileSource> Create(
      const webrtc::Environment& env,
      const std::string& file_path,
      SwitchableAudioInput* audio_input,
      std::string* error_message);

  FfmpegFileSource(const webrtc::Environment& env,
                   std::string file_path,
                   SwitchableAudioInput* audio_input);

  FfmpegFileSource(const FfmpegFileSource&) = delete;
  FfmpegFileSource& operator=(const FfmpegFileSource&) = delete;

  ~FfmpegFileSource() override;

  void Stop() override;

 protected:
  webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override;

  bool Start(std::string* error_message);
  void DecodeLoop();

  const webrtc::Environment env_;
  const std::string file_path_;
  SwitchableAudioInput* const audio_input_;
  std::atomic<bool> running_{false};
  std::thread decode_thread_;
  webrtc::VideoBroadcaster broadcaster_;
};

#endif  // FFMPEG_FILE_SOURCE_H_GUARD
