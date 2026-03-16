#include "sdl_app.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

#include <SDL3/SDL_main.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl3.h>

namespace {

constexpr ImGuiWindowFlags kMainWindowFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

}  // namespace

SdlApp::SdlApp(ICallController* controller, AppConfig config)
    : controller_(controller), config_(std::move(config)) {
  PushLog("info", "Application started");
}

SdlApp::~SdlApp() {
  Shutdown();
}

bool SdlApp::Initialize() {
  if (!InitializeSdl() || !InitializeD3d()) {
    Shutdown();
    return false;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  if (!ImGui_ImplSDL3_InitForD3D(window_)) {
    return false;
  }
  if (!ImGui_ImplDX11_Init(d3d_device_.Get(), d3d_device_context_.Get())) {
    return false;
  }

  return true;
}

int SdlApp::Run() {
  bool should_quit = false;

  while (!should_quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      HandleSdlEvent(event, &should_quit);
    }

    MSG message;
    while (::PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&message);
      ::DispatchMessage(&message);
    }

    RenderFrame();
  }

  return 0;
}

void SdlApp::OnStartLocalRenderer(webrtc::VideoTrackInterface* track) {
  local_renderer_.SetVideoTrack(track);
}

void SdlApp::OnStopLocalRenderer() {
  local_renderer_.Stop();
}

void SdlApp::OnStartRemoteRenderer(webrtc::VideoTrackInterface* track) {
  remote_renderer_.SetVideoTrack(track);
}

void SdlApp::OnStopRemoteRenderer() {
  remote_renderer_.Stop();
}

void SdlApp::OnLogMessage(const std::string& message, const std::string& level) {
  PushLog(level, message);
}

void SdlApp::OnShowError(const std::string& title, const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.dialog = DialogMessage{title, message, true};
  }
  PushLog("error", title + ": " + message);
}

void SdlApp::OnShowInfo(const std::string& title, const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.dialog = DialogMessage{title, message, false};
  }
  PushLog("info", title + ": " + message);
}

void SdlApp::OnSignalConnected(const std::string& client_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.connected = true;
  state_.client_id = client_id;
}

void SdlApp::OnSignalDisconnected() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.connected = false;
  state_.clients.clear();
}

void SdlApp::OnSignalError(const std::string& error) {
  PushLog("error", error);
}

void SdlApp::OnClientListUpdate(const std::vector<ClientInfo>& clients) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.clients = clients;
  if (state_.selected_peer_id) {
    const auto selected_exists = std::any_of(
        state_.clients.begin(), state_.clients.end(),
        [this](const ClientInfo& client) {
          return state_.selected_peer_id && client.id == *state_.selected_peer_id;
        });
    if (!selected_exists) {
      state_.selected_peer_id.reset();
    }
  }
}

void SdlApp::OnCallStateChanged(CallState state, const std::string& peer_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.call_state = state;
  state_.current_peer_id = peer_id;
  if (state == CallState::Idle) {
    state_.incoming_caller_id.reset();
  }
}

void SdlApp::OnIncomingCall(const std::string& caller_id) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.incoming_caller_id = caller_id;
}

bool SdlApp::InitializeSdl() {
  SDL_SetMainReady();
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    return false;
  }

  window_ = SDL_CreateWindow("PeerConnection Client", 1440, 900,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  return window_ != nullptr;
}

bool SdlApp::InitializeD3d() {
  SDL_PropertiesID window_properties = SDL_GetWindowProperties(window_);
  HWND hwnd = reinterpret_cast<HWND>(
      SDL_GetPointerProperty(window_properties,
                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
  if (!hwnd || !CreateDeviceD3D(hwnd)) {
    return false;
  }
  CreateRenderTarget();
  return true;
}

bool SdlApp::CreateDeviceD3D(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC swap_chain_description = {};
  swap_chain_description.BufferCount = 2;
  swap_chain_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_description.OutputWindow = hwnd;
  swap_chain_description.SampleDesc.Count = 1;
  swap_chain_description.Windowed = TRUE;
  swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  constexpr D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL created_feature_level = D3D_FEATURE_LEVEL_11_0;

  const HRESULT result = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, feature_levels,
      static_cast<UINT>(std::size(feature_levels)), D3D11_SDK_VERSION,
      &swap_chain_description, &swap_chain_, &d3d_device_,
      &created_feature_level, &d3d_device_context_);
  return SUCCEEDED(result);
}

void SdlApp::CleanupD3D() {
  CleanupRenderTarget();
  swap_chain_.Reset();
  d3d_device_context_.Reset();
  d3d_device_.Reset();
}

void SdlApp::CreateRenderTarget() {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer;
  if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
    return;
  }
  d3d_device_->CreateRenderTargetView(back_buffer.Get(), nullptr,
                                      &main_render_target_view_);
}

void SdlApp::CleanupRenderTarget() {
  main_render_target_view_.Reset();
}

void SdlApp::ResizeSwapChain(int width, int height) {
  if (!swap_chain_ || width <= 0 || height <= 0) {
    return;
  }

  CleanupRenderTarget();
  swap_chain_->ResizeBuffers(0, static_cast<UINT>(width),
                             static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
  CreateRenderTarget();
}

void SdlApp::Shutdown() {
  local_renderer_.Stop();
  remote_renderer_.Stop();

  if (ImGui::GetCurrentContext()) {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }

  CleanupD3D();

  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

void SdlApp::HandleSdlEvent(const SDL_Event& event, bool* should_quit) {
  if (event.type == SDL_EVENT_QUIT) {
    *should_quit = true;
    return;
  }

  if (event.type == SDL_EVENT_WINDOW_RESIZED &&
      event.window.windowID == SDL_GetWindowID(window_)) {
    ResizeSwapChain(event.window.data1, event.window.data2);
  }
}

void SdlApp::RenderFrame() {
  UpdateVideoTexture(&remote_renderer_, &remote_texture_);
  UpdateVideoTexture(&local_renderer_, &local_texture_);

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  UiSnapshot snapshot = SnapshotState();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  ImGui::Begin("PeerConnection", nullptr, kMainWindowFlags);
  RenderTopBar(&snapshot);
  ImGui::Separator();
  const ImVec2 content_pos = ImGui::GetCursorScreenPos();
  const ImVec2 content_size = ImGui::GetContentRegionAvail();

  if (ImGui::BeginTable("main_layout", 2,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("sidebar", ImGuiTableColumnFlags_WidthFixed, 320.0f);
    ImGui::TableSetupColumn("content", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextColumn();
    RenderSidebar(&snapshot);

    ImGui::TableNextColumn();
    RenderVideoArea(snapshot);

    ImGui::EndTable();
  }

  RenderLogDrawer(&snapshot, content_pos, content_size);
  RenderDialogs(&snapshot);
  ImGui::End();

  ImGui::Render();
  const float clear_color[4] = {0.08f, 0.09f, 0.10f, 1.0f};
  d3d_device_context_->OMSetRenderTargets(1, main_render_target_view_.GetAddressOf(),
                                          nullptr);
  d3d_device_context_->ClearRenderTargetView(main_render_target_view_.Get(),
                                             clear_color);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  swap_chain_->Present(1, 0);
}

void SdlApp::RenderTopBar(UiSnapshot* snapshot) {
  ImGui::TextUnformatted("Native WebRTC Client");
  ImGui::SameLine();
  ImGui::TextDisabled("| %s", config_.signal_url.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("| user: %s", config_.username.c_str());
  ImGui::SameLine();
  ImGui::TextColored(snapshot->connected ? ImVec4(0.25f, 0.78f, 0.45f, 1.0f)
                                         : ImVec4(0.92f, 0.33f, 0.24f, 1.0f),
                     snapshot->connected ? "connected" : "disconnected");

  ImGui::SameLine(ImGui::GetWindowWidth() - 330.0f);
  if (ImGui::SmallButton(snapshot->logs_drawer_open ? "[<] Hide Logs"
                                                    : "[>] Show Logs")) {
    snapshot->logs_drawer_open = !snapshot->logs_drawer_open;
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.logs_drawer_open = snapshot->logs_drawer_open;
  }

  ImGui::SameLine();
  if (snapshot->connected) {
    if (ImGui::Button("Disconnect", ImVec2(100.0f, 0.0f))) {
      controller_->DisconnectFromSignalServer();
    }
  } else {
    if (ImGui::Button("Reconnect", ImVec2(100.0f, 0.0f))) {
      controller_->ConnectToSignalServer(config_.signal_url, config_.username);
    }
  }
}

void SdlApp::RenderSidebar(UiSnapshot* snapshot) {
  ImGui::BeginChild("sidebar_panel", ImVec2(0.0f, 0.0f), true);
  ImGui::TextUnformatted("Online Users");
  ImGui::Spacing();

  for (const ClientInfo& client : snapshot->clients) {
    if (client.id == snapshot->client_id) {
      continue;
    }

    const bool selected =
        snapshot->selected_peer_id && *snapshot->selected_peer_id == client.id;
    if (ImGui::Selectable(client.id.c_str(), selected)) {
      snapshot->selected_peer_id = client.id;
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.selected_peer_id = client.id;
    }
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Call Controls");
  ImGui::Text("State: %s", GetCallStateLabel(snapshot->call_state).c_str());
  if (!snapshot->current_peer_id.empty()) {
    ImGui::Text("Peer: %s", snapshot->current_peer_id.c_str());
  }

  const bool can_start_call = snapshot->connected &&
                              snapshot->call_state == CallState::Idle &&
                              snapshot->selected_peer_id.has_value();
  ImGui::BeginDisabled(!can_start_call);
  if (ImGui::Button("Start Call", ImVec2(-1.0f, 0.0f)) &&
      snapshot->selected_peer_id) {
    controller_->StartCall(*snapshot->selected_peer_id);
  }
  ImGui::EndDisabled();

  const bool can_hangup = snapshot->call_state != CallState::Idle;
  ImGui::BeginDisabled(!can_hangup);
  if (ImGui::Button("Hang Up", ImVec2(-1.0f, 0.0f))) {
    controller_->EndCall();
  }
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::SeparatorText("Stats");
  RenderStatsPanel();
  ImGui::EndChild();
}

void SdlApp::RenderVideoArea(const UiSnapshot& snapshot) {
  ImGui::BeginChild("video_panel", ImVec2(0.0f, 0.0f), true);
  ImGui::Text("Call with: %s",
              snapshot.current_peer_id.empty() ? "(none)"
                                               : snapshot.current_peer_id.c_str());
  ImGui::TextDisabled("%s", GetCallStateLabel(snapshot.call_state).c_str());
  ImGui::Spacing();
  ImGui::Separator();

  ImGui::BeginChild("video_canvas", ImVec2(0.0f, 0.0f), false,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
  const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(canvas_pos,
                           ImVec2(canvas_pos.x + canvas_size.x,
                                  canvas_pos.y + canvas_size.y),
                           IM_COL32(22, 27, 34, 255), 8.0f);

  if (remote_texture_.shader_resource_view) {
    const ImVec2 remote_size =
        CalculateFitSize(canvas_size, remote_texture_.width, remote_texture_.height);
    const ImVec2 remote_pos(
        canvas_pos.x + std::max(0.0f, (canvas_size.x - remote_size.x) * 0.5f),
        canvas_pos.y + std::max(0.0f, (canvas_size.y - remote_size.y) * 0.5f));
    ImGui::SetCursorScreenPos(remote_pos);
    ImGui::Image(reinterpret_cast<ImTextureID>(
                     remote_texture_.shader_resource_view.Get()),
                 remote_size);
  } else {
    const ImVec2 text_size = ImGui::CalcTextSize("Waiting for remote video...");
    ImGui::SetCursorScreenPos(ImVec2(
        canvas_pos.x + std::max(24.0f, (canvas_size.x - text_size.x) * 0.5f),
        canvas_pos.y + std::max(24.0f, (canvas_size.y - text_size.y) * 0.5f)));
    ImGui::TextDisabled("Waiting for remote video...");
  }

  if (local_texture_.shader_resource_view) {
    const float overlay_width =
        std::clamp(canvas_size.x * 0.24f, 160.0f, 280.0f);
    const float overlay_height =
        std::clamp(canvas_size.y * 0.24f, 90.0f, 180.0f);
    const ImVec2 local_max_size(overlay_width, overlay_height);
    const ImVec2 local_size =
        CalculateFitSize(local_max_size, local_texture_.width, local_texture_.height);
    const float margin = 16.0f;
    const float frame_padding = 8.0f;
    const ImVec2 overlay_pos(
        canvas_pos.x + canvas_size.x - local_size.x - margin - frame_padding * 2.0f,
        canvas_pos.y + canvas_size.y - local_size.y - margin - frame_padding * 2.0f);
    const ImVec2 overlay_end(
        overlay_pos.x + local_size.x + frame_padding * 2.0f,
        overlay_pos.y + local_size.y + frame_padding * 2.0f + 22.0f);

    draw_list->AddRectFilled(
        overlay_pos, overlay_end, IM_COL32(8, 10, 14, 230), 10.0f);
    draw_list->AddRect(
        overlay_pos, overlay_end, IM_COL32(255, 255, 255, 70), 10.0f, 0, 2.0f);
    draw_list->AddText(ImVec2(overlay_pos.x + frame_padding,
                              overlay_pos.y + frame_padding - 1.0f),
                       IM_COL32(235, 238, 242, 255), "You");

    ImGui::SetCursorScreenPos(ImVec2(overlay_pos.x + frame_padding,
                                     overlay_pos.y + 22.0f));
    ImGui::Image(reinterpret_cast<ImTextureID>(
                     local_texture_.shader_resource_view.Get()),
                 local_size);
  }

  ImGui::SetCursorScreenPos(canvas_pos);
  ImGui::Dummy(canvas_size);
  ImGui::EndChild();

  ImGui::EndChild();
}

void SdlApp::RenderStatsPanel() {
  const RtcStatsSnapshot stats = controller_->GetLatestRtcStats();

  auto write_stat = [](const char* label, const std::string& value) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(140.0f);
    ImGui::TextDisabled("%s", value.c_str());
  };

  write_stat("ICE", stats.ice_state.empty() ? "-" : stats.ice_state);
  if (!stats.valid) {
    write_stat("Mic", stats.local_audio_track_attached ? "Track attached" : "-");
    write_stat("Speaker", stats.audio_playout_active ? "Playing" : "-");
    write_stat("Remote Audio",
               stats.remote_audio_track_attached ? "Track attached" : "-");
    write_stat("Outbound", "-");
    write_stat("Inbound", "-");
    write_stat("RTT", "-");
    write_stat("Audio TX", "-");
    write_stat("Audio RX", "-");
    write_stat("Audio Jitter", "-");
    write_stat("Audio Loss", "-");
    write_stat("Video", "-");
    return;
  }

  std::string mic_status = "Unavailable";
  if (stats.local_audio_track_attached) {
    if (stats.audio_recording_active) {
      mic_status = "Capturing";
    } else if (stats.recording_available) {
      mic_status = "Track attached";
    } else {
      mic_status = "No input device";
    }
  } else if (stats.recording_available) {
    mic_status = "Idle";
  }

  std::string speaker_status = "Unavailable";
  if (stats.audio_playout_active) {
    speaker_status = "Playing";
  } else if (stats.playout_available) {
    speaker_status = "Ready";
  }

  std::string remote_audio_status = "Waiting";
  if (stats.remote_audio_track_attached) {
    remote_audio_status = stats.audio_receiving ? "Receiving" : "Track attached";
  }

  write_stat("Mic", mic_status);
  write_stat("Speaker", speaker_status);
  write_stat("Remote Audio", remote_audio_status);
  write_stat("Outbound", std::to_string(static_cast<int>(stats.outbound_bitrate_kbps)) +
                            " kbps");
  write_stat("Inbound", std::to_string(static_cast<int>(stats.inbound_bitrate_kbps)) +
                           " kbps");
  write_stat("RTT", std::to_string(static_cast<int>(stats.current_rtt_ms)) + " ms");
  write_stat("Audio TX", stats.audio_sending ? "Sending" : "Idle");
  write_stat("Audio RX", stats.audio_receiving ? "Receiving" : "Idle");
  write_stat("Input Level",
             std::to_string(
                 static_cast<int>(stats.local_audio_level * 100.0)) +
                 "%");
  write_stat("Remote Level",
             std::to_string(
                 static_cast<int>(stats.remote_audio_level * 100.0)) +
                 "%");
  write_stat("Audio Jitter",
             std::to_string(static_cast<int>(stats.inbound_audio_jitter_ms)) +
                 " ms");
  write_stat("Audio Loss",
             std::to_string(
                 static_cast<int>(stats.inbound_audio_packet_loss_percent)) +
                 "%");
  write_stat("Video",
             std::to_string(stats.inbound_video_width) + "x" +
                 std::to_string(stats.inbound_video_height) + " @ " +
                 std::to_string(static_cast<int>(stats.inbound_video_fps)) +
                 " fps");
}

void SdlApp::RenderLogDrawer(UiSnapshot* snapshot,
                             const ImVec2& content_pos,
                             const ImVec2& content_size) {
  if (!snapshot->logs_drawer_open) {
    return;
  }

  const float drawer_width =
      std::clamp(content_size.x * 0.28f, 320.0f, 420.0f);
  const ImVec2 drawer_pos(content_pos.x + content_size.x - drawer_width,
                          content_pos.y);
  const ImVec2 drawer_size(drawer_width, content_size.y);

  ImGui::SetNextWindowPos(drawer_pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(drawer_size, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.97f);

  const ImGuiWindowFlags drawer_flags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

  if (!ImGui::Begin("Logs Drawer", nullptr, drawer_flags)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Logs");
  ImGui::SameLine(ImGui::GetWindowWidth() - 78.0f);
  if (ImGui::SmallButton("Close")) {
    snapshot->logs_drawer_open = false;
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.logs_drawer_open = false;
  }

  ImGui::Separator();

  ImGui::BeginChild("log_panel", ImVec2(0.0f, 0.0f), true);
  for (const LogEntry& entry : snapshot->logs) {
    ImVec4 color = ImVec4(0.70f, 0.72f, 0.74f, 1.0f);
    if (entry.level == "error") {
      color = ImVec4(0.92f, 0.33f, 0.24f, 1.0f);
    } else if (entry.level == "warning") {
      color = ImVec4(0.94f, 0.73f, 0.24f, 1.0f);
    } else if (entry.level == "success") {
      color = ImVec4(0.25f, 0.78f, 0.45f, 1.0f);
    }

    ImGui::TextColored(color, "[%s] %s", entry.timestamp.c_str(),
                       entry.message.c_str());
  }

  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
    ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();
  ImGui::End();
}

void SdlApp::RenderDialogs(UiSnapshot* snapshot) {
  if (snapshot->incoming_caller_id) {
    ImGui::OpenPopup("Incoming Call");
  }

  if (ImGui::BeginPopupModal("Incoming Call", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Incoming call from %s", snapshot->incoming_caller_id
                                              ? snapshot->incoming_caller_id->c_str()
                                              : "");
    if (ImGui::Button("Accept", ImVec2(120.0f, 0.0f))) {
      controller_->AcceptCall();
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.incoming_caller_id.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reject", ImVec2(120.0f, 0.0f))) {
      controller_->RejectCall("rejected");
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.incoming_caller_id.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (snapshot->dialog) {
    ImGui::OpenPopup("Message");
  }

  if (ImGui::BeginPopupModal("Message", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (snapshot->dialog) {
      ImGui::TextUnformatted(snapshot->dialog->title.c_str());
      ImGui::Separator();
      ImGui::TextWrapped("%s", snapshot->dialog->message.c_str());
    }
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.dialog.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void SdlApp::UpdateVideoTexture(VideoRenderer* renderer, VideoTexture* texture) {
  const auto frame = renderer->ConsumeLatestFrame();
  if (!frame || !d3d_device_ || !d3d_device_context_) {
    return;
  }

  if (!texture->texture || texture->width != frame->width ||
      texture->height != frame->height) {
    texture->texture.Reset();
    texture->shader_resource_view.Reset();

    D3D11_TEXTURE2D_DESC description = {};
    description.Width = static_cast<UINT>(frame->width);
    description.Height = static_cast<UINT>(frame->height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    if (FAILED(d3d_device_->CreateTexture2D(&description, nullptr,
                                            &texture->texture))) {
      return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC shader_view_description = {};
    shader_view_description.Format = description.Format;
    shader_view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shader_view_description.Texture2D.MipLevels = 1;

    if (FAILED(d3d_device_->CreateShaderResourceView(
            texture->texture.Get(), &shader_view_description,
            &texture->shader_resource_view))) {
      texture->texture.Reset();
      return;
    }

    texture->width = frame->width;
    texture->height = frame->height;
  }

  D3D11_MAPPED_SUBRESOURCE mapped_resource = {};
  if (FAILED(d3d_device_context_->Map(texture->texture.Get(), 0,
                                      D3D11_MAP_WRITE_DISCARD, 0,
                                      &mapped_resource))) {
    return;
  }

  const int src_stride = frame->width * 4;
  for (int row = 0; row < frame->height; ++row) {
    memcpy(static_cast<unsigned char*>(mapped_resource.pData) +
               row * mapped_resource.RowPitch,
           frame->pixels.data() + row * src_stride, static_cast<size_t>(src_stride));
  }

  d3d_device_context_->Unmap(texture->texture.Get(), 0);
}

std::string SdlApp::GetNowString() const {
  const auto now = std::chrono::system_clock::now();
  const std::time_t current_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time = {};
  localtime_s(&local_time, &current_time);

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%H:%M:%S");
  return stream.str();
}

std::string SdlApp::GetCallStateLabel(CallState state) const {
  switch (state) {
    case CallState::Idle:
      return "Idle";
    case CallState::Calling:
      return "Calling";
    case CallState::Receiving:
      return "Incoming";
    case CallState::Connecting:
      return "Connecting";
    case CallState::Connected:
      return "Connected";
    case CallState::Ending:
      return "Ending";
    default:
      return "Unknown";
  }
}

ImVec2 SdlApp::CalculateFitSize(ImVec2 max_size, int width, int height) const {
  if (width <= 0 || height <= 0) {
    return ImVec2(0.0f, 0.0f);
  }

  const float width_scale = max_size.x / static_cast<float>(width);
  const float height_scale = max_size.y / static_cast<float>(height);
  const float scale = std::min(width_scale, height_scale);
  return ImVec2(static_cast<float>(width) * scale,
                static_cast<float>(height) * scale);
}

SdlApp::UiSnapshot SdlApp::SnapshotState() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return state_;
}

void SdlApp::PushLog(const std::string& level, const std::string& message) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.logs.push_back(LogEntry{GetNowString(), level, message});
  while (state_.logs.size() > 200) {
    state_.logs.pop_front();
  }
}
