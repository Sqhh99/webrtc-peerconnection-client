#ifndef SLINT_VIDEO_RENDERER_H_GUARD
#define SLINT_VIDEO_RENDERER_H_GUARD

#include <atomic>
#include <cstdint>
#include <optional>

#include "app-window.h"
#include <slint_image.h>
#include "videorenderer.h"

class SlintVideoRenderer {
 public:
  SlintVideoRenderer() = default;
  ~SlintVideoRenderer() = default;

  bool Initialize(const slint::ComponentHandle<AppWindow>& ui);

  void AttachLocalRenderer(VideoRenderer* renderer);
  void DetachLocalRenderer();
  void AttachRemoteRenderer(VideoRenderer* renderer);
  void DetachRemoteRenderer();
  void RequestRedraw();

 private:
  struct TextureSlot {
    std::atomic<VideoRenderer*> renderer{nullptr};
    std::atomic_bool pending_clear{false};
    bool buffers_ready = false;
    int width = 0;
    int height = 0;
    uint64_t frame_id = 0;
    slint::SharedPixelBuffer<slint::Rgba8Pixel> front_buffer;
    slint::SharedPixelBuffer<slint::Rgba8Pixel> back_buffer;
    slint::Image image;
  };

  void FlushPendingFrames();
  void SyncRemoteTexture(AppWindow& ui);
  void SyncLocalTexture(AppWindow& ui);
  void SyncTexture(TextureSlot& slot,
                   AppWindow& ui,
                   void (AppWindow::*setter)(const slint::Image&) const);
  void ClearTexture(TextureSlot& slot,
                    AppWindow& ui,
                    void (AppWindow::*setter)(const slint::Image&) const);
  bool EnsureBuffers(TextureSlot& slot, int width, int height);
  void DestroyTexture(TextureSlot& slot);

  slint::ComponentWeakHandle<AppWindow> ui_;
  TextureSlot remote_;
  TextureSlot local_;
  std::atomic_bool redraw_pending_{false};
  bool initialized_ = false;
};

#endif  // SLINT_VIDEO_RENDERER_H_GUARD
