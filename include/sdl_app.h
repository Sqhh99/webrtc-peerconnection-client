#ifndef SDL_APP_H_GUARD
#define SDL_APP_H_GUARD

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "app_config.h"
#include "icall_observer.h"
#include "videorenderer.h"

class SdlApp : public ICallUIObserver {
 public:
  SdlApp(ICallController* controller, AppConfig config);
  ~SdlApp() override;

  bool Initialize();
  int Run();

  void OnStartLocalRenderer(webrtc::VideoTrackInterface* track) override;
  void OnStopLocalRenderer() override;
  void OnStartRemoteRenderer(webrtc::VideoTrackInterface* track) override;
  void OnStopRemoteRenderer() override;
  void OnLogMessage(const std::string& message, const std::string& level) override;
  void OnShowError(const std::string& title, const std::string& message) override;
  void OnShowInfo(const std::string& title, const std::string& message) override;
  void OnSignalConnected(const std::string& client_id) override;
  void OnSignalDisconnected() override;
  void OnSignalError(const std::string& error) override;
  void OnClientListUpdate(const std::vector<ClientInfo>& clients) override;
  void OnCallStateChanged(CallState state, const std::string& peer_id) override;
  void OnIncomingCall(const std::string& caller_id) override;

 private:
  struct LogEntry {
    std::string timestamp;
    std::string level;
    std::string message;
  };

  struct DialogMessage {
    std::string title;
    std::string message;
    bool error = false;
  };

  struct UiSnapshot {
    bool connected = false;
    bool logs_drawer_open = false;
    std::string client_id;
    std::vector<ClientInfo> clients;
    std::string current_peer_id;
    std::optional<std::string> selected_peer_id;
    std::optional<std::string> incoming_caller_id;
    std::deque<LogEntry> logs;
    std::optional<DialogMessage> dialog;
    CallState call_state = CallState::Idle;
  };

  struct VideoTexture {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_view;
    int width = 0;
    int height = 0;
  };

  bool InitializeSdl();
  bool InitializeD3d();
  bool CreateDeviceD3D(HWND hwnd);
  void CleanupD3D();
  void CreateRenderTarget();
  void CleanupRenderTarget();
  void ResizeSwapChain(int width, int height);
  void Shutdown();
  void HandleSdlEvent(const SDL_Event& event, bool* should_quit);
  void RenderFrame();
  void RenderTopBar(UiSnapshot* snapshot);
  void RenderSidebar(UiSnapshot* snapshot);
  void RenderVideoArea(const UiSnapshot& snapshot);
  void RenderStatsPanel();
  void RenderLogDrawer(UiSnapshot* snapshot,
                       const ImVec2& content_pos,
                       const ImVec2& content_size);
  void RenderDialogs(UiSnapshot* snapshot);
  void UpdateVideoTexture(VideoRenderer* renderer, VideoTexture* texture);
  std::string GetNowString() const;
  std::string GetCallStateLabel(CallState state) const;
  ImVec2 CalculateFitSize(ImVec2 max_size, int width, int height) const;
  UiSnapshot SnapshotState();
  void PushLog(const std::string& level, const std::string& message);

  ICallController* controller_;
  AppConfig config_;

  SDL_Window* window_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_device_context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> main_render_target_view_;

  VideoRenderer local_renderer_;
  VideoRenderer remote_renderer_;
  VideoTexture local_texture_;
  VideoTexture remote_texture_;

  mutable std::mutex state_mutex_;
  UiSnapshot state_;
};

#endif  // SDL_APP_H_GUARD
