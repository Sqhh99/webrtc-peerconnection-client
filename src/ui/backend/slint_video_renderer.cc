#include "ui/slint_video_renderer.h"

#include <cstring>
#include <utility>

bool SlintVideoRenderer::Initialize(const slint::ComponentHandle<AppWindow>& ui) {
  if (initialized_) {
    return true;
  }

  ui_ = ui;
  initialized_ = true;
  return true;
}

void SlintVideoRenderer::AttachLocalRenderer(VideoRenderer* renderer) {
  local_.renderer.store(renderer, std::memory_order_release);
  local_.pending_clear.store(false, std::memory_order_release);
  if (renderer) {
    renderer->SetFrameAvailableCallback([this]() { RequestRedraw(); });
  }
  RequestRedraw();
}

void SlintVideoRenderer::DetachLocalRenderer() {
  if (VideoRenderer* renderer = local_.renderer.load(std::memory_order_acquire)) {
    renderer->SetFrameAvailableCallback({});
  }
  local_.renderer.store(nullptr, std::memory_order_release);
  local_.pending_clear.store(true, std::memory_order_release);
  RequestRedraw();
}

void SlintVideoRenderer::AttachRemoteRenderer(VideoRenderer* renderer) {
  remote_.renderer.store(renderer, std::memory_order_release);
  remote_.pending_clear.store(false, std::memory_order_release);
  if (renderer) {
    renderer->SetFrameAvailableCallback([this]() { RequestRedraw(); });
  }
  RequestRedraw();
}

void SlintVideoRenderer::DetachRemoteRenderer() {
  if (VideoRenderer* renderer = remote_.renderer.load(std::memory_order_acquire)) {
    renderer->SetFrameAvailableCallback({});
  }
  remote_.renderer.store(nullptr, std::memory_order_release);
  remote_.pending_clear.store(true, std::memory_order_release);
  RequestRedraw();
}

void SlintVideoRenderer::RequestRedraw() {
  if (redraw_pending_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  const auto weak_ui = ui_;
  slint::invoke_from_event_loop([this, weak_ui]() {
    redraw_pending_.store(false, std::memory_order_release);
    if (!weak_ui.lock()) {
      return;
    }
    FlushPendingFrames();
  });
}

void SlintVideoRenderer::FlushPendingFrames() {
  if (auto ui = ui_.lock()) {
    SyncRemoteTexture(**ui);
    SyncLocalTexture(**ui);
  }
}

void SlintVideoRenderer::SyncRemoteTexture(AppWindow& ui) {
  SyncTexture(remote_, ui, &AppWindow::set_remote_video_image);
}

void SlintVideoRenderer::SyncLocalTexture(AppWindow& ui) {
  SyncTexture(local_, ui, &AppWindow::set_local_video_image);
}

void SlintVideoRenderer::SyncTexture(
    TextureSlot& slot,
    AppWindow& ui,
    void (AppWindow::*setter)(const slint::Image&) const) {
  if (slot.pending_clear.exchange(false, std::memory_order_acq_rel)) {
    ClearTexture(slot, ui, setter);
  }

  VideoRenderer* renderer = slot.renderer.load(std::memory_order_acquire);
  if (!renderer) {
    return;
  }

  const std::optional<VideoRenderer::Frame> frame = renderer->ConsumeLatestFrame();
  if (!frame) {
    return;
  }

  if (frame->width <= 0 || frame->height <= 0 || frame->pixels.empty()) {
    ClearTexture(slot, ui, setter);
    return;
  }

  if (!EnsureBuffers(slot, frame->width, frame->height)) {
    return;
  }

  const size_t pixel_count =
      static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height);
  auto* destination = slot.back_buffer.begin();
  std::memcpy(destination, frame->pixels.data(),
              pixel_count * sizeof(slint::Rgba8Pixel));

  slot.frame_id = frame->frame_id;
  slot.image = slint::Image(slot.back_buffer);
  (ui.*setter)(slot.image);
  std::swap(slot.front_buffer, slot.back_buffer);
}

void SlintVideoRenderer::ClearTexture(
    TextureSlot& slot,
    AppWindow& ui,
    void (AppWindow::*setter)(const slint::Image&) const) {
  DestroyTexture(slot);
  (ui.*setter)(slint::Image());
}

bool SlintVideoRenderer::EnsureBuffers(TextureSlot& slot, int width, int height) {
  if (slot.buffers_ready && slot.width == width && slot.height == height) {
    return true;
  }

  slot.front_buffer = slint::SharedPixelBuffer<slint::Rgba8Pixel>(
      static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  slot.back_buffer = slint::SharedPixelBuffer<slint::Rgba8Pixel>(
      static_cast<uint32_t>(width), static_cast<uint32_t>(height));
  slot.width = width;
  slot.height = height;
  slot.frame_id = 0;
  slot.buffers_ready = true;
  slot.image = slint::Image();
  return true;
}

void SlintVideoRenderer::DestroyTexture(TextureSlot& slot) {
  slot.buffers_ready = false;
  slot.width = 0;
  slot.height = 0;
  slot.frame_id = 0;
  slot.front_buffer = slint::SharedPixelBuffer<slint::Rgba8Pixel>();
  slot.back_buffer = slint::SharedPixelBuffer<slint::Rgba8Pixel>();
  slot.image = slint::Image();
}
