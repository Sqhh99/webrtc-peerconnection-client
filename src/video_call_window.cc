#include "video_call_window.h"
#include "videorenderer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QGridLayout>
#include <cmath>

VideoCallWindow::VideoCallWindow(ICallController* controller, QWidget* parent)
    : QMainWindow(parent),
      controller_(controller),
      is_connected_(false) {
  setWindowTitle("WebRTC Video Call Client");
  resize(1200, 800);
  
  // 设置全局样式
  setStyleSheet(R"(
    QMainWindow {
      background-color: #eef1f5;
    }
    QWidget {
      font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
      font-size: 12px;
      color: #2d3748;
    }
    QGroupBox {
      background-color: #ffffff;
      border: 1px solid #d9dde3;
      border-radius: 4px;
      margin-top: 10px;
      padding-top: 8px;
      font-weight: 600;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 12px;
      padding: 0 4px;
      color: #1a202c;
      background-color: #ffffff;
    }
    QPushButton {
      background-color: #2b6cb0;
      color: #ffffff;
      border: none;
      border-radius: 3px;
      padding: 6px 14px;
    }
    QPushButton:disabled {
      background-color: #cbd5e0;
      color: #718096;
    }
    QPushButton#connectButton {
      background-color: #2f855a;
    }
    QPushButton#disconnectButton {
      background-color: #4a5568;
    }
    QPushButton#hangupButton {
      background-color: #c53030;
    }
    QLineEdit, QListWidget, QTextEdit {
      background-color: #ffffff;
      border: 1px solid #d9dde3;
      border-radius: 3px;
      padding: 6px;
    }
    QListWidget::item {
      padding: 6px 10px;
    }
    QListWidget::item:selected {
      background-color: #edf2f7;
      color: #1a202c;
    }
    QTextEdit {
      padding: 8px;
    }
    QLabel {
      border: none;
    }
  )");
  
  // 创建UI
  CreateUI();
  UpdateUIState();
  
  // 创建统计定时器
  stats_timer_ = std::make_unique<QTimer>(this);
  connect(stats_timer_.get(), &QTimer::timeout, this, &VideoCallWindow::OnUpdateStatsTimer);
  stats_timer_->start(1000);  // 每秒更新一次
  
  AppendLogInternal("应用程序已启动", "info");
}

VideoCallWindow::~VideoCallWindow() {
  // 停止定时器
  if (stats_timer_) {
    stats_timer_->stop();
  }
}

// ============================================================================
// ICallUIObserver 实现
// ============================================================================

void VideoCallWindow::OnStartLocalRenderer(webrtc::VideoTrackInterface* track) {
  QMetaObject::invokeMethod(this, [this, track]() {
    if (!local_renderer_) {
      local_renderer_ = std::make_unique<VideoRenderer>(video_panel_);
      local_renderer_->setFixedSize(220, 160);
      local_renderer_->setStyleSheet(R"(
        QLabel {
          border: 2px solid rgba(255, 255, 255, 0.8);
          border-radius: 6px;
          background-color: #1a202c;
        }
      )");
      local_renderer_->raise();
    }
    local_renderer_->SetVideoTrack(track);
    local_renderer_->show();
    LayoutLocalVideo();
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnStopLocalRenderer() {
  QMetaObject::invokeMethod(this, [this]() {
    if (local_renderer_) {
      local_renderer_->Stop();
      local_renderer_->hide();
    }
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnStartRemoteRenderer(webrtc::VideoTrackInterface* track) {
  QMetaObject::invokeMethod(this, [this, track]() {
    if (remote_renderer_) {
      remote_renderer_->SetVideoTrack(track);
      remote_renderer_->show();
      call_status_label_->hide();
    }
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnStopRemoteRenderer() {
  QMetaObject::invokeMethod(this, [this]() {
    if (remote_renderer_) {
      remote_renderer_->Stop();
      remote_renderer_->hide();
      call_status_label_->show();
      call_status_label_->setText("等待远端视频...");
    }
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnLogMessage(const std::string& message, const std::string& level) {
  QMetaObject::invokeMethod(this, [this, message, level]() {
    AppendLogInternal(QString::fromStdString(message), QString::fromStdString(level));
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnShowError(const std::string& title, const std::string& message) {
  QMetaObject::invokeMethod(this, [this, title, message]() {
    QMessageBox::critical(this, QString::fromStdString(title), QString::fromStdString(message));
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnShowInfo(const std::string& title, const std::string& message) {
  QMetaObject::invokeMethod(this, [this, title, message]() {
    QMessageBox::information(this, QString::fromStdString(title), QString::fromStdString(message));
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnSignalConnected(const std::string& client_id) {
  QMetaObject::invokeMethod(this, [this, client_id]() {
    QString qclient_id = QString::fromStdString(client_id);
    is_connected_ = true;
    client_id_edit_->setText(qclient_id);
    
    connection_status_label_->setText(QString("已连接 [%1]").arg(qclient_id));
    connection_status_label_->setStyleSheet("color: #2f855a; font-weight: 600;");
    
    connect_button_->setEnabled(false);
    disconnect_button_->setEnabled(true);
    server_url_edit_->setEnabled(false);
    client_id_edit_->setEnabled(false);
    
    UpdateUIState();
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnSignalDisconnected() {
  QMetaObject::invokeMethod(this, [this]() {
    is_connected_ = false;
    
    connection_status_label_->setText("未连接");
    connection_status_label_->setStyleSheet("color: #c53030; font-weight: 600;");
    
    connect_button_->setEnabled(true);
    disconnect_button_->setEnabled(false);
    server_url_edit_->setEnabled(true);
    client_id_edit_->setEnabled(true);
    
    user_list_->clear();
    
    UpdateUIState();
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnSignalError(const std::string& error) {
  QString qerror = QString::fromStdString(error);
  AppendLogInternal(QString("信令错误: %1").arg(qerror), "error");
}

void VideoCallWindow::OnClientListUpdate(const QJsonArray& clients) {
  QMetaObject::invokeMethod(this, [this, clients]() {
    user_list_->clear();
    QString my_id = QString::fromStdString(controller_->GetClientId());
    
    int online_count = 0;
    for (const QJsonValue& value : clients) {
      QJsonObject client = value.toObject();
      QString client_id = client["id"].toString();
      
      // 不显示自己
      if (client_id == my_id) {
        continue;
      }
      
      QListWidgetItem* item = new QListWidgetItem(client_id);
      item->setData(Qt::UserRole, client_id);
      user_list_->addItem(item);
      online_count++;
    }
    
    AppendLogInternal(QString("用户列表已更新，在线用户: %1").arg(online_count), "info");
    UpdateUIState();
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnCallStateChanged(CallState state, const std::string& peer_id) {
  QMetaObject::invokeMethod(this, [this, state, peer_id]() {
    QString qpeer_id = QString::fromStdString(peer_id);
    current_peer_id_ = qpeer_id;
    
    UpdateCallButtonState();
    
    if (state == CallState::Connected) {
      call_status_label_->hide();
      if (remote_renderer_) {
        remote_renderer_->show();
      }
    } else if (state == CallState::Idle) {
      call_status_label_->show();
      call_status_label_->setText("等待远端视频...");
      if (remote_renderer_) {
        remote_renderer_->hide();
      }
      current_peer_id_.clear();
    }
  }, Qt::QueuedConnection);
}

void VideoCallWindow::OnIncomingCall(const std::string& caller_id) {
  QMetaObject::invokeMethod(this, [this, caller_id]() {
    QString qcaller_id = QString::fromStdString(caller_id);
    incoming_caller_id_ = qcaller_id;
    
    AppendLogInternal(QString("收到来自 %1 的呼叫").arg(qcaller_id), "info");
    
    QMessageBox msg_box(this);
    msg_box.setWindowTitle("来电");
    msg_box.setText(QString("用户 %1 正在呼叫您").arg(qcaller_id));
    
    QPushButton* acceptButton = msg_box.addButton("接听", QMessageBox::YesRole);
    msg_box.addButton("拒绝", QMessageBox::NoRole);
    
    msg_box.exec();
    
    if (msg_box.clickedButton() == acceptButton) {
      controller_->AcceptCall();
      AppendLogInternal(QString("已接听来自 %1 的呼叫").arg(qcaller_id), "success");
    } else {
      controller_->RejectCall("用户拒绝");
      AppendLogInternal(QString("已拒绝来自 %1 的呼叫").arg(qcaller_id), "info");
    }
  }, Qt::QueuedConnection);
}

// ============================================================================
// UI 创建
// ============================================================================

void VideoCallWindow::CreateUI() {
  QWidget* central_widget = new QWidget(this);
  setCentralWidget(central_widget);
  
  QVBoxLayout* main_layout = new QVBoxLayout(central_widget);
  main_layout->setContentsMargins(12, 12, 12, 12);
  main_layout->setSpacing(12);
  
  // 创建连接面板（顶部）
  CreateConnectionPanel();
  main_layout->addWidget(connection_panel_);
  
  // 创建主分割器（左右布局）
  main_splitter_ = new QSplitter(Qt::Horizontal, this);
  main_splitter_->setHandleWidth(4);
  main_splitter_->setStyleSheet("QSplitter::handle { background-color: #cbd5e0; }");
  
  // 左侧：用户列表
  CreateUserListPanel();
  main_splitter_->addWidget(user_list_group_);
  
  // 右侧：视频和日志的垂直分割
  right_splitter_ = new QSplitter(Qt::Vertical, this);
  right_splitter_->setHandleWidth(4);
  right_splitter_->setStyleSheet("QSplitter::handle { background-color: #cbd5e0; }");
  
  // 视频面板
  CreateVideoPanel();
  right_splitter_->addWidget(video_panel_);
  
  // 统计与日志面板
  CreateStatsPanel();
  CreateLogPanel();
  lower_panel_ = new QWidget(this);
  QVBoxLayout* lower_layout = new QVBoxLayout(lower_panel_);
  lower_layout->setContentsMargins(0, 0, 0, 0);
  lower_layout->setSpacing(8);
  lower_layout->addWidget(stats_group_);
  lower_layout->addWidget(log_group_);
  right_splitter_->addWidget(lower_panel_);
  
  right_splitter_->setStretchFactor(0, 3);
  right_splitter_->setStretchFactor(1, 2);
  
  main_splitter_->addWidget(right_splitter_);
  main_splitter_->setStretchFactor(0, 1);
  main_splitter_->setStretchFactor(1, 4);
  
  main_layout->addWidget(main_splitter_);
  
  // 创建控制面板（底部）
  CreateControlPanel();
  main_layout->addWidget(control_panel_);
}

void VideoCallWindow::CreateConnectionPanel() {
  connection_panel_ = new QWidget(this);
  connection_panel_->setStyleSheet("background-color: #ffffff; border: 1px solid #d9dde3; border-radius: 4px;");
  
  QHBoxLayout* layout = new QHBoxLayout(connection_panel_);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(10);
  
  QLabel* server_label = new QLabel("信令服务器:", connection_panel_);
  layout->addWidget(server_label);
  
  server_url_edit_ = new QLineEdit("ws://localhost:8081/ws/webrtc", connection_panel_);
  server_url_edit_->setMinimumWidth(260);
  layout->addWidget(server_url_edit_);
  
  QLabel* id_label = new QLabel("客户端ID:", connection_panel_);
  layout->addWidget(id_label);
  
  client_id_edit_ = new QLineEdit(connection_panel_);
  client_id_edit_->setPlaceholderText("自动生成");
  client_id_edit_->setMaximumWidth(140);
  layout->addWidget(client_id_edit_);
  
  connect_button_ = new QPushButton("连接", connection_panel_);
  connect_button_->setObjectName("connectButton");
  connect_button_->setFixedWidth(90);
  connect(connect_button_, &QPushButton::clicked, this, &VideoCallWindow::OnConnectClicked);
  layout->addWidget(connect_button_);
  
  disconnect_button_ = new QPushButton("断开", connection_panel_);
  disconnect_button_->setObjectName("disconnectButton");
  disconnect_button_->setFixedWidth(90);
  disconnect_button_->setEnabled(false);
  connect(disconnect_button_, &QPushButton::clicked, this, &VideoCallWindow::OnDisconnectClicked);
  layout->addWidget(disconnect_button_);
  
  connection_status_label_ = new QLabel("未连接", connection_panel_);
  connection_status_label_->setStyleSheet("color: #c53030; font-weight: 600;");
  layout->addWidget(connection_status_label_);
  
  layout->addStretch();
}

void VideoCallWindow::CreateUserListPanel() {
  user_list_group_ = new QGroupBox("在线用户", this);
  QVBoxLayout* layout = new QVBoxLayout(user_list_group_);
  layout->setContentsMargins(10, 14, 10, 10);
  layout->setSpacing(6);

  user_list_ = new QListWidget(user_list_group_);
  user_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  connect(user_list_, &QListWidget::itemDoubleClicked,
          this, &VideoCallWindow::OnUserItemDoubleClicked);
  layout->addWidget(user_list_);

  QLabel* hint_label = new QLabel("双击用户即可发起呼叫", user_list_group_);
  hint_label->setStyleSheet("color: #718096; font-size: 11px;");
  layout->addWidget(hint_label);
}

void VideoCallWindow::CreateLogPanel() {
  log_group_ = new QGroupBox("系统日志", this);
  QVBoxLayout* layout = new QVBoxLayout(log_group_);
  layout->setContentsMargins(10, 14, 10, 10);
  layout->setSpacing(6);
  
  log_text_ = new QTextEdit(log_group_);
  log_text_->setReadOnly(true);
  log_text_->setMaximumHeight(170);
  layout->addWidget(log_text_);
  
  QPushButton* clear_log_btn = new QPushButton("清空日志", log_group_);
  clear_log_btn->setFixedWidth(100);
  connect(clear_log_btn, &QPushButton::clicked, log_text_, &QTextEdit::clear);
  layout->addWidget(clear_log_btn, 0, Qt::AlignRight);
}

void VideoCallWindow::CreateVideoPanel() {
  video_panel_ = new QWidget(this);
  video_panel_->setStyleSheet("background-color: #1a202c; border-radius: 4px;");
  video_panel_->setMinimumHeight(420);
  
  QVBoxLayout* layout = new QVBoxLayout(video_panel_);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  
  // 远程视频（主视频）
  remote_renderer_ = std::make_unique<VideoRenderer>(video_panel_);
  remote_renderer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  remote_renderer_->setStyleSheet("QLabel { background-color: #1a202c; border-radius: 4px; }");
  layout->addWidget(remote_renderer_.get());
  remote_renderer_->hide();
  
  // 无视频提示标签
  call_status_label_ = new QLabel("等待远端视频...", video_panel_);
  call_status_label_->setAlignment(Qt::AlignCenter);
  call_status_label_->setStyleSheet("color: #a0aec0; font-size: 16px;");
  layout->addWidget(call_status_label_);
}

void VideoCallWindow::CreateStatsPanel() {
  stats_group_ = new QGroupBox("WebRTC 实时数据", this);
  QGridLayout* layout = new QGridLayout(stats_group_);
  layout->setContentsMargins(10, 14, 10, 10);
  layout->setHorizontalSpacing(12);
  layout->setVerticalSpacing(6);

  auto add_row = [layout, this](int row, const QString& label_text, QLabel** value_label) {
    QLabel* label = new QLabel(label_text, stats_group_);
    QLabel* value = new QLabel("—", stats_group_);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(label, row, 0);
    layout->addWidget(value, row, 1);
    *value_label = value;
  };

  int row = 0;
  add_row(row++, "更新时间", &stats_timestamp_value_);
  add_row(row++, "ICE 状态", &stats_ice_state_value_);
  add_row(row++, "上行码率", &stats_outbound_bitrate_value_);
  add_row(row++, "下行码率", &stats_inbound_bitrate_value_);
  add_row(row++, "往返时延", &stats_rtt_value_);
  add_row(row++, "音频抖动", &stats_audio_jitter_value_);
  add_row(row++, "音频丢包率", &stats_audio_loss_value_);
  add_row(row++, "视频丢包率", &stats_video_loss_value_);
  add_row(row++, "视频帧率", &stats_video_fps_value_);
  add_row(row++, "视频分辨率", &stats_video_resolution_value_);

  layout->setColumnStretch(0, 0);
  layout->setColumnStretch(1, 1);

  if (stats_ice_state_value_) {
    stats_ice_state_value_->setText("未连接");
  }
}

void VideoCallWindow::CreateControlPanel() {
  control_panel_ = new QWidget(this);
  control_panel_->setStyleSheet("background-color: #ffffff; border: 1px solid #d9dde3; border-radius: 4px;");
  
  QHBoxLayout* layout = new QHBoxLayout(control_panel_);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(10);
  
  call_button_ = new QPushButton("呼叫", control_panel_);
  call_button_->setObjectName("callButton");
  call_button_->setEnabled(false);
  call_button_->setMinimumHeight(40);
  call_button_->setFixedWidth(110);
  connect(call_button_, &QPushButton::clicked, this, &VideoCallWindow::OnCallButtonClicked);
  layout->addWidget(call_button_);
  
  hangup_button_ = new QPushButton("挂断", control_panel_);
  hangup_button_->setObjectName("hangupButton");
  hangup_button_->setEnabled(false);
  hangup_button_->setMinimumHeight(40);
  hangup_button_->setFixedWidth(110);
  connect(hangup_button_, &QPushButton::clicked, this, &VideoCallWindow::OnHangupButtonClicked);
  layout->addWidget(hangup_button_);
  
  call_info_label_ = new QLabel("空闲", control_panel_);
  call_info_label_->setStyleSheet("font-weight: 600; color: #4a5568; padding-left: 12px;");
  layout->addWidget(call_info_label_);
  
  layout->addStretch();
}

// ============================================================================
// 槽函数
// ============================================================================

void VideoCallWindow::OnConnectClicked() {
  QString server_url = server_url_edit_->text().trimmed();
  if (server_url.isEmpty()) {
    QMessageBox::critical(this, "错误", "请输入信令服务器地址");
    return;
  }
  
  QString client_id = client_id_edit_->text().trimmed();
  
  AppendLogInternal(QString("正在连接到服务器: %1").arg(server_url), "info");
  controller_->ConnectToSignalServer(server_url.toStdString(), client_id.toStdString());
  
  connect_button_->setEnabled(false);
}

void VideoCallWindow::OnDisconnectClicked() {
  controller_->DisconnectFromSignalServer();
  AppendLogInternal("已断开连接", "info");
}

void VideoCallWindow::OnUserItemDoubleClicked(QListWidgetItem* item) {
  if (!item || !is_connected_) {
    return;
  }
  
  QString target_id = item->data(Qt::UserRole).toString();
  current_peer_id_ = target_id;
  
  AppendLogInternal(QString("准备呼叫用户: %1").arg(target_id), "info");
  controller_->StartCall(target_id.toStdString());
}

void VideoCallWindow::OnCallButtonClicked() {
  QListWidgetItem* selected = user_list_->currentItem();
  if (!selected) {
    QMessageBox::information(this, "提示", "请先选择要呼叫的用户");
    return;
  }
  
  OnUserItemDoubleClicked(selected);
}

void VideoCallWindow::OnHangupButtonClicked() {
  controller_->EndCall();
  AppendLogInternal("通话已挂断", "info");
}

void VideoCallWindow::OnUpdateStatsTimer() {
  RtcStatsSnapshot stats = controller_->GetLatestRtcStats();
  if (controller_->IsInCall()) {
    CallState state = controller_->GetCallState();
    call_info_label_->setText(GetCallStateString(state));
  } else {
    call_info_label_->setText(GetCallStateString(CallState::Idle));
    stats.valid = false;
  }
  UpdateStatsUI(stats);
}

// ============================================================================
// 辅助方法
// ============================================================================

void VideoCallWindow::UpdateUIState() {
  bool can_call = is_connected_ && !controller_->IsInCall() && user_list_->currentItem() != nullptr;
  call_button_->setEnabled(can_call);
  UpdateCallButtonState();
}

void VideoCallWindow::UpdateCallButtonState() {
  bool in_call = controller_->IsInCall();
  hangup_button_->setEnabled(in_call);
  
  bool can_call = is_connected_ && !in_call && user_list_->currentItem() != nullptr;
  call_button_->setEnabled(can_call);
}

QString VideoCallWindow::GetCallStateString(CallState state) const {
  switch (state) {
    case CallState::Idle: return "空闲";
    case CallState::Calling: return "呼叫中...";
    case CallState::Receiving: return "来电中...";
    case CallState::Connecting: return "建立连接...";
    case CallState::Connected: return "通话中";
    case CallState::Ending: return "结束中...";
    default: return "未知状态";
  }
}

void VideoCallWindow::AppendLogInternal(const QString& message, const QString& level) {
  const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
  QString level_text = "INFO";
  if (level == "error") {
    level_text = "ERROR";
  } else if (level == "warning") {
    level_text = "WARN";
  } else if (level == "success") {
    level_text = "OK";
  }

  log_text_->append(QString("[%1] [%2] %3").arg(timestamp, level_text, message));

  QTextCursor cursor = log_text_->textCursor();
  cursor.movePosition(QTextCursor::End);
  log_text_->setTextCursor(cursor);
}

void VideoCallWindow::UpdateStatsUI(const RtcStatsSnapshot& stats) {
  if (!stats_group_) {
    return;
  }

  auto set_value = [](QLabel* label, const QString& text) {
    if (label) {
      label->setText(text);
    }
  };

  QString ice_text = QString::fromStdString(stats.ice_state);
  if (ice_text.isEmpty()) {
    ice_text = "—";
  }
  set_value(stats_ice_state_value_, ice_text);

  if (!stats.valid) {
    set_value(stats_timestamp_value_, "—");
    set_value(stats_outbound_bitrate_value_, "—");
    set_value(stats_inbound_bitrate_value_, "—");
    set_value(stats_rtt_value_, "—");
    set_value(stats_audio_jitter_value_, "—");
    set_value(stats_audio_loss_value_, "—");
    set_value(stats_video_loss_value_, "—");
    set_value(stats_video_fps_value_, "—");
    set_value(stats_video_resolution_value_, "—");
    return;
  }

  set_value(stats_timestamp_value_, FormatTimestamp(stats.timestamp_ms));
  set_value(stats_outbound_bitrate_value_, FormatBitrate(stats.outbound_bitrate_kbps));
  set_value(stats_inbound_bitrate_value_, FormatBitrate(stats.inbound_bitrate_kbps));
  set_value(stats_rtt_value_, FormatDouble(stats.current_rtt_ms, 1) + " ms");
  set_value(stats_audio_jitter_value_, FormatDouble(stats.inbound_audio_jitter_ms, 1) + " ms");
  set_value(stats_audio_loss_value_, FormatPercentage(stats.inbound_audio_packet_loss_percent));
  set_value(stats_video_loss_value_, FormatPercentage(stats.inbound_video_packet_loss_percent));
  set_value(stats_video_fps_value_, FormatDouble(stats.inbound_video_fps, 1) + " fps");
  set_value(stats_video_resolution_value_, FormatResolution(stats.inbound_video_width, stats.inbound_video_height));
}

QString VideoCallWindow::FormatBitrate(double kbps) const {
  if (!std::isfinite(kbps) || kbps <= 0.0) {
    return "—";
  }
  if (kbps >= 1000.0) {
    return QString("%1 Mbps").arg(FormatDouble(kbps / 1000.0, 2));
  }
  return QString("%1 kbps").arg(FormatDouble(kbps, 1));
}

QString VideoCallWindow::FormatPercentage(double value) const {
  if (!std::isfinite(value) || value < 0.0) {
    return "—";
  }
  return QString("%1 %").arg(FormatDouble(value, 2));
}

QString VideoCallWindow::FormatDouble(double value, int precision) const {
  if (!std::isfinite(value)) {
    return "—";
  }
  QString text = QString::number(value, 'f', precision);
  if (precision > 0) {
    while (text.contains('.') && text.endsWith('0')) {
      text.chop(1);
    }
    if (text.endsWith('.')) {
      text.chop(1);
    }
  }
  return text;
}

QString VideoCallWindow::FormatResolution(int width, int height) const {
  if (width <= 0 || height <= 0) {
    return "—";
  }
  return QString("%1x%2").arg(width).arg(height);
}

QString VideoCallWindow::FormatTimestamp(uint64_t timestamp_ms) const {
  if (timestamp_ms == 0) {
    return "—";
  }
  QDateTime dt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_ms), Qt::UTC).toLocalTime();
  return dt.toString("HH:mm:ss");
}

void VideoCallWindow::LayoutLocalVideo() {
  if (!local_renderer_ || !video_panel_) {
    return;
  }
  
  // 将本地视频放在右下角
  int margin = 10;
  int x = video_panel_->width() - local_renderer_->width() - margin;
  int y = video_panel_->height() - local_renderer_->height() - margin;
  
  local_renderer_->move(x, y);
}

void VideoCallWindow::closeEvent(QCloseEvent* event) {
  // 停止所有视频渲染器
  if (local_renderer_) {
    local_renderer_->Stop();
  }
  if (remote_renderer_) {
    remote_renderer_->Stop();
  }
  
  // 如果正在通话，先挂断
  if (controller_->IsInCall()) {
    controller_->EndCall();
  }
  
  // 断开信令连接
  if (controller_->IsConnectedToSignalServer()) {
    controller_->DisconnectFromSignalServer();
  }
  
  // 关闭协调器
  controller_->Shutdown();
  
  event->accept();
}

void VideoCallWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  LayoutLocalVideo();
}
