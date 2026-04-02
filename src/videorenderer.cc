#include "videorenderer.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"

VideoRenderer::VideoRenderer() = default;

VideoRenderer::~VideoRenderer() {
  Stop();
}

void VideoRenderer::ConfigureDisplayOutput(
    int max_width,
    int max_height,
    std::chrono::milliseconds min_frame_interval) {
  std::lock_guard<std::mutex> lock(mutex_);
  max_width_ = std::max(0, max_width);
  max_height_ = std::max(0, max_height);
  min_frame_interval_ = std::max(std::chrono::milliseconds::zero(), min_frame_interval);
  last_frame_time_ = std::chrono::steady_clock::time_point{};
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

void VideoRenderer::SetFrameAvailableCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  frame_available_callback_ = std::move(callback);
}

void VideoRenderer::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (rendered_track_) {
    rendered_track_->RemoveSink(this);
    rendered_track_ = nullptr;
  }

  latest_frame_ = Frame{};
  standby_frame_ = Frame{};
  latest_frame_.frame_id = next_frame_id_++;
  frame_dirty_ = true;
  last_frame_time_ = std::chrono::steady_clock::time_point{};
}

void VideoRenderer::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  latest_frame_ = Frame{};
  standby_frame_ = Frame{};
  latest_frame_.frame_id = next_frame_id_++;
  frame_dirty_ = true;
  last_frame_time_ = std::chrono::steady_clock::time_point{};
}

std::optional<VideoRenderer::Frame> VideoRenderer::ConsumeLatestFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!frame_dirty_) {
    return std::nullopt;
  }
  frame_dirty_ = false;
  return std::exchange(latest_frame_, Frame{});
}

void VideoRenderer::OnFrame(const webrtc::VideoFrame& video_frame) {
  const auto now = std::chrono::steady_clock::now();
  Frame frame;
  int max_width = 0;
  int max_height = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (min_frame_interval_ > std::chrono::milliseconds::zero() &&
        last_frame_time_ != std::chrono::steady_clock::time_point{} &&
        (now - last_frame_time_) < min_frame_interval_) {
      return;
    }
    last_frame_time_ = now;
    max_width = max_width_;
    max_height = max_height_;
    frame = std::move(standby_frame_);
    standby_frame_ = Frame{};
  }

  webrtc::scoped_refptr<webrtc::I420BufferInterface> buffer(
      video_frame.video_frame_buffer()->ToI420());
  if (video_frame.rotation() != webrtc::kVideoRotation_0) {
    buffer = webrtc::I420Buffer::Rotate(*buffer, video_frame.rotation());
  }

  int target_width = buffer->width();
  int target_height = buffer->height();

  if (max_width > 0 && max_height > 0 &&
      (target_width > max_width || target_height > max_height)) {
    const double width_scale = static_cast<double>(max_width) / target_width;
    const double height_scale = static_cast<double>(max_height) / target_height;
    const double scale = std::min(width_scale, height_scale);

    target_width = std::max(2, static_cast<int>(target_width * scale));
    target_height = std::max(2, static_cast<int>(target_height * scale));
    target_width &= ~1;
    target_height &= ~1;

    auto scaled_buffer = webrtc::I420Buffer::Create(target_width, target_height);
    libyuv::I420Scale(buffer->DataY(), buffer->StrideY(), buffer->DataU(),
                      buffer->StrideU(), buffer->DataV(), buffer->StrideV(),
                      buffer->width(), buffer->height(), scaled_buffer->MutableDataY(),
                      scaled_buffer->StrideY(), scaled_buffer->MutableDataU(),
                      scaled_buffer->StrideU(), scaled_buffer->MutableDataV(),
                      scaled_buffer->StrideV(), target_width, target_height,
                      libyuv::kFilterBilinear);
    buffer = std::move(scaled_buffer);
  }

  frame.width = buffer->width();
  frame.height = buffer->height();
  frame.pixels.resize(static_cast<size_t>(frame.width * frame.height * 4));

  libyuv::I420ToABGR(buffer->DataY(), buffer->StrideY(), buffer->DataU(),
                     buffer->StrideU(), buffer->DataV(), buffer->StrideV(),
                     frame.pixels.data(), frame.width * 4, frame.width,
                     frame.height);

  std::function<void()> frame_available_callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (frame_dirty_) {
      standby_frame_ = std::move(latest_frame_);
    }
    frame.frame_id = next_frame_id_++;
    latest_frame_ = std::move(frame);
    frame_dirty_ = true;
    frame_available_callback = frame_available_callback_;
  }

  if (frame_available_callback) {
    frame_available_callback();
  }
}
