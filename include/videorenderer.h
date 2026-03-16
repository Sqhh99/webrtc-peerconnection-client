#ifndef EXAMPLES_PEERCONNECTION_CLIENT_VIDEORENDERER_H_
#define EXAMPLES_PEERCONNECTION_CLIENT_VIDEORENDERER_H_

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

  void SetVideoTrack(webrtc::VideoTrackInterface* track_to_render);
  void Stop();
  void Clear();
  std::optional<Frame> ConsumeLatestFrame();

  // webrtc::VideoSinkInterface implementation
  void OnFrame(const webrtc::VideoFrame& frame) override;

 private:
  std::mutex mutex_;
  Frame latest_frame_;
  bool frame_dirty_ = false;
  uint64_t next_frame_id_ = 1;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> rendered_track_;
};

#endif  // EXAMPLES_PEERCONNECTION_CLIENT_VIDEORENDERER_H_
