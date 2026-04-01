#include "native_video_host.h"

#include <algorithm>
#include <cwchar>

namespace {

constexpr wchar_t kNativeVideoHostClassName[] = L"PeerConnectionNativeVideoHost";

}  // namespace

NativeVideoHost::NativeVideoHost() = default;

NativeVideoHost::~NativeVideoHost() {
  Destroy();
}

bool NativeVideoHost::Create(HWND parent) {
  if (window_) {
    return true;
  }

  if (!parent || !EnsureWindowClassRegistered()) {
    return false;
  }

  window_ = CreateWindowExW(0, kNativeVideoHostClassName, L"",
                            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, 0, 0, parent, nullptr,
                            GetModuleHandleW(nullptr), this);
  return window_ != nullptr;
}

void NativeVideoHost::Destroy() {
  if (!window_) {
    return;
  }

  if (IsWindow(window_)) {
    DestroyWindow(window_);
  }
  window_ = nullptr;
  renderer_ = nullptr;
  ClearFrame();
}

void NativeVideoHost::AttachRenderer(VideoRenderer* renderer) {
  renderer_ = renderer;
}

void NativeVideoHost::SetBounds(const RECT& bounds) {
  if (!window_) {
    return;
  }

  const int width = std::max(0L, bounds.right - bounds.left);
  const int height = std::max(0L, bounds.bottom - bounds.top);
  MoveWindow(window_, bounds.left, bounds.top, width, height, TRUE);
}

void NativeVideoHost::SetVisible(bool visible) {
  if (!window_) {
    return;
  }

  ShowWindow(window_, visible ? SW_SHOW : SW_HIDE);
}

void NativeVideoHost::RefreshFrame() {
  if (!window_) {
    return;
  }

  if (!renderer_) {
    ClearFrame();
    InvalidateRect(window_, nullptr, TRUE);
    return;
  }

  const auto latest_frame = renderer_->ConsumeLatestFrame();
  if (!latest_frame) {
    return;
  }

  if (latest_frame->width <= 0 || latest_frame->height <= 0 ||
      latest_frame->pixels.empty()) {
    ClearFrame();
  } else {
    frame_.width = latest_frame->width;
    frame_.height = latest_frame->height;
    frame_.frame_id = latest_frame->frame_id;
    frame_.pixels = latest_frame->pixels;
  }

  InvalidateRect(window_, nullptr, FALSE);
}

ATOM NativeVideoHost::EnsureWindowClassRegistered() {
  static const ATOM atom = []() -> ATOM {
    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = &NativeVideoHost::WindowProc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = kNativeVideoHostClassName;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    return RegisterClassW(&window_class);
  }();

  return atom;
}

LRESULT CALLBACK NativeVideoHost::WindowProc(HWND hwnd,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam) {
  NativeVideoHost* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<LPCREATESTRUCTW>(lparam);
    self = static_cast<NativeVideoHost*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<NativeVideoHost*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (!self) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  return self->HandleMessage(message, wparam, lparam);
}

LRESULT NativeVideoHost::HandleMessage(UINT message,
                                       WPARAM wparam,
                                       LPARAM lparam) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC device_context = BeginPaint(window_, &paint);
      Paint(device_context);
      EndPaint(window_, &paint);
      return 0;
    }
    default:
      break;
  }

  return DefWindowProcW(window_, message, wparam, lparam);
}

void NativeVideoHost::Paint(HDC device_context) {
  RECT client_rect = {};
  GetClientRect(window_, &client_rect);
  FillRect(device_context, &client_rect,
           reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

  if (frame_.width <= 0 || frame_.height <= 0 || frame_.pixels.empty()) {
    SetBkMode(device_context, TRANSPARENT);
    SetTextColor(device_context, RGB(160, 166, 174));
    DrawTextW(device_context, L"Waiting for video...", -1, &client_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  const double scale_x = static_cast<double>(client_rect.right) /
                         static_cast<double>(frame_.width);
  const double scale_y = static_cast<double>(client_rect.bottom) /
                         static_cast<double>(frame_.height);
  const double scale = std::min(scale_x, scale_y);

  const int target_width = std::max(1, static_cast<int>(frame_.width * scale));
  const int target_height = std::max(1, static_cast<int>(frame_.height * scale));
  const int target_x = (client_rect.right - target_width) / 2;
  const int target_y = (client_rect.bottom - target_height) / 2;

  BITMAPINFO bitmap_info = {};
  bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
  bitmap_info.bmiHeader.biWidth = frame_.width;
  bitmap_info.bmiHeader.biHeight = -frame_.height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  StretchDIBits(device_context, target_x, target_y, target_width, target_height,
                0, 0, frame_.width, frame_.height, frame_.pixels.data(),
                &bitmap_info, DIB_RGB_COLORS, SRCCOPY);
}

void NativeVideoHost::ClearFrame() {
  frame_ = CachedFrame{};
}
