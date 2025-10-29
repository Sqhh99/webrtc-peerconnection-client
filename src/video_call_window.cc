#include "video_call_window.h"
#include "videorenderer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QGraphicsDropShadowEffect>

VideoCallWindow::VideoCallWindow(ICallController* controller, QWidget* parent)
    : QMainWindow(parent),
      controller_(controller),
      is_connected_(false) {
  setWindowTitle("WebRTC Video Call Client");
  resize(1200, 800);
  
  // 设置全局样式
  setStyleSheet(R"(
    QMainWindow {
      background-color: #f5f7fa;
    }
    QWidget {
      font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
      font-size: 13px;
    }
    QGroupBox {
      background-color: white;
      border: 1px solid #e4e7ed;
      border-radius: 6px;
      margin-top: 12px;
      padding-top: 10px;
      font-weight: bold;
      color: #303133;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 15px;
      padding: 0 5px;
      background-color: white;
    }
    QPushButton {
      background-color: #409eff;
      color: white;
      border: none;
      border-radius: 4px;
      padding: 8px 16px;
      font-weight: 500;
      min-height: 32px;
    }
    QPushButton:hover {
      background-color: #66b1ff;
    }
    QPushButton:pressed {
      background-color: #3a8ee6;
    }
    QPushButton:disabled {
      background-color: #c0c4cc;
      color: #909399;
    }
    QLineEdit {
      background-color: white;
      border: 1px solid #dcdfe6;
      border-radius: 4px;
      padding: 6px 12px;
      color: #606266;
    }
    QLineEdit:focus {
      border-color: #409eff;
    }
    QListWidget {
      background-color: white;
      border: 1px solid #e4e7ed;
      border-radius: 4px;
      padding: 4px;
      outline: none;
    }
    QListWidget::item {
      padding: 8px 12px;
      border-radius: 4px;
      color: #606266;
    }
    QListWidget::item:hover {
      background-color: #f5f7fa;
    }
    QListWidget::item:selected {
      background-color: #ecf5ff;
      color: #409eff;
    }
    QTextEdit {
      background-color: white;
      border: 1px solid #e4e7ed;
      border-radius: 4px;
      padding: 8px;
      color: #606266;
    }
    QLabel {
      color: #606266;
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
      local_renderer_->setFixedSize(240, 180);
      local_renderer_->setStyleSheet(R"(
        QLabel {
          border: 3px solid white;
          border-radius: 8px;
          background-color: #2c3e50;
        }
      )");
      // 添加阴影效果
      QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect();
      shadow->setBlurRadius(15);
      shadow->setColor(QColor(0, 0, 0, 120));
      shadow->setOffset(0, 4);
      local_renderer_->setGraphicsEffect(shadow);
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
      call_status_label_->setText("等待视频连接...");
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
    
    connection_status_label_->setText(QString("● 已连接 [%1]").arg(qclient_id));
    connection_status_label_->setStyleSheet(R"(
      QLabel { 
        color: #67c23a; 
        font-weight: 600;
        font-size: 14px;
        border: none;
        padding: 0 10px;
      }
    )");
    
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
    
    connection_status_label_->setText("● 未连接");
    connection_status_label_->setStyleSheet(R"(
      QLabel { 
        color: #f56c6c; 
        font-weight: 600;
        font-size: 14px;
        border: none;
        padding: 0 10px;
      }
    )");
    
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
      call_status_label_->setText("等待视频连接...");
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
  main_layout->setContentsMargins(10, 10, 10, 10);
  main_layout->setSpacing(10);
  
  // 创建连接面板（顶部）
  CreateConnectionPanel();
  main_layout->addWidget(connection_panel_);
  
  // 创建主分割器（左右布局）
  main_splitter_ = new QSplitter(Qt::Horizontal, this);
  main_splitter_->setHandleWidth(1);
  main_splitter_->setStyleSheet(R"(
    QSplitter::handle {
      background-color: #e4e7ed;
    }
  )");
  
  // 左侧：用户列表
  CreateUserListPanel();
  main_splitter_->addWidget(user_list_group_);
  
  // 右侧：视频和日志的垂直分割
  right_splitter_ = new QSplitter(Qt::Vertical, this);
  right_splitter_->setHandleWidth(1);
  right_splitter_->setStyleSheet(R"(
    QSplitter::handle {
      background-color: #e4e7ed;
    }
  )");
  
  // 视频面板
  CreateVideoPanel();
  right_splitter_->addWidget(video_panel_);
  
  // 日志面板
  CreateLogPanel();
  right_splitter_->addWidget(log_group_);
  
  right_splitter_->setStretchFactor(0, 3);  // 视频占3份
  right_splitter_->setStretchFactor(1, 1);  // 日志占1份
  
  main_splitter_->addWidget(right_splitter_);
  main_splitter_->setStretchFactor(0, 1);  // 用户列表占1份
  main_splitter_->setStretchFactor(1, 4);  // 右侧占4份
  
  main_layout->addWidget(main_splitter_);
  
  // 创建控制面板（底部）
  CreateControlPanel();
  main_layout->addWidget(control_panel_);
}

void VideoCallWindow::CreateConnectionPanel() {
  connection_panel_ = new QWidget(this);
  connection_panel_->setStyleSheet(R"(
    QWidget {
      background-color: white;
      border-radius: 6px;
      border: 1px solid #e4e7ed;
    }
  )");
  
  QHBoxLayout* layout = new QHBoxLayout(connection_panel_);
  layout->setContentsMargins(15, 12, 15, 12);
  layout->setSpacing(12);
  
  QLabel* server_label = new QLabel("信令服务器:");
  server_label->setStyleSheet("QLabel { color: #606266; font-weight: 500; border: none; }");
  layout->addWidget(server_label);
  
  server_url_edit_ = new QLineEdit("ws://localhost:8081/ws/webrtc");
  server_url_edit_->setMinimumWidth(280);
  server_url_edit_->setStyleSheet(R"(
    QLineEdit {
      background-color: #f5f7fa;
      border: 1px solid #dcdfe6;
      border-radius: 4px;
      padding: 8px 12px;
      color: #303133;
    }
    QLineEdit:focus {
      border-color: #409eff;
      background-color: white;
    }
  )");
  layout->addWidget(server_url_edit_);
  
  QLabel* id_label = new QLabel("客户端ID:");
  id_label->setStyleSheet("QLabel { color: #606266; font-weight: 500; border: none; }");
  layout->addWidget(id_label);
  
  client_id_edit_ = new QLineEdit();
  client_id_edit_->setPlaceholderText("自动生成");
  client_id_edit_->setMaximumWidth(150);
  client_id_edit_->setStyleSheet(R"(
    QLineEdit {
      background-color: #f5f7fa;
      border: 1px solid #dcdfe6;
      border-radius: 4px;
      padding: 8px 12px;
      color: #303133;
    }
    QLineEdit:focus {
      border-color: #409eff;
      background-color: white;
    }
  )");
  layout->addWidget(client_id_edit_);
  
  connect_button_ = new QPushButton("连接");
  connect_button_->setMaximumWidth(90);
  connect_button_->setStyleSheet(R"(
    QPushButton {
      background-color: #67c23a;
      color: white;
      border: none;
      border-radius: 4px;
      padding: 8px 20px;
      font-weight: 600;
      min-height: 36px;
    }
    QPushButton:hover {
      background-color: #85ce61;
    }
    QPushButton:pressed {
      background-color: #5daf34;
    }
    QPushButton:disabled {
      background-color: #c0c4cc;
    }
  )");
  connect(connect_button_, &QPushButton::clicked, this, &VideoCallWindow::OnConnectClicked);
  layout->addWidget(connect_button_);
  
  disconnect_button_ = new QPushButton("断开");
  disconnect_button_->setMaximumWidth(90);
  disconnect_button_->setEnabled(false);
  disconnect_button_->setStyleSheet(R"(
    QPushButton {
      background-color: #909399;
      color: white;
      border: none;
      border-radius: 4px;
      padding: 8px 20px;
      font-weight: 600;
      min-height: 36px;
    }
    QPushButton:hover {
      background-color: #a6a9ad;
    }
    QPushButton:pressed {
      background-color: #82848a;
    }
    QPushButton:disabled {
      background-color: #c0c4cc;
    }
  )");
  connect(disconnect_button_, &QPushButton::clicked, this, &VideoCallWindow::OnDisconnectClicked);
  layout->addWidget(disconnect_button_);
  
  connection_status_label_ = new QLabel("● 未连接");
  connection_status_label_->setStyleSheet(R"(
    QLabel { 
      color: #f56c6c; 
      font-weight: 600;
      font-size: 14px;
      border: none;
      padding: 0 10px;
    }
  )");
  layout->addWidget(connection_status_label_);
  
  layout->addStretch();
}

void VideoCallWindow::CreateUserListPanel() {
  user_list_group_ = new QGroupBox("在线用户", this);
  user_list_group_->setStyleSheet(R"(
    QGroupBox {
      background-color: white;
      border: 1px solid #e4e7ed;
      border-radius: 6px;
      margin-top: 12px;
      padding-top: 15px;
      font-weight: 600;
      color: #303133;
      font-size: 14px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 15px;
      padding: 0 8px;
      background-color: white;
    }
  )");
  
  QVBoxLayout* layout = new QVBoxLayout(user_list_group_);
  layout->setContentsMargins(10, 20, 10, 10);
  layout->setSpacing(8);
  
  user_list_ = new QListWidget();
  user_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  user_list_->setStyleSheet(R"(
    QListWidget {
      background-color: #fafafa;
      border: 1px solid #e4e7ed;
      border-radius: 4px;
      padding: 6px;
      outline: none;
    }
    QListWidget::item {
      padding: 12px 15px;
      border-radius: 4px;
      color: #303133;
      margin: 2px 0;
    }
    QListWidget::item:hover {
      background-color: #f0f9ff;
      color: #409eff;
    }
    QListWidget::item:selected {
      background-color: #ecf5ff;
      color: #409eff;
      font-weight: 600;
    }
  )");
  connect(user_list_, &QListWidget::itemDoubleClicked,
          this, &VideoCallWindow::OnUserItemDoubleClicked);
  layout->addWidget(user_list_);
  
  QLabel* hint_label = new QLabel("💡 双击用户发起通话");
  hint_label->setStyleSheet(R"(
    QLabel { 
      color: #909399; 
      font-size: 12px; 
      padding: 5px 0;
      border: none;
    }
  )");
  layout->addWidget(hint_label);
}

void VideoCallWindow::CreateLogPanel() {
  log_group_ = new QGroupBox("系统日志", this);
  log_group_->setStyleSheet(R"(
    QGroupBox {
      background-color: white;
      border: 1px solid #e4e7ed;
      border-radius: 6px;
      margin-top: 12px;
      padding-top: 15px;
      font-weight: 600;
      color: #303133;
      font-size: 14px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      left: 15px;
      padding: 0 8px;
      background-color: white;
    }
  )");
  
  QVBoxLayout* layout = new QVBoxLayout(log_group_);
  layout->setContentsMargins(10, 20, 10, 10);
  layout->setSpacing(8);
  
  log_text_ = new QTextEdit();
  log_text_->setReadOnly(true);
  log_text_->setMaximumHeight(150);
  log_text_->setStyleSheet(R"(
    QTextEdit {
      background-color: #fafafa;
      border: 1px solid #e4e7ed;
      border-radius: 4px;
      padding: 10px;
      color: #606266;
      font-family: "Consolas", "Monaco", monospace;
      font-size: 12px;
    }
  )");
  layout->addWidget(log_text_);
  
  QPushButton* clear_log_btn = new QPushButton("🗑 清空日志");
  clear_log_btn->setMaximumWidth(120);
  clear_log_btn->setStyleSheet(R"(
    QPushButton {
      background-color: #f5f7fa;
      color: #606266;
      border: 1px solid #dcdfe6;
      border-radius: 4px;
      padding: 6px 12px;
      font-weight: 500;
      min-height: 30px;
    }
    QPushButton:hover {
      background-color: #ecf5ff;
      color: #409eff;
      border-color: #c6e2ff;
    }
    QPushButton:pressed {
      background-color: #e0eeff;
    }
  )");
  connect(clear_log_btn, &QPushButton::clicked, log_text_, &QTextEdit::clear);
  layout->addWidget(clear_log_btn);
}

void VideoCallWindow::CreateVideoPanel() {
  video_panel_ = new QWidget(this);
  video_panel_->setStyleSheet(R"(
    QWidget { 
      background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                  stop:0 #2c3e50, stop:1 #34495e);
      border-radius: 6px;
    }
  )");
  video_panel_->setMinimumHeight(400);
  
  QVBoxLayout* layout = new QVBoxLayout(video_panel_);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  
  // 远程视频（主视频）
  remote_renderer_ = std::make_unique<VideoRenderer>(video_panel_);
  remote_renderer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  remote_renderer_->setStyleSheet("QLabel { border-radius: 6px; }");
  layout->addWidget(remote_renderer_.get());
  remote_renderer_->hide();
  
  // 无视频提示标签
  call_status_label_ = new QLabel("📹 等待视频连接...", video_panel_);
  call_status_label_->setAlignment(Qt::AlignCenter);
  call_status_label_->setStyleSheet(R"(
    QLabel { 
      color: #ecf0f1; 
      font-size: 20px;
      font-weight: 500;
      background: transparent;
      border: none;
    }
  )");
  layout->addWidget(call_status_label_);
}

void VideoCallWindow::CreateControlPanel() {
  control_panel_ = new QWidget(this);
  control_panel_->setStyleSheet(R"(
    QWidget {
      background-color: white;
      border-radius: 6px;
      border: 1px solid #e4e7ed;
    }
  )");
  
  QHBoxLayout* layout = new QHBoxLayout(control_panel_);
  layout->setContentsMargins(15, 12, 15, 12);
  layout->setSpacing(12);
  
  call_button_ = new QPushButton("📞 呼叫");
  call_button_->setEnabled(false);
  call_button_->setMinimumHeight(45);
  call_button_->setMinimumWidth(120);
  call_button_->setStyleSheet(R"(
    QPushButton { 
      font-size: 15px; 
      font-weight: 600;
      background-color: #67c23a;
      color: white;
      border: none;
      border-radius: 6px;
      padding: 10px 24px;
    }
    QPushButton:hover {
      background-color: #85ce61;
    }
    QPushButton:pressed {
      background-color: #5daf34;
    }
    QPushButton:disabled {
      background-color: #e4e7ed;
      color: #c0c4cc;
    }
  )");
  connect(call_button_, &QPushButton::clicked, this, &VideoCallWindow::OnCallButtonClicked);
  layout->addWidget(call_button_);
  
  hangup_button_ = new QPushButton("📵 挂断");
  hangup_button_->setEnabled(false);
  hangup_button_->setMinimumHeight(45);
  hangup_button_->setMinimumWidth(120);
  hangup_button_->setStyleSheet(R"(
    QPushButton { 
      font-size: 15px; 
      font-weight: 600; 
      background-color: #f56c6c; 
      color: white;
      border: none;
      border-radius: 6px;
      padding: 10px 24px;
    }
    QPushButton:hover {
      background-color: #f78989;
    }
    QPushButton:pressed {
      background-color: #dd6161;
    }
    QPushButton:disabled {
      background-color: #e4e7ed;
      color: #c0c4cc;
    }
  )");
  connect(hangup_button_, &QPushButton::clicked, this, &VideoCallWindow::OnHangupButtonClicked);
  layout->addWidget(hangup_button_);
  
  call_info_label_ = new QLabel("⚪ 空闲");
  call_info_label_->setStyleSheet(R"(
    QLabel { 
      font-size: 15px; 
      font-weight: 600;
      color: #909399;
      border: none;
      padding: 0 15px;
    }
  )");
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
  if (controller_->IsInCall()) {
    CallState state = controller_->GetCallState();
    call_info_label_->setText(GetCallStateString(state));
  } else {
    call_info_label_->setText("空闲");
  }
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
    case CallState::Idle: return "⚪ 空闲";
    case CallState::Calling: return "📞 呼叫中...";
    case CallState::Receiving: return "📲 来电中...";
    case CallState::Connecting: return "🔄 连接中...";
    case CallState::Connected: return "🟢 通话中";
    case CallState::Ending: return "📵 结束中...";
    default: return "❓ 未知";
  }
}

void VideoCallWindow::AppendLogInternal(const QString& message, const QString& level) {
  QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
  QString color;
  QString icon;
  
  if (level == "error") {
    color = "#f56c6c";
    icon = "❌";
  } else if (level == "warning") {
    color = "#e6a23c";
    icon = "⚠️";
  } else if (level == "success") {
    color = "#67c23a";
    icon = "✅";
  } else {
    color = "#409eff";
    icon = "ℹ️";
  }
  
  QString html = QString("<span style='color: #909399; font-weight: 600;'>[%1]</span> "
                        "<span style='color: %2; font-weight: 600;'>%3</span> "
                        "<span style='color: #606266;'>%4</span>")
                 .arg(timestamp, color, icon, message);
  
  log_text_->append(html);
  
  // 自动滚动到底部
  QTextCursor cursor = log_text_->textCursor();
  cursor.movePosition(QTextCursor::End);
  log_text_->setTextCursor(cursor);
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
