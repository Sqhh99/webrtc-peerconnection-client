#ifndef EXAMPLES_PEERCONNECTION_CLIENT_VIDEORENDERER_H_
#define EXAMPLES_PEERCONNECTION_CLIENT_VIDEORENDERER_H_

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

class VideoRenderer : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    uint64_t frame_id = 0;
  };

  VideoRenderer();
  ~VideoRenderer() override;

  void ConfigureDisplayOutput(int max_width,
                              int max_height,
                              std::chrono::milliseconds min_frame_interval);
  void SetVideoTrack(webrtc::VideoTrackInterface* track_to_render);
  void SetFrameAvailableCallback(std::function<void()> callback);
  void Stop();
  void Clear();
  std::optional<Frame> ConsumeLatestFrame();

  // webrtc::VideoSinkInterface implementation
  void OnFrame(const webrtc::VideoFrame& frame) override;

 private:
  std::mutex mutex_;
  Frame latest_frame_;
  Frame standby_frame_;
  bool frame_dirty_ = false;
  uint64_t next_frame_id_ = 1;
  int max_width_ = 0;
  int max_height_ = 0;
  std::chrono::milliseconds min_frame_interval_{0};
  std::chrono::steady_clock::time_point last_frame_time_{};
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> rendered_track_;
  std::function<void()> frame_available_callback_;
};

#endif  // EXAMPLES_PEERCONNECTION_CLIENT_VIDEORENDERER_H_
