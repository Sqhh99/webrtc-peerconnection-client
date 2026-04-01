#include <windows.h>
#include "slint_app.h"

#include <commdlg.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "generated/ui_paths.h"

namespace {

using InterpreterValue = slint::interpreter::Value;

std::string BuildStatusLabel(bool connected) {
  return connected ? "Connected" : "Disconnected";
}

std::string BuildConnectionActionLabel(bool connected) {
  return connected ? "Disconnect" : "Reconnect";
}

std::string BuildLogsToggleLabel(bool visible) {
  return visible ? "Hide Logs" : "Show Logs";
}

slint::SharedVector<InterpreterValue> ToSlintArray(
    const std::vector<std::string>& items) {
  slint::SharedVector<InterpreterValue> result;
  for (const std::string& item : items) {
    result.push_back(slint::SharedString(item));
  }
  return result;
}

}  // namespace

SlintApp::SlintApp(ICallController* controller, AppConfig config)
    : controller_(controller), config_(std::move(config)) {
  PushLog("info", "Application started");
}

SlintApp::~SlintApp() {
  shutting_down_.store(true);
  StopTimers();
  local_renderer_.Stop();
  remote_renderer_.Stop();
}

bool SlintApp::Initialize() {
  if (!LoadUi()) {
    return false;
  }

  BindCallbacks();
  ApplyStateToUi();
  return true;
}

int SlintApp::Run() {
  if (!ui_) {
    return 1;
  }

  event_loop_running_.store(true);
  ui_.value()->show();
  ApplyStateToUi();
  StartTimers();
  slint::run_event_loop();
  StopTimers();
  event_loop_running_.store(false);
  ui_.value()->hide();
  return 0;
}

void SlintApp::OnStartLocalRenderer(webrtc::VideoTrackInterface* track) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.local_video_visible = true;
    state_.local_video_ready = false;
  }
  local_renderer_.Clear();
  local_renderer_.SetVideoTrack(track);
  PushLog("info", "Local video track attached");
  ScheduleUiRefresh();
}

void SlintApp::OnStopLocalRenderer() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.local_video_visible = false;
    state_.local_video_ready = false;
  }
  local_renderer_.Stop();
  ClearVideoImage(true);
  PushLog("info", "Local video track removed");
  ScheduleUiRefresh();
}

void SlintApp::OnStartRemoteRenderer(webrtc::VideoTrackInterface* track) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.remote_video_visible = true;
    state_.remote_video_ready = false;
  }
  remote_renderer_.Clear();
  remote_renderer_.SetVideoTrack(track);
  PushLog("info", "Remote video track attached");
  ScheduleUiRefresh();
}

void SlintApp::OnStopRemoteRenderer() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.remote_video_visible = false;
    state_.remote_video_ready = false;
  }
  remote_renderer_.Stop();
  ClearVideoImage(false);
  PushLog("info", "Remote video track removed");
  ScheduleUiRefresh();
}

void SlintApp::OnLogMessage(const std::string& message, const std::string& level) {
  PushLog(level, message);
  ScheduleUiRefresh();
}

void SlintApp::OnShowError(const std::string& title, const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.dialog = DialogMessage{title, message, true};
  }
  PushLog("error", title + ": " + message);
  ScheduleUiRefresh();
}

void SlintApp::OnShowInfo(const std::string& title, const std::string& message) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.dialog = DialogMessage{title, message, false};
  }
  PushLog("info", title + ": " + message);
  ScheduleUiRefresh();
}

void SlintApp::OnSignalConnected(const std::string& client_id) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.connected = true;
    state_.client_id = client_id;
  }
  ScheduleUiRefresh();
}

void SlintApp::OnSignalDisconnected() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.connected = false;
    state_.clients.clear();
    state_.selected_peer_id.clear();
  }
  ScheduleUiRefresh();
}

void SlintApp::OnSignalError(const std::string& error) {
  PushLog("error", error);
  ScheduleUiRefresh();
}

void SlintApp::OnClientListUpdate(const std::vector<ClientInfo>& clients) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.clients = clients;
    if (!state_.selected_peer_id.empty()) {
      const bool exists = std::any_of(
          state_.clients.begin(), state_.clients.end(),
          [this](const ClientInfo& client) {
            return client.id == state_.selected_peer_id;
          });
      if (!exists) {
        state_.selected_peer_id.clear();
      }
    }
  }
  ScheduleUiRefresh();
}

void SlintApp::OnCallStateChanged(CallState state, const std::string& peer_id) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.call_state = state;
    state_.current_peer_id = peer_id;
    if (state == CallState::Idle) {
      state_.incoming_caller_id.reset();
    }
  }
  ScheduleUiRefresh();
}

void SlintApp::OnIncomingCall(const std::string& caller_id) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.incoming_caller_id = caller_id;
  }
  ScheduleUiRefresh();
}

bool SlintApp::LoadUi() {
  const std::filesystem::path ui_path =
      std::filesystem::path(PEERCONNECTION_UI_DIR) / "AppWindow.slint";

  slint::interpreter::ComponentCompiler compiler;
  const auto definition = compiler.build_from_path(ui_path.string());
  if (!definition) {
    std::ostringstream stream;
    stream << "Failed to load Slint UI from " << ui_path.string();
    for (const auto& diagnostic : compiler.diagnostics()) {
      stream << "\n"
             << (diagnostic.level ==
                         slint::interpreter::DiagnosticLevel::Error
                     ? "error"
                     : "warning")
             << ": " << ToStdString(diagnostic.source_file) << ":"
             << diagnostic.line << ":" << diagnostic.column << " - "
             << ToStdString(diagnostic.message);
    }
    OutputDebugStringA(stream.str().c_str());
    return false;
  }

  component_definition_ = *definition;
  ui_ = component_definition_->create();
  return true;
}

void SlintApp::BindCallbacks() {
  if (!ui_) {
    return;
  }

  ui_.value()->set_property("signal_url", slint::SharedString(config_.signal_url));
  ui_.value()->set_property("username", slint::SharedString(config_.username));

  ui_.value()->set_callback("toggle_logs", [this](auto) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.logs_drawer_open = !state_.logs_drawer_open;
    }
    ApplyStateToUi();
    return InterpreterValue();
  });

  ui_.value()->set_callback("connect_or_disconnect", [this](auto) {
    const UiSnapshot snapshot = SnapshotState();
    if (snapshot.connected) {
      controller_->DisconnectFromSignalServer();
    } else {
      controller_->ConnectToSignalServer(config_.signal_url, config_.username);
    }
    return InterpreterValue();
  });

  ui_.value()->set_callback("select_peer", [this](std::span<const InterpreterValue> args) {
    if (!args.empty()) {
      if (const auto value = args[0].to_string()) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        state_.selected_peer_id = ToStdString(*value);
      }
    }
    ApplyStateToUi();
    return InterpreterValue();
  });

  ui_.value()->set_callback("start_call", [this](auto) {
    const UiSnapshot snapshot = SnapshotState();
    if (!snapshot.selected_peer_id.empty()) {
      controller_->StartCall(snapshot.selected_peer_id);
    }
    return InterpreterValue();
  });

  ui_.value()->set_callback("hang_up", [this](auto) {
    controller_->EndCall();
    return InterpreterValue();
  });

  ui_.value()->set_callback("use_camera", [this](auto) {
    LocalVideoSourceConfig config;
    config.kind = LocalVideoSourceKind::Camera;
    controller_->SetLocalVideoSource(config);
    ApplyStateToUi();
    return InterpreterValue();
  });

  ui_.value()->set_callback("open_media_file", [this](auto) {
    const std::optional<std::string> file_path = PromptForMediaFilePath();
    if (file_path) {
      LocalVideoSourceConfig config;
      config.kind = LocalVideoSourceKind::File;
      config.file_path = *file_path;
      controller_->SetLocalVideoSource(config);
      ApplyStateToUi();
    }
    return InterpreterValue();
  });

  ui_.value()->set_callback("accept_incoming", [this](auto) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.incoming_caller_id.reset();
    }
    controller_->AcceptCall();
    ApplyStateToUi();
    return InterpreterValue();
  });

  ui_.value()->set_callback("reject_incoming", [this](auto) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.incoming_caller_id.reset();
    }
    controller_->RejectCall("rejected");
    ApplyStateToUi();
    return InterpreterValue();
  });

  ui_.value()->set_callback("dismiss_dialog", [this](auto) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.dialog.reset();
    }
    ApplyStateToUi();
    return InterpreterValue();
  });
}

void SlintApp::StartTimers() {
  stats_timer_.start(slint::TimerMode::Repeated, std::chrono::seconds(1), [this]() {
    RefreshStats();
  });
  video_timer_.start(slint::TimerMode::Repeated, std::chrono::milliseconds(33), [this]() {
    RefreshVideoFrames();
  });
}

void SlintApp::StopTimers() {
  if (stats_timer_.running()) {
    stats_timer_.stop();
  }
  if (video_timer_.running()) {
    video_timer_.stop();
  }
}

void SlintApp::ApplyStateToUi() {
  if (!ui_) {
    return;
  }

  const UiSnapshot snapshot = SnapshotState();
  const LocalVideoSourceState source_state = controller_->GetLocalVideoSourceState();
  const bool can_start_call = snapshot.connected &&
                              snapshot.call_state == CallState::Idle &&
                              !snapshot.selected_peer_id.empty();
  const bool can_hang_up = snapshot.call_state != CallState::Idle;

  ui_.value()->set_property("connected", snapshot.connected);
  ui_.value()->set_property("client_id", slint::SharedString(snapshot.client_id));
  ui_.value()->set_property("selected_peer_id",
                             slint::SharedString(snapshot.selected_peer_id));
  ui_.value()->set_property("current_peer_id",
                             slint::SharedString(snapshot.current_peer_id));
  ui_.value()->set_property("connection_status_text",
                             slint::SharedString(BuildStatusLabel(snapshot.connected)));
  ui_.value()->set_property("connection_button_text",
                             slint::SharedString(
                                 BuildConnectionActionLabel(snapshot.connected)));
  ui_.value()->set_property("logs_button_text",
                             slint::SharedString(
                                 BuildLogsToggleLabel(snapshot.logs_drawer_open)));
  ui_.value()->set_property("logs_visible", snapshot.logs_drawer_open);
  ui_.value()->set_property("call_state_text",
                             slint::SharedString(
                                 GetCallStateLabel(snapshot.call_state)));
  ui_.value()->set_property("peer_text",
                             slint::SharedString(
                                 snapshot.current_peer_id.empty()
                                     ? std::string("(none)")
                                     : snapshot.current_peer_id));
  ui_.value()->set_property("can_start_call", can_start_call);
  ui_.value()->set_property("can_hang_up", can_hang_up);
  ui_.value()->set_property("media_mode_text",
                             slint::SharedString(
                                 LocalVideoSourceKindToString(source_state.kind)));
  ui_.value()->set_property(
      "media_detail_text",
      slint::SharedString(
          !source_state.display_name.empty()
              ? source_state.display_name
              : (source_state.file_path.empty() ? std::string("Camera")
                                                : source_state.file_path)));
  ui_.value()->set_property("stats_text",
                             slint::SharedString(BuildStatsText()));
  ui_.value()->set_property("logs_text",
                             slint::SharedString(BuildLogsText(snapshot)));
  ui_.value()->set_property("clients", InterpreterValue(ToSlintArray(
                                              BuildClientList(snapshot))));
  ui_.value()->set_property("incoming_visible",
                             snapshot.incoming_caller_id.has_value());
  ui_.value()->set_property(
      "incoming_caller_id",
      slint::SharedString(snapshot.incoming_caller_id.value_or("")));
  ui_.value()->set_property("dialog_visible", snapshot.dialog.has_value());
  ui_.value()->set_property(
      "dialog_title",
      slint::SharedString(snapshot.dialog ? snapshot.dialog->title : std::string()));
  ui_.value()->set_property(
      "dialog_message",
      slint::SharedString(snapshot.dialog ? snapshot.dialog->message
                                          : std::string()));
  ui_.value()->set_property(
      "dialog_kind",
      slint::SharedString(snapshot.dialog && snapshot.dialog->error ? "Error"
                                                                    : "Info"));
  UpdateVideoProperties();
}

void SlintApp::ScheduleUiRefresh() {
  if (shutting_down_.load() || !ui_ || !event_loop_running_.load()) {
    return;
  }
  if (ui_refresh_pending_.exchange(true)) {
    return;
  }

  slint::invoke_from_event_loop([this]() {
    ui_refresh_pending_.store(false);
    if (!shutting_down_.load() && ui_) {
      ApplyStateToUi();
    }
  });
}

void SlintApp::RefreshStats() {
  if (!ui_) {
    return;
  }
  ApplyStateToUi();
}

void SlintApp::RefreshVideoFrames() {
  if (!ui_) {
    return;
  }

  const UiSnapshot snapshot = SnapshotState();
  bool remote_ready = snapshot.remote_video_ready;
  bool local_ready = snapshot.local_video_ready;
  bool changed = false;

  changed = UpdateImageFromRenderer(&remote_renderer_, &remote_video_image_,
                                    &remote_ready) || changed;
  changed =
      UpdateImageFromRenderer(&local_renderer_, &local_video_image_, &local_ready) ||
      changed;

  if (!snapshot.remote_video_ready && remote_ready) {
    PushLog("info", "Remote video first frame received");
  }
  if (!snapshot.local_video_ready && local_ready) {
    PushLog("info", "Local video first frame received");
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.remote_video_ready = remote_ready;
    state_.local_video_ready = local_ready;
  }

  if (changed) {
    UpdateVideoProperties();
  }
}

void SlintApp::UpdateVideoProperties() {
  if (!ui_) {
    return;
  }

  const UiSnapshot snapshot = SnapshotState();
  ui_.value()->set_property("remote_video_image",
                            InterpreterValue(remote_video_image_));
  ui_.value()->set_property("local_video_image",
                            InterpreterValue(local_video_image_));
  ui_.value()->set_property("remote_video_ready",
                            snapshot.remote_video_visible &&
                                snapshot.remote_video_ready);
  ui_.value()->set_property("local_video_ready",
                            snapshot.local_video_visible &&
                                snapshot.local_video_ready);
}

bool SlintApp::UpdateImageFromRenderer(VideoRenderer* renderer,
                                       slint::Image* image,
                                       bool* ready) {
  if (!renderer || !image || !ready) {
    return false;
  }

  const auto latest_frame = renderer->ConsumeLatestFrame();
  if (!latest_frame) {
    return false;
  }

  if (latest_frame->width <= 0 || latest_frame->height <= 0 ||
      latest_frame->pixels.empty()) {
    *image = slint::Image();
    *ready = false;
    return true;
  }

  *image = ConvertFrameToImage(*latest_frame);
  *ready = true;
  return true;
}

slint::Image SlintApp::ConvertFrameToImage(const VideoRenderer::Frame& frame) {
  slint::SharedPixelBuffer<slint::Rgba8Pixel> pixel_buffer(
      static_cast<uint32_t>(frame.width), static_cast<uint32_t>(frame.height));

  auto* pixels = pixel_buffer.begin();
  const size_t pixel_count =
      static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);

  for (size_t i = 0; i < pixel_count; ++i) {
    const size_t offset = i * 4;
    pixels[i] = slint::Rgba8Pixel{
        frame.pixels[offset + 2],
        frame.pixels[offset + 1],
        frame.pixels[offset + 0],
        frame.pixels[offset + 3],
    };
  }

  return slint::Image(std::move(pixel_buffer));
}

void SlintApp::ClearVideoImage(bool local) {
  if (shutting_down_.load()) {
    return;
  }

  auto clear_on_ui_thread = [this, local]() {
    if (local) {
      local_video_image_ = slint::Image();
    } else {
      remote_video_image_ = slint::Image();
    }
    UpdateVideoProperties();
  };

  if (event_loop_running_.load()) {
    slint::invoke_from_event_loop(clear_on_ui_thread);
  } else {
    clear_on_ui_thread();
  }
}

void SlintApp::PushLog(const std::string& level, const std::string& message) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  state_.logs.push_back(LogEntry{GetNowString(), level, message});
  while (state_.logs.size() > 200) {
    state_.logs.pop_front();
  }
}

SlintApp::UiSnapshot SlintApp::SnapshotState() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return state_;
}

std::string SlintApp::BuildStatsText() const {
  const RtcStatsSnapshot stats = controller_->GetLatestRtcStats();
  std::vector<std::string> lines;
  lines.push_back("ICE: " + (stats.ice_state.empty() ? std::string("-")
                                                     : stats.ice_state));
  lines.push_back("Mic: " +
                  std::string(stats.local_audio_track_attached ? "Attached"
                                                               : "Idle"));
  lines.push_back("Speaker: " +
                  std::string(stats.audio_playout_active ? "Playing" : "Idle"));
  lines.push_back("Remote Audio: " +
                  std::string(stats.remote_audio_track_attached ? "Attached"
                                                                : "Waiting"));

  if (!stats.valid) {
    lines.push_back("Outbound: -");
    lines.push_back("Inbound: -");
    lines.push_back("RTT: -");
    lines.push_back("Video: -");
    return JoinLines(lines);
  }

  lines.push_back("Outbound: " +
                  std::to_string(static_cast<int>(stats.outbound_bitrate_kbps)) +
                  " kbps");
  lines.push_back("Inbound: " +
                  std::to_string(static_cast<int>(stats.inbound_bitrate_kbps)) +
                  " kbps");
  lines.push_back("RTT: " +
                  std::to_string(static_cast<int>(stats.current_rtt_ms)) + " ms");
  lines.push_back("Audio Jitter: " +
                  std::to_string(static_cast<int>(stats.inbound_audio_jitter_ms)) +
                  " ms");
  lines.push_back(
      "Audio Loss: " +
      std::to_string(static_cast<int>(stats.inbound_audio_packet_loss_percent)) +
      "%");
  lines.push_back("Video: " + std::to_string(stats.inbound_video_width) + "x" +
                  std::to_string(stats.inbound_video_height) + " @ " +
                  std::to_string(static_cast<int>(stats.inbound_video_fps)) +
                  " fps");
  return JoinLines(lines);
}

std::string SlintApp::BuildLogsText(const UiSnapshot& snapshot) const {
  std::vector<std::string> lines;
  lines.reserve(snapshot.logs.size());
  for (const LogEntry& entry : snapshot.logs) {
    lines.push_back("[" + entry.timestamp + "] " + entry.message);
  }
  return JoinLines(lines);
}

std::vector<std::string> SlintApp::BuildClientList(
    const UiSnapshot& snapshot) const {
  std::vector<std::string> clients;
  clients.reserve(snapshot.clients.size());
  for (const ClientInfo& client : snapshot.clients) {
    if (client.id == snapshot.client_id) {
      continue;
    }
    clients.push_back(client.id);
  }
  return clients;
}

std::optional<std::string> SlintApp::PromptForMediaFilePath() {
  if (!ui_) {
    return std::nullopt;
  }
  auto ui = ui_.value();

  char file_buffer[MAX_PATH] = {};
  OPENFILENAMEA dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = ui->window().win32_hwnd();
  dialog.lpstrFilter =
      "Media Files (*.mp4;*.mov;*.mkv;*.avi)\0*.mp4;*.mov;*.mkv;*.avi\0All Files (*.*)\0*.*\0";
  dialog.lpstrFile = file_buffer;
  dialog.nMaxFile = MAX_PATH;
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  dialog.lpstrTitle = "Select Media File";

  if (!GetOpenFileNameA(&dialog)) {
    return std::nullopt;
  }
  return std::string(dialog.lpstrFile);
}

std::string SlintApp::GetNowString() const {
  const auto now = std::chrono::system_clock::now();
  const std::time_t current_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time = {};
  localtime_s(&local_time, &current_time);

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%H:%M:%S");
  return stream.str();
}

std::string SlintApp::GetCallStateLabel(CallState state) const {
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

std::string SlintApp::ToStdString(const slint::SharedString& value) {
  return std::string(value.data(), value.size());
}

std::string SlintApp::JoinLines(const std::vector<std::string>& lines) {
  std::ostringstream stream;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      stream << "\n";
    }
    stream << lines[i];
  }
  return stream.str();
}
