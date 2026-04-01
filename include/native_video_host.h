#ifndef NATIVE_VIDEO_HOST_H_GUARD
#define NATIVE_VIDEO_HOST_H_GUARD

#include <windows.h>

#include <cstdint>
#include <vector>

#include "videorenderer.h"

class NativeVideoHost {
 public:
  NativeVideoHost();
  ~NativeVideoHost();

  bool Create(HWND parent);
  void Destroy();

  void AttachRenderer(VideoRenderer* renderer);
  void SetBounds(const RECT& bounds);
  void SetVisible(bool visible);
  void RefreshFrame();

 private:
  struct CachedFrame {
    int width = 0;
    int height = 0;
    uint64_t frame_id = 0;
    std::vector<uint8_t> pixels;
  };

  static ATOM EnsureWindowClassRegistered();
  static LRESULT CALLBACK WindowProc(HWND hwnd,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);

  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Paint(HDC device_context);
  void ClearFrame();

  HWND window_ = nullptr;
  VideoRenderer* renderer_ = nullptr;
  CachedFrame frame_;
};

#endif  // NATIVE_VIDEO_HOST_H_GUARD
