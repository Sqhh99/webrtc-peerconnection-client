/*
 *  CallCoordinator - 业务协调器（重构版Conductor）
 *  职责：协调WebRTC引擎、信令客户端和呼叫管理器
 *  优点：通过接口与UI解耦，提升可复用性和可测试性
 */

#include "call_coordinator.h"
#include "rtc_base/logging.h"
#include <QMetaObject>
#include <QJsonDocument>

namespace {

QJsonObject ExtractSdpPayload(const QJsonObject& payload) {
  if (payload.contains("sdp")) {
    const auto sdp_value = payload.value("sdp");
    if (sdp_value.isObject()) {
      return sdp_value.toObject();
    }
    if (sdp_value.isString()) {
      QJsonObject converted;
      converted["type"] = payload.value("type").toString();
      converted["sdp"] = sdp_value.toString();
      return converted;
    }
  }
  return payload;
}

QString ExtractSdpText(const QJsonObject& payload) {
  if (payload.contains("sdp")) {
    const auto sdp_value = payload.value("sdp");
    if (sdp_value.isString()) {
      return sdp_value.toString();
    }
    if (sdp_value.isObject()) {
      return sdp_value.toObject().value("sdp").toString();
    }
  }
  return QString();
}

QJsonObject ExtractCandidatePayload(const QJsonObject& payload) {
  if (payload.contains("candidate")) {
    const auto candidate_value = payload.value("candidate");
    if (candidate_value.isObject()) {
      return candidate_value.toObject();
    }
  }
  return payload;
}

int ExtractMLineIndex(const QJsonObject& candidate) {
  if (candidate.contains("sdpMLineIndex")) {
    return candidate.value("sdpMLineIndex").toInt();
  }
  if (candidate.contains("sdpMlineIndex")) {
    return candidate.value("sdpMlineIndex").toInt();
  }
  return -1;
}

}  // namespace

// ============================================================================
// 构造和析构
// ============================================================================

CallCoordinator::CallCoordinator(const webrtc::Environment& env)
    : env_(env), ui_observer_(nullptr), is_caller_(false) {
  // 创建WebRTC引擎
  webrtc_engine_ = std::make_unique<WebRTCEngine>(env);
  webrtc_engine_->SetObserver(this);
  
  // 创建信令客户端
  signal_client_ = std::make_unique<SignalClient>(nullptr);
  
  // 创建呼叫管理器
  call_manager_ = std::make_unique<CallManager>(nullptr);
  call_manager_->SetSignalClient(signal_client_.get());
}

CallCoordinator::~CallCoordinator() {
  Shutdown();
}

// ============================================================================
// 公有方法
// ============================================================================

void CallCoordinator::SetUIObserver(ICallUIObserver* ui_observer) {
  ui_observer_ = ui_observer;
}

bool CallCoordinator::Initialize() {
  RTC_LOG(LS_INFO) << "Initializing CallCoordinator...";
  
  // 注册为信令客户端的观察者
  signal_client_->RegisterObserver(this);
  
  // 注册为呼叫管理器的观察者
  call_manager_->RegisterObserver(this);
  
  // 连接呼叫管理器的Qt信号（用于UI通知）
  QObject::connect(call_manager_.get(), &CallManager::CallStateChanged,
    [this](CallState state, const QString& peer_id) {
      OnCallStateChanged(state, peer_id.toStdString());
    });
  
  // 注意：不需要连接IncomingCall信号，因为已经通过observer回调处理
  // CallManager会调用observer_->OnIncomingCall()
  
  return webrtc_engine_->Initialize();
}

void CallCoordinator::Shutdown() {
  if (webrtc_engine_) {
    webrtc_engine_->Shutdown();
  }
  if (signal_client_) {
    signal_client_->Disconnect();
  }
  current_peer_id_.clear();
}

void CallCoordinator::ConnectToSignalServer(const std::string& url, const std::string& client_id) {
  if (signal_client_) {
    signal_client_->Connect(QString::fromStdString(url), QString::fromStdString(client_id));
  }
}

void CallCoordinator::DisconnectFromSignalServer() {
  if (signal_client_) {
    signal_client_->Disconnect();
  }
}

void CallCoordinator::StartCall(const std::string& peer_id) {
  if (call_manager_) {
    call_manager_->InitiateCall(QString::fromStdString(peer_id));
  }
}

void CallCoordinator::AcceptCall() {
  if (call_manager_) {
    call_manager_->AcceptCall();
  }
}

void CallCoordinator::RejectCall(const std::string& reason) {
  if (call_manager_) {
    call_manager_->RejectCall(QString::fromStdString(reason));
  }
}

void CallCoordinator::EndCall() {
  if (call_manager_) {
    call_manager_->EndCall();
  }
}

bool CallCoordinator::IsConnectedToSignalServer() const {
  return signal_client_ && signal_client_->IsConnected();
}

bool CallCoordinator::IsInCall() const {
  return call_manager_ && call_manager_->IsInCall();
}

CallState CallCoordinator::GetCallState() const {
  return call_manager_ ? call_manager_->GetCallState() : CallState::Idle;
}

std::string CallCoordinator::GetCurrentPeerId() const {
  return current_peer_id_;
}

std::string CallCoordinator::GetClientId() const {
  return signal_client_ ? signal_client_->GetClientId().toStdString() : "";
}

// ============================================================================
// WebRTCEngineObserver 实现 - 处理WebRTC引擎的回调
// ============================================================================

void CallCoordinator::OnLocalVideoTrackAdded(webrtc::VideoTrackInterface* track) {
  RTC_LOG(LS_INFO) << "Local video track added";
  if (ui_observer_) {
    ui_observer_->OnStartLocalRenderer(track);
  }
}

void CallCoordinator::OnRemoteVideoTrackAdded(webrtc::VideoTrackInterface* track) {
  RTC_LOG(LS_INFO) << "Remote video track added";
  if (ui_observer_) {
    ui_observer_->OnStartRemoteRenderer(track);
  }
}

void CallCoordinator::OnRemoteVideoTrackRemoved() {
  RTC_LOG(LS_INFO) << "Remote video track removed";
  if (ui_observer_) {
    ui_observer_->OnStopRemoteRenderer();
  }
}

void CallCoordinator::OnIceConnectionStateChanged(webrtc::PeerConnectionInterface::IceConnectionState state) {
  RTC_LOG(LS_INFO) << "ICE connection state changed: " << state;
  
  if (state == webrtc::PeerConnectionInterface::kIceConnectionConnected ||
      state == webrtc::PeerConnectionInterface::kIceConnectionCompleted) {
    if (call_manager_) {
      call_manager_->NotifyPeerConnectionEstablished();
    }
  } else if (state == webrtc::PeerConnectionInterface::kIceConnectionFailed ||
             state == webrtc::PeerConnectionInterface::kIceConnectionDisconnected ||
             state == webrtc::PeerConnectionInterface::kIceConnectionClosed) {
    if (ui_observer_) {
      ui_observer_->OnLogMessage("ICE连接已断开", "warning");
    }
  }
}

void CallCoordinator::OnOfferCreated(const std::string& sdp) {
  RTC_LOG(LS_INFO) << "Offer created, sending to peer...";
  qDebug() << "=== OnOfferCreated called ===" << "current_peer:" << QString::fromStdString(current_peer_id_);
  
  QJsonObject json_sdp;
  json_sdp["type"] = "offer";
  json_sdp["sdp"] = QString::fromStdString(sdp);
  
  if (signal_client_) {
    qDebug() << "Calling SendOffer to" << QString::fromStdString(current_peer_id_);
    
    // 注意：此回调可能在WebRTC线程中调用，需要切换到主线程发送WebSocket消息
    QString peer_id = QString::fromStdString(current_peer_id_);
    QMetaObject::invokeMethod(signal_client_.get(), [this, peer_id, json_sdp]() {
      signal_client_->SendOffer(peer_id, json_sdp);
    }, Qt::QueuedConnection);
  } else {
    qDebug() << "ERROR: signal_client_ is null!";
  }
}

void CallCoordinator::OnAnswerCreated(const std::string& sdp) {
  RTC_LOG(LS_INFO) << "Answer created, sending to peer...";
  qDebug() << "=== OnAnswerCreated called ===" << "current_peer:" << QString::fromStdString(current_peer_id_);
  
  QJsonObject json_sdp;
  json_sdp["type"] = "answer";
  json_sdp["sdp"] = QString::fromStdString(sdp);
  
  if (signal_client_) {
    qDebug() << "Calling SendAnswer to" << QString::fromStdString(current_peer_id_);
    
    // 注意：此回调可能在WebRTC线程中调用，需要切换到主线程发送WebSocket消息
    QString peer_id = QString::fromStdString(current_peer_id_);
    QMetaObject::invokeMethod(signal_client_.get(), [this, peer_id, json_sdp]() {
      signal_client_->SendAnswer(peer_id, json_sdp);
    }, Qt::QueuedConnection);
  } else {
    qDebug() << "ERROR: signal_client_ is null!";
  }
}

void CallCoordinator::OnIceCandidateGenerated(const std::string& sdp_mid, 
                                               int sdp_mline_index, 
                                               const std::string& candidate) {
  RTC_LOG(LS_INFO) << "ICE candidate generated: " << sdp_mline_index;
  
  QJsonObject json_candidate;
  json_candidate["sdpMid"] = QString::fromStdString(sdp_mid);
  json_candidate["sdpMLineIndex"] = sdp_mline_index;
  json_candidate["candidate"] = QString::fromStdString(candidate);
  
  if (signal_client_) {
    // 注意：此回调可能在WebRTC线程中调用，需要切换到主线程发送WebSocket消息
    QString peer_id = QString::fromStdString(current_peer_id_);
    QMetaObject::invokeMethod(signal_client_.get(), [this, peer_id, json_candidate]() {
      signal_client_->SendIceCandidate(peer_id, json_candidate);
    }, Qt::QueuedConnection);
  }
}

void CallCoordinator::OnError(const std::string& error) {
  RTC_LOG(LS_ERROR) << "WebRTC Engine error: " << error;
  if (ui_observer_) {
    ui_observer_->OnShowError("WebRTC错误", error);
  }
}

// ============================================================================
// SignalClientObserver 实现 - 处理信令消息
// ============================================================================

void CallCoordinator::OnConnected(const std::string& client_id) {
  RTC_LOG(LS_INFO) << "Connected to signaling server: " << client_id;
  if (ui_observer_) {
    ui_observer_->OnSignalConnected(client_id);
    ui_observer_->OnLogMessage("已连接到服务器，客户端ID: " + client_id, "success");
  }
}

void CallCoordinator::OnDisconnected() {
  RTC_LOG(LS_INFO) << "Disconnected from signaling server";
  if (ui_observer_) {
    ui_observer_->OnSignalDisconnected();
    ui_observer_->OnLogMessage("已断开与服务器的连接", "warning");
  }
}

void CallCoordinator::OnConnectionError(const std::string& error) {
  RTC_LOG(LS_ERROR) << "Signaling connection error: " << error;
  if (ui_observer_) {
    ui_observer_->OnSignalError(error);
    ui_observer_->OnLogMessage("连接错误: " + error, "error");
  }
}

void CallCoordinator::OnIceServersReceived(const std::vector<IceServerConfig>& ice_servers) {
  RTC_LOG(LS_INFO) << "Received " << ice_servers.size() << " ICE server configurations";
  
  ice_servers_ = ice_servers;
  webrtc_engine_->SetIceServers(ice_servers);
  
  if (ui_observer_) {
    std::string log_msg = "接收到 " + std::to_string(ice_servers.size()) + " 个 ICE 服务器配置:";
    for (const auto& server : ice_servers) {
      for (const auto& url : server.urls) {
        log_msg += "\n  - " + url;
        if (!server.username.empty()) {
          log_msg += " (认证)";
        }
      }
    }
    ui_observer_->OnLogMessage(log_msg, "info");
  }
}

void CallCoordinator::OnClientListUpdate(const QJsonArray& clients) {
  RTC_LOG(LS_INFO) << "Client list updated: " << clients.size() << " clients";
  if (ui_observer_) {
    ui_observer_->OnClientListUpdate(clients);
  }
}

void CallCoordinator::OnUserOffline(const std::string& client_id) {
  RTC_LOG(LS_INFO) << "User offline: " << client_id;
  if (client_id == current_peer_id_ && call_manager_) {
    call_manager_->EndCall();
  }
}

void CallCoordinator::OnCallRequest(const std::string& from, const QJsonObject& payload) {
  RTC_LOG(LS_INFO) << "Call request from: " << from;
  if (call_manager_) {
    call_manager_->HandleCallRequest(QString::fromStdString(from));
  }
}

void CallCoordinator::OnCallResponse(const std::string& from, bool accepted, const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call response from: " << from << " accepted: " << accepted;
  if (call_manager_) {
    call_manager_->HandleCallResponse(
        QString::fromStdString(from), 
        accepted, 
        QString::fromStdString(reason));
  }
}

void CallCoordinator::OnCallCancel(const std::string& from, const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call cancelled by: " << from;
  if (call_manager_) {
    call_manager_->HandleCallCancel(
        QString::fromStdString(from), 
        QString::fromStdString(reason));
  }
}

void CallCoordinator::OnCallEnd(const std::string& from, const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call ended by: " << from;
  if (call_manager_) {
    call_manager_->HandleCallEnd(
        QString::fromStdString(from), 
        QString::fromStdString(reason));
  }
}

void CallCoordinator::OnOffer(const std::string& from, const QJsonObject& sdp) {
  RTC_LOG(LS_INFO) << "Received offer from: " << from;
  ProcessOffer(from, sdp);
}

void CallCoordinator::OnAnswer(const std::string& from, const QJsonObject& sdp) {
  RTC_LOG(LS_INFO) << "Received answer from: " << from;
  ProcessAnswer(from, sdp);
}

void CallCoordinator::OnIceCandidate(const std::string& from, const QJsonObject& candidate) {
  RTC_LOG(LS_INFO) << "Received ICE candidate from: " << from;
  ProcessIceCandidate(from, candidate);
}

// ============================================================================
// CallManagerObserver 实现 - 处理呼叫流程
// ============================================================================

void CallCoordinator::OnCallStateChanged(CallState state, const std::string& peer_id) {
  RTC_LOG(LS_INFO) << "Call state changed: " << static_cast<int>(state);
  if (ui_observer_) {
    ui_observer_->OnCallStateChanged(state, peer_id);
  }
}

void CallCoordinator::OnIncomingCall(const std::string& caller_id) {
  RTC_LOG(LS_INFO) << "Incoming call from: " << caller_id;
  if (ui_observer_) {
    ui_observer_->OnIncomingCall(caller_id);
  }
}

void CallCoordinator::OnCallAccepted(const std::string& peer_id) {
  RTC_LOG(LS_INFO) << "Call accepted by: " << peer_id;
}

void CallCoordinator::OnCallRejected(const std::string& peer_id, const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call rejected by: " << peer_id << " reason: " << reason;
  
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnCallCancelled(const std::string& peer_id, const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call cancelled by: " << peer_id << " reason: " << reason;
  
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnCallEnded(const std::string& peer_id, const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call ended with: " << peer_id << " reason: " << reason;
  
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnCallTimeout() {
  RTC_LOG(LS_INFO) << "Call timeout";
  
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnNeedCreatePeerConnection(const std::string& peer_id, bool is_caller) {
  RTC_LOG(LS_INFO) << "Need create peer connection with: " << peer_id << " is_caller: " << is_caller;
  qDebug() << "=== OnNeedCreatePeerConnection ===" << "peer:" << QString::fromStdString(peer_id) << "is_caller:" << is_caller;
  
  current_peer_id_ = peer_id;
  is_caller_ = is_caller;
  
  if (!webrtc_engine_->HasPeerConnection()) {
    qDebug() << "Creating PeerConnection...";
    if (webrtc_engine_->CreatePeerConnection()) {
      qDebug() << "PeerConnection created successfully, adding tracks...";
      webrtc_engine_->AddTracks();
      
      if (is_caller) {
        qDebug() << "Caller side - calling CreateOffer()";
        webrtc_engine_->CreateOffer();
        qDebug() << "CreateOffer() returned";
      } else {
        qDebug() << "Callee side - waiting for offer";
      }
    } else {
      RTC_LOG(LS_ERROR) << "Failed to create peer connection";
      qDebug() << "ERROR: Failed to create peer connection";
      if (ui_observer_) {
        ui_observer_->OnShowError("错误", "创建连接失败");
      }
    }
  } else {
    qDebug() << "PeerConnection already exists!";
  }
}

void CallCoordinator::OnNeedClosePeerConnection() {
  RTC_LOG(LS_INFO) << "Need close peer connection";
  
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

// ============================================================================
// 私有方法 - 处理信令消息的详细逻辑
// ============================================================================

void CallCoordinator::ProcessOffer(const std::string& from, const QJsonObject& sdp) {
  RTC_LOG(LS_INFO) << "Processing offer from: " << from;
  qDebug() << "=== ProcessOffer called ===" << "from:" << QString::fromStdString(from);
  
  if (!webrtc_engine_->HasPeerConnection()) {
    RTC_LOG(LS_ERROR) << "No peer connection exists when processing offer!";
    qDebug() << "ERROR: No peer connection exists!";
    if (ui_observer_) {
      ui_observer_->OnLogMessage("错误: 收到offer但没有PeerConnection", "error");
    }
    return;
  }
  
  qDebug() << "PeerConnection exists, setting current_peer and processing offer";
  current_peer_id_ = from;
  is_caller_ = false;
  
  if (ui_observer_) {
    ui_observer_->OnLogMessage("正在处理来自 " + from + " 的offer", "info");
  }
  
  const QJsonObject sdp_payload = ExtractSdpPayload(sdp);
  const QString sdp_text = ExtractSdpText(sdp_payload);
  if (sdp_text.isEmpty()) {
    RTC_LOG(LS_ERROR) << "Offer payload missing SDP text";
    qDebug() << "ERROR: Offer payload missing SDP text";
    return;
  }

  std::string sdp_str = sdp_text.toStdString();
  qDebug() << "Calling SetRemoteOffer...";
  webrtc_engine_->SetRemoteOffer(sdp_str);
  qDebug() << "Calling CreateAnswer...";
  webrtc_engine_->CreateAnswer();
  qDebug() << "CreateAnswer returned";
}

void CallCoordinator::ProcessAnswer(const std::string& from, const QJsonObject& sdp) {
  RTC_LOG(LS_INFO) << "Processing answer from: " << from;
  
  const QJsonObject sdp_payload = ExtractSdpPayload(sdp);
  const QString sdp_text = ExtractSdpText(sdp_payload);
  if (sdp_text.isEmpty()) {
    RTC_LOG(LS_ERROR) << "Answer payload missing SDP text";
    qDebug() << "ERROR: Answer payload missing SDP text";
    return;
  }

  std::string sdp_str = sdp_text.toStdString();
  webrtc_engine_->SetRemoteAnswer(sdp_str);
}

void CallCoordinator::ProcessIceCandidate(const std::string& from, const QJsonObject& candidate) {
  RTC_LOG(LS_INFO) << "Processing ICE candidate from: " << from;
  
  const QJsonObject candidate_payload = ExtractCandidatePayload(candidate);
  const QString sdp_mid_value = candidate_payload.value("sdpMid").toString();
  const int sdp_mline_index = ExtractMLineIndex(candidate_payload);
  const QString candidate_text = candidate_payload.value("candidate").toString();

  if (sdp_mid_value.isEmpty() || sdp_mline_index < 0 || candidate_text.isEmpty()) {
    RTC_LOG(LS_ERROR) << "ICE candidate payload incomplete";
    qDebug() << "ERROR: ICE candidate payload incomplete"
             << "sdpMid:" << sdp_mid_value
             << "mline:" << sdp_mline_index
             << "candidate:" << candidate_text.left(32);
    return;
  }
  
  std::string sdp_mid = sdp_mid_value.toStdString();
  std::string sdp = candidate_text.toStdString();
  
  webrtc_engine_->AddIceCandidate(sdp_mid, sdp_mline_index, sdp);
}
