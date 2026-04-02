#include "slint_video_renderer.h"

#include <windows.h>
#include <GL/gl.h>

#include <iostream>
#include <utility>

namespace {

constexpr GLint kLinearFilter = 0x2601;
constexpr GLint kClampToEdge = 0x812F;

}  // namespace

bool SlintVideoRenderer::Initialize(const slint::ComponentHandle<AppWindow>& ui) {
  if (initialized_) {
    return true;
  }

  ui_ = ui;
  if (auto error = ui->window().set_rendering_notifier(
          [this](slint::RenderingState state, slint::GraphicsAPI graphics_api) {
            OnRenderingNotification(state, graphics_api);
          })) {
    return false;
  }

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

void SlintVideoRenderer::RequestRedraw() const {
  if (redraw_pending_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  const auto weak_ui = ui_;
  slint::invoke_from_event_loop([this, weak_ui]() {
    redraw_pending_.store(false, std::memory_order_release);
    if (auto ui = weak_ui.lock()) {
      (*ui)->window().request_redraw();
    }
  });
}

void SlintVideoRenderer::OnRenderingNotification(slint::RenderingState state,
                                                 slint::GraphicsAPI graphics_api) {
  switch (state) {
    case slint::RenderingState::RenderingSetup:
      opengl_available_ = graphics_api == slint::GraphicsAPI::NativeOpenGL;
      if (!opengl_available_ && !logged_non_opengl_api_) {
        logged_non_opengl_api_ = true;
        std::cerr << "Slint video renderer: current renderer is not NativeOpenGL; "
                     "borrowed GL textures will stay disabled.\n";
      }
      break;
    case slint::RenderingState::BeforeRendering:
      if (!opengl_available_) {
        return;
      }
      if (auto ui = ui_.lock()) {
        SyncRemoteTexture(**ui);
        SyncLocalTexture(**ui);
      }
      break;
    case slint::RenderingState::AfterRendering:
      break;
    case slint::RenderingState::RenderingTeardown:
      DestroyTexture(remote_);
      DestroyTexture(local_);
      opengl_available_ = false;
      break;
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

  EnsureTexture(slot, ui, frame->width, frame->height, setter);

  glBindTexture(GL_TEXTURE_2D, slot.texture_id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RGBA,
                  GL_UNSIGNED_BYTE, frame->pixels.data());
  glBindTexture(GL_TEXTURE_2D, 0);

  slot.frame_id = frame->frame_id;
}

void SlintVideoRenderer::ClearTexture(
    TextureSlot& slot,
    AppWindow& ui,
    void (AppWindow::*setter)(const slint::Image&) const) {
  DestroyTexture(slot);
  (ui.*setter)(slint::Image());
}

void SlintVideoRenderer::EnsureTexture(
    TextureSlot& slot,
    AppWindow& ui,
    int width,
    int height,
    void (AppWindow::*setter)(const slint::Image&) const) {
  if (slot.texture_id != 0 && slot.width == width && slot.height == height) {
    return;
  }

  DestroyTexture(slot);

  GLuint texture_id = 0;
  glGenTextures(1, &texture_id);
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, kLinearFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, kLinearFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);

  slot.texture_id = texture_id;
  slot.width = width;
  slot.height = height;
  slot.frame_id = 0;
  slot.image = slint::Image::create_from_borrowed_gl_2d_rgba_texture(
      slot.texture_id,
      {static_cast<uint32_t>(slot.width), static_cast<uint32_t>(slot.height)});
  (ui.*setter)(slot.image);
}

void SlintVideoRenderer::DestroyTexture(TextureSlot& slot) {
  if (slot.texture_id != 0) {
    GLuint texture_id = slot.texture_id;
    glDeleteTextures(1, &texture_id);
  }
  slot.texture_id = 0;
  slot.width = 0;
  slot.height = 0;
  slot.frame_id = 0;
  slot.image = slint::Image();
}
