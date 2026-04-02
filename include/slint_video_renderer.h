#ifndef SLINT_VIDEO_RENDERER_H_GUARD
#define SLINT_VIDEO_RENDERER_H_GUARD

#include <atomic>
#include <cstdint>
#include <optional>

#include "AppWindow.h"
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
  void RequestRedraw() const;

 private:
  struct TextureSlot {
    std::atomic<VideoRenderer*> renderer{nullptr};
    std::atomic_bool pending_clear{false};
    uint32_t texture_id = 0;
    int width = 0;
    int height = 0;
    uint64_t frame_id = 0;
    slint::Image image;
  };

  void OnRenderingNotification(slint::RenderingState state,
                               slint::GraphicsAPI graphics_api);
  void SyncRemoteTexture(AppWindow& ui);
  void SyncLocalTexture(AppWindow& ui);
  void SyncTexture(TextureSlot& slot,
                   AppWindow& ui,
                   void (AppWindow::*setter)(const slint::Image&) const);
  void ClearTexture(TextureSlot& slot,
                    AppWindow& ui,
                    void (AppWindow::*setter)(const slint::Image&) const);
  void EnsureTexture(TextureSlot& slot,
                     AppWindow& ui,
                     int width,
                     int height,
                     void (AppWindow::*setter)(const slint::Image&) const);
  void DestroyTexture(TextureSlot& slot);

  slint::ComponentWeakHandle<AppWindow> ui_;
  TextureSlot remote_;
  TextureSlot local_;
  mutable std::atomic_bool redraw_pending_{false};
  bool initialized_ = false;
  bool opengl_available_ = false;
  bool logged_non_opengl_api_ = false;
};

#endif  // SLINT_VIDEO_RENDERER_H_GUARD
