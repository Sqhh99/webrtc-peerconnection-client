#ifndef VIDEO_CALL_WINDOW_H_GUARD
#define VIDEO_CALL_WINDOW_H_GUARD

#include <memory>
#include <string>

// Fix Qt emit macro conflict with WebRTC sigslot
#ifdef emit
#undef emit
#define QT_NO_EMIT_DEFINED
#endif

#include "api/media_stream_interface.h"

#ifdef QT_NO_EMIT_DEFINED
#define emit
#undef QT_NO_EMIT_DEFINED
#endif

#include "icall_observer.h"
#include "call_coordinator.h"
#include "callmanager.h"

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QTextEdit>
#include <QGroupBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QJsonArray>

class VideoRenderer;

// VideoCallWindow - 视频通话窗口（重构版MainWnd）
// 职责：纯UI展示和用户交互，通过ICallUIObserver接收回调，通过ICallController控制业务
// 优点：与业务逻辑完全解耦，可复用性高，易于测试
class VideoCallWindow : public QMainWindow, public ICallUIObserver {
  Q_OBJECT

 public:
  explicit VideoCallWindow(ICallController* controller, QWidget* parent = nullptr);
  ~VideoCallWindow() override;

  // ICallUIObserver 实现
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
  void OnClientListUpdate(const QJsonArray& clients) override;
  void OnCallStateChanged(CallState state, const std::string& peer_id) override;
  void OnIncomingCall(const std::string& caller_id) override;

 private slots:
  // 连接相关
  void OnConnectClicked();
  void OnDisconnectClicked();
  
  // 用户列表
  void OnUserItemDoubleClicked(QListWidgetItem* item);
  
  // 呼叫控制
  void OnCallButtonClicked();
  void OnHangupButtonClicked();
  
  // 定时更新
  void OnUpdateStatsTimer();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void CreateUI();
  void CreateConnectionPanel();
  void CreateUserListPanel();
  void CreateLogPanel();
  void CreateVideoPanel();
  void CreateStatsPanel();
  void CreateControlPanel();
  
  void UpdateUIState();
  void UpdateCallButtonState();
  void LayoutLocalVideo();
  void UpdateStatsUI(const RtcStatsSnapshot& stats);
  QString FormatBitrate(double kbps) const;
  QString FormatPercentage(double value) const;
  QString FormatDouble(double value, int precision) const;
  QString FormatResolution(int width, int height) const;
  QString FormatTimestamp(uint64_t timestamp_ms) const;
  
  QString GetCallStateString(CallState state) const;
  void AppendLogInternal(const QString& message, const QString& level);
  
  // 业务控制器
  ICallController* controller_;
  
  // UI状态
  bool is_connected_;
  QString current_peer_id_;
  QString incoming_caller_id_;
  
  // UI组件
  QWidget* connection_panel_;
  QLineEdit* server_url_edit_;
  QLineEdit* client_id_edit_;
  QPushButton* connect_button_;
  QPushButton* disconnect_button_;
  QLabel* connection_status_label_;
  
  QGroupBox* user_list_group_;
  QListWidget* user_list_;
  
  QGroupBox* log_group_;
  QTextEdit* log_text_;
  
  QWidget* video_panel_;
  QLabel* local_video_label_;
  QLabel* remote_video_label_;
  QLabel* call_status_label_;
  QWidget* lower_panel_;
  QGroupBox* stats_group_;
  QLabel* stats_timestamp_value_;
  QLabel* stats_ice_state_value_;
  QLabel* stats_outbound_bitrate_value_;
  QLabel* stats_inbound_bitrate_value_;
  QLabel* stats_rtt_value_;
  QLabel* stats_audio_jitter_value_;
  QLabel* stats_audio_loss_value_;
  QLabel* stats_video_loss_value_;
  QLabel* stats_video_fps_value_;
  QLabel* stats_video_resolution_value_;
  
  QWidget* control_panel_;
  QPushButton* call_button_;
  QPushButton* hangup_button_;
  QLabel* call_info_label_;
  
  QSplitter* main_splitter_;
  QSplitter* right_splitter_;
  
  // 视频渲染器
  std::unique_ptr<VideoRenderer> local_renderer_;
  std::unique_ptr<VideoRenderer> remote_renderer_;
  
  // 定时器
  std::unique_ptr<QTimer> stats_timer_;
};

#endif  // VIDEO_CALL_WINDOW_H_GUARD
