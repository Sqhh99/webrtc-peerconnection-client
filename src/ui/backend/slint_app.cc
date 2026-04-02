#include <windows.h>
#include "ui/slint_app.h"

#include <commdlg.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace {

std::string BuildStatusLabel(bool connected) {
  return connected ? "Connected" : "Disconnected";
}

std::string BuildConnectionActionLabel(bool connected) {
  return connected ? "Disconnect" : "Reconnect";
}

std::string BuildLogsToggleLabel(bool visible) {
  return visible ? "Hide Logs" : "Show Logs";
}

}  // namespace

SlintApp::SlintApp(ICallController* controller, AppConfig config)
    : controller_(controller), config_(std::move(config)) {
  remote_renderer_.ConfigureDisplayOutput(1280, 720,
                                          std::chrono::milliseconds(33));
  local_renderer_.ConfigureDisplayOutput(640, 360,
                                         std::chrono::milliseconds(41));
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
  if (!InitializeVideoRenderer()) {
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
  }
  local_renderer_.Clear();
  local_renderer_.SetVideoTrack(track);
  video_renderer_bridge_.AttachLocalRenderer(&local_renderer_);
  PushLog("info", "Local video track attached");
  ScheduleUiRefresh();
}

void SlintApp::OnStopLocalRenderer() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.local_video_visible = false;
  }
  local_renderer_.Stop();
  video_renderer_bridge_.DetachLocalRenderer();
  PushLog("info", "Local video track removed");
  ScheduleUiRefresh();
}

void SlintApp::OnStartRemoteRenderer(webrtc::VideoTrackInterface* track) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.remote_video_visible = true;
  }
  remote_renderer_.Clear();
  remote_renderer_.SetVideoTrack(track);
  video_renderer_bridge_.AttachRemoteRenderer(&remote_renderer_);
  PushLog("info", "Remote video track attached");
  ScheduleUiRefresh();
}

void SlintApp::OnStopRemoteRenderer() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.remote_video_visible = false;
  }
  remote_renderer_.Stop();
  video_renderer_bridge_.DetachRemoteRenderer();
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
  ui_ = AppWindow::create();
  return true;
}

bool SlintApp::InitializeVideoRenderer() {
  if (!ui_) {
    return false;
  }

  if (!video_renderer_bridge_.Initialize(ui_.value())) {
    std::cerr << "Failed to initialize Slint video renderer bridge.\n";
    return false;
  }

  ui_.value()->set_remote_video_image(slint::Image());
  ui_.value()->set_local_video_image(slint::Image());
  return true;
}

void SlintApp::BindCallbacks() {
  if (!ui_) {
    return;
  }

  auto ui = ui_.value();
  ui->set_signal_url(slint::SharedString(config_.signal_url));
  ui->set_username(slint::SharedString(config_.username));

  ui->on_toggle_logs([this]() {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.logs_drawer_open = !state_.logs_drawer_open;
    }
    ApplyStateToUi();
  });

  ui->on_connect_or_disconnect([this]() {
    const UiSnapshot snapshot = SnapshotState();
    if (snapshot.connected) {
      controller_->DisconnectFromSignalServer();
    } else {
      controller_->ConnectToSignalServer(config_.signal_url, config_.username);
    }
  });

  ui->on_select_peer([this](slint::SharedString value) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.selected_peer_id = ToStdString(value);
    }
    ApplyStateToUi();
  });

  ui->on_start_call([this]() {
    const UiSnapshot snapshot = SnapshotState();
    if (!snapshot.selected_peer_id.empty()) {
      controller_->StartCall(snapshot.selected_peer_id);
    }
  });

  ui->on_hang_up([this]() {
    controller_->EndCall();
  });

  ui->on_use_camera([this]() {
    LocalVideoSourceConfig local_config;
    local_config.kind = LocalVideoSourceKind::Camera;
    controller_->SetLocalVideoSource(local_config);
    ApplyStateToUi();
  });

  ui->on_open_media_file([this]() {
    const std::optional<std::string> file_path = PromptForMediaFilePath();
    if (!file_path) {
      return;
    }

    LocalVideoSourceConfig local_config;
    local_config.kind = LocalVideoSourceKind::File;
    local_config.file_path = *file_path;
    controller_->SetLocalVideoSource(local_config);
    ApplyStateToUi();
  });

  ui->on_accept_incoming([this]() {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.incoming_caller_id.reset();
    }
    controller_->AcceptCall();
    ApplyStateToUi();
  });

  ui->on_reject_incoming([this]() {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.incoming_caller_id.reset();
    }
    controller_->RejectCall("rejected");
    ApplyStateToUi();
  });

  ui->on_dismiss_dialog([this]() {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      state_.dialog.reset();
    }
    ApplyStateToUi();
  });
}

void SlintApp::StartTimers() {
  stats_timer_.start(slint::TimerMode::Repeated, std::chrono::seconds(1), [this]() {
    RefreshStats();
  });
}

void SlintApp::StopTimers() {
  if (stats_timer_.running()) {
    stats_timer_.stop();
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

  auto ui = ui_.value();
  ui->set_connected(snapshot.connected);
  ui->set_client_id(slint::SharedString(snapshot.client_id));
  ui->set_selected_peer_id(slint::SharedString(snapshot.selected_peer_id));
  ui->set_current_peer_id(slint::SharedString(snapshot.current_peer_id));
  ui->set_connection_status_text(slint::SharedString(
      BuildStatusLabel(snapshot.connected)));
  ui->set_connection_button_text(slint::SharedString(
      BuildConnectionActionLabel(snapshot.connected)));
  ui->set_logs_button_text(slint::SharedString(
      BuildLogsToggleLabel(snapshot.logs_drawer_open)));
  ui->set_logs_visible(snapshot.logs_drawer_open);
  ui->set_call_state_text(slint::SharedString(
      GetCallStateLabel(snapshot.call_state)));
  ui->set_peer_text(slint::SharedString(
      snapshot.current_peer_id.empty() ? std::string("(none)")
                                       : snapshot.current_peer_id));
  ui->set_can_start_call(can_start_call);
  ui->set_can_hang_up(can_hang_up);
  ui->set_media_mode_text(slint::SharedString(
      LocalVideoSourceKindToString(source_state.kind)));
  ui->set_media_detail_text(slint::SharedString(
      !source_state.display_name.empty()
          ? source_state.display_name
          : (source_state.file_path.empty() ? std::string("Camera")
                                            : source_state.file_path)));
  ui->set_stats_text(slint::SharedString(BuildStatsText()));
  ui->set_logs_text(slint::SharedString(BuildLogsText(snapshot)));
  ui->set_clients(std::make_shared<slint::VectorModel<slint::SharedString>>(
      BuildClientList(snapshot)));
  ui->set_incoming_visible(snapshot.incoming_caller_id.has_value());
  ui->set_incoming_caller_id(
      slint::SharedString(snapshot.incoming_caller_id.value_or("")));
  ui->set_dialog_visible(snapshot.dialog.has_value());
  ui->set_dialog_title(slint::SharedString(
      snapshot.dialog ? snapshot.dialog->title : std::string()));
  ui->set_dialog_message(slint::SharedString(
      snapshot.dialog ? snapshot.dialog->message : std::string()));
  ui->set_dialog_kind(slint::SharedString(
      snapshot.dialog && snapshot.dialog->error ? "Error" : "Info"));
  ui->set_remote_video_visible(snapshot.remote_video_visible);
  ui->set_local_video_visible(snapshot.local_video_visible);

  if (snapshot.remote_video_visible || snapshot.local_video_visible) {
    video_renderer_bridge_.RequestRedraw();
  }
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

std::vector<slint::SharedString> SlintApp::BuildClientList(
    const UiSnapshot& snapshot) const {
  std::vector<slint::SharedString> clients;
  clients.reserve(snapshot.clients.size());
  for (const ClientInfo& client : snapshot.clients) {
    if (client.id == snapshot.client_id) {
      continue;
    }
    clients.push_back(slint::SharedString(client.id));
  }
  return clients;
}

std::optional<std::string> SlintApp::PromptForMediaFilePath() {
  if (!ui_) {
    return std::nullopt;
  }

  char file_buffer[MAX_PATH] = {};
  OPENFILENAMEA dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = ui_.value()->window().win32_hwnd();
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
