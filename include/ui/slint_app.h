#ifndef SLINT_APP_H_GUARD
#define SLINT_APP_H_GUARD

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <slint_timer.h>

#include "app-window.h"
#include "app_config.h"
#include "icall_observer.h"
#include "ui/slint_video_renderer.h"
#include "videorenderer.h"

class SlintApp : public ICallUIObserver {
 public:
  SlintApp(ICallController* controller, AppConfig config);
  ~SlintApp() override;

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
    bool local_video_visible = false;
    bool remote_video_visible = false;
    std::string client_id;
    std::vector<ClientInfo> clients;
    std::string current_peer_id;
    std::string selected_peer_id;
    std::optional<std::string> incoming_caller_id;
    std::deque<LogEntry> logs;
    std::optional<DialogMessage> dialog;
    CallState call_state = CallState::Idle;
  };

  bool LoadUi();
  bool InitializeVideoRenderer();
  void BindCallbacks();
  void StartTimers();
  void StopTimers();
  void ApplyStateToUi();
  void ScheduleUiRefresh();
  void RefreshStats();

  void PushLog(const std::string& level, const std::string& message);
  UiSnapshot SnapshotState() const;
  std::string BuildStatsText() const;
  std::string BuildLogsText(const UiSnapshot& snapshot) const;
  std::vector<slint::SharedString> BuildClientList(const UiSnapshot& snapshot) const;
  std::optional<std::string> PromptForMediaFilePath();
  std::string GetNowString() const;
  std::string GetCallStateLabel(CallState state) const;

  static std::string ToStdString(const slint::SharedString& value);
  static std::string JoinLines(const std::vector<std::string>& lines);

  ICallController* controller_;
  AppConfig config_;

  mutable std::mutex state_mutex_;
  UiSnapshot state_;

  SlintVideoRenderer video_renderer_bridge_;
  std::optional<slint::ComponentHandle<AppWindow>> ui_;
  slint::Timer stats_timer_;
  std::atomic_bool event_loop_running_{false};
  std::atomic_bool ui_refresh_pending_{false};
  std::atomic_bool shutting_down_{false};

  VideoRenderer local_renderer_;
  VideoRenderer remote_renderer_;
};

#endif  // SLINT_APP_H_GUARD
