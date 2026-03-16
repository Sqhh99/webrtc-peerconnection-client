#include "videorenderer.h"

#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"

VideoRenderer::VideoRenderer() = default;

VideoRenderer::~VideoRenderer() {
  Stop();
}

void VideoRenderer::SetVideoTrack(webrtc::VideoTrackInterface* track_to_render) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (rendered_track_) {
    rendered_track_->RemoveSink(this);
  }

  rendered_track_ = track_to_render;
  if (rendered_track_) {
    rendered_track_->AddOrUpdateSink(this, webrtc::VideoSinkWants());
  }
}

void VideoRenderer::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (rendered_track_) {
    rendered_track_->RemoveSink(this);
    rendered_track_ = nullptr;
  }

  latest_frame_ = Frame{};
  latest_frame_.frame_id = next_frame_id_++;
  frame_dirty_ = true;
}

void VideoRenderer::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  latest_frame_ = Frame{};
  latest_frame_.frame_id = next_frame_id_++;
  frame_dirty_ = true;
}

std::optional<VideoRenderer::Frame> VideoRenderer::ConsumeLatestFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!frame_dirty_) {
    return std::nullopt;
  }
  frame_dirty_ = false;
  return latest_frame_;
}

void VideoRenderer::OnFrame(const webrtc::VideoFrame& video_frame) {
  webrtc::scoped_refptr<webrtc::I420BufferInterface> buffer(
      video_frame.video_frame_buffer()->ToI420());
  if (video_frame.rotation() != webrtc::kVideoRotation_0) {
    buffer = webrtc::I420Buffer::Rotate(*buffer, video_frame.rotation());
  }

  Frame frame;
  frame.width = buffer->width();
  frame.height = buffer->height();
  frame.pixels.resize(static_cast<size_t>(frame.width * frame.height * 4));

  libyuv::I420ToARGB(buffer->DataY(), buffer->StrideY(), buffer->DataU(),
                     buffer->StrideU(), buffer->DataV(), buffer->StrideV(),
                     frame.pixels.data(), frame.width * 4, frame.width,
                     frame.height);

  std::lock_guard<std::mutex> lock(mutex_);
  frame.frame_id = next_frame_id_++;
  latest_frame_ = std::move(frame);
  frame_dirty_ = true;
}
