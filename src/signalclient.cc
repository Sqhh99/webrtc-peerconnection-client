#include "signalclient.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QDebug>

SignalClient::SignalClient(QObject* parent)
    : QObject(parent),
      observer_(nullptr),
      is_connected_(false),
      manual_disconnect_(false),
      reconnect_attempts_(0) {
  websocket_ = std::make_unique<QWebSocket>();
  reconnect_timer_ = std::make_unique<QTimer>(this);
  reconnect_timer_->setSingleShot(true);
  
  connect(websocket_.get(), &QWebSocket::connected, this, &SignalClient::OnWebSocketConnected);
  connect(websocket_.get(), &QWebSocket::disconnected, this, &SignalClient::OnWebSocketDisconnected);
  connect(websocket_.get(), QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
          this, &SignalClient::OnWebSocketError);
  connect(websocket_.get(), &QWebSocket::textMessageReceived, this, &SignalClient::OnTextMessageReceived);
  connect(reconnect_timer_.get(), &QTimer::timeout, this, &SignalClient::OnReconnectTimer);
}

SignalClient::~SignalClient() {
  Disconnect();
}

void SignalClient::Connect(const QString& server_url, const QString& client_id) {
  if (is_connected_) {
    qDebug() << "Already connected to signaling server";
    return;
  }
  
  server_url_ = server_url;
  
  // 生成或使用提供的客户端ID
  if (client_id.isEmpty()) {
    client_id_ = QString("qt_client_%1").arg(
        QDateTime::currentMSecsSinceEpoch() % 1000000);
  } else {
    client_id_ = client_id;
  }
  
  qDebug() << "Connecting to signaling server:" << server_url_;
  qDebug() << "Client ID:" << client_id_;
  
  // 构建完整的URL,添加uid参数
  QString full_url = server_url;
  if (!full_url.contains("?")) {
    full_url += "?uid=" + client_id_;
  } else {
    full_url += "&uid=" + client_id_;
  }
  
  qDebug() << "Full URL with uid:" << full_url;
  
  manual_disconnect_ = false;
  websocket_->open(QUrl(full_url));
}

void SignalClient::Disconnect() {
  manual_disconnect_ = true;
  ClearReconnectTimer();
  
  if (websocket_ && websocket_->state() == QAbstractSocket::ConnectedState) {
    websocket_->close();
  }
  
  is_connected_ = false;
}

bool SignalClient::IsConnected() const {
  return is_connected_;
}

void SignalClient::RegisterObserver(SignalClientObserver* observer) {
  observer_ = observer;
}

void SignalClient::SendCallRequest(const QString& to) {
  QJsonObject message;
  message["type"] = "call-request";
  message["from"] = client_id_;
  message["to"] = to;
  
  QJsonObject payload;
  payload["timestamp"] = QDateTime::currentMSecsSinceEpoch();
  message["payload"] = payload;
  
  SendMessage(message);
}

void SignalClient::SendCallResponse(const QString& to, bool accepted, const QString& reason) {
  QJsonObject message;
  message["type"] = "call-response";
  message["from"] = client_id_;
  message["to"] = to;
  
  QJsonObject payload;
  payload["accepted"] = accepted;
  if (!reason.isEmpty()) {
    payload["reason"] = reason;
  }
  message["payload"] = payload;
  
  SendMessage(message);
}

void SignalClient::SendCallCancel(const QString& to, const QString& reason) {
  QJsonObject message;
  message["type"] = "call-cancel";
  message["from"] = client_id_;
  message["to"] = to;
  
  QJsonObject payload;
  if (!reason.isEmpty()) {
    payload["reason"] = reason;
  }
  message["payload"] = payload;
  
  SendMessage(message);
}

void SignalClient::SendCallEnd(const QString& to, const QString& reason) {
  QJsonObject message;
  message["type"] = "call-end";
  message["from"] = client_id_;
  message["to"] = to;
  
  QJsonObject payload;
  if (!reason.isEmpty()) {
    payload["reason"] = reason;
  }
  message["payload"] = payload;
  
  SendMessage(message);
}

void SignalClient::SendOffer(const QString& to, const QJsonObject& sdp) {
  qDebug() << "SendOffer called - to:" << to << "from:" << client_id_;
  
  QJsonObject message;
  message["type"] = "offer";
  message["from"] = client_id_;
  message["to"] = to;

  QJsonObject payload;
  payload["sdp"] = sdp;
  message["payload"] = payload;
  
  qDebug() << "Sending offer message:" << message;
  SendMessage(message);
}

void SignalClient::SendAnswer(const QString& to, const QJsonObject& sdp) {
  QJsonObject message;
  message["type"] = "answer";
  message["from"] = client_id_;
  message["to"] = to;

  QJsonObject payload;
  payload["sdp"] = sdp;
  message["payload"] = payload;
  
  SendMessage(message);
}

void SignalClient::SendIceCandidate(const QString& to, const QJsonObject& candidate) {
  QJsonObject message;
  message["type"] = "ice-candidate";
  message["from"] = client_id_;
  message["to"] = to;

  QJsonObject payload;
  payload["candidate"] = candidate;
  message["payload"] = payload;
  
  SendMessage(message);
}

void SignalClient::RequestClientList() {
  QJsonObject message;
  message["type"] = "list-clients";
  message["from"] = client_id_;
  
  SendMessage(message);
}

void SignalClient::OnWebSocketConnected() {
  is_connected_ = true;
  reconnect_attempts_ = 0;
  
  qDebug() << "WebSocket connected";
  
  // 发送注册消息
  QJsonObject message;
  message["type"] = "register";
  message["from"] = client_id_;
  SendMessage(message);
  
  if (observer_) {
    observer_->OnConnected(client_id_.toStdString());
  }
  
  emit SignalConnected(client_id_);
}

void SignalClient::OnWebSocketDisconnected() {
  is_connected_ = false;
  
  qDebug() << "WebSocket disconnected";
  
  if (observer_) {
    observer_->OnDisconnected();
  }
  
  emit SignalDisconnected();
  
  // 尝试重连
  if (!manual_disconnect_) {
    AttemptReconnect();
  }
}

void SignalClient::OnWebSocketError(QAbstractSocket::SocketError error) {
  QString error_string = websocket_->errorString();
  qDebug() << "WebSocket error:" << error << error_string;
  
  if (observer_) {
    observer_->OnConnectionError(error_string.toStdString());
  }
  
  emit SignalError(error_string);
}

void SignalClient::OnTextMessageReceived(const QString& message) {
  QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
  if (!doc.isObject()) {
    qWarning() << "Received invalid JSON message";
    return;
  }
  
  QJsonObject json = doc.object();
  HandleMessage(json);
}

void SignalClient::OnReconnectTimer() {
  qDebug() << "Attempting reconnection, attempt" << reconnect_attempts_;
  Connect(server_url_, client_id_);
}

void SignalClient::SendMessage(const QJsonObject& message) {
  if (!is_connected_ || websocket_->state() != QAbstractSocket::ConnectedState) {
    qWarning() << "Cannot send message: not connected, state:" << websocket_->state();
    return;
  }
  
  QJsonDocument doc(message);
  QString json_string = doc.toJson(QJsonDocument::Compact);
  
  QString msg_type = message["type"].toString();
  qDebug() << "Sending message:" << msg_type << "length:" << json_string.length() << "bytes";
  
  qint64 bytes_written = websocket_->sendTextMessage(json_string);
  if (bytes_written < 0) {
    qWarning() << "Failed to send message:" << msg_type;
  } else {
    qDebug() << "Message sent successfully:" << msg_type << "(" << bytes_written << "bytes)";
  }
}

void SignalClient::HandleMessage(const QJsonObject& message) {
  QString type_str = message["type"].toString();
  SignalMessageType msg_type = GetMessageType(type_str);
  
  qDebug() << "=== Received message ===" 
           << "\n  Type:" << type_str
           << "\n  From:" << message["from"].toString()
           << "\n  Has payload:" << message.contains("payload");
  
  if (!observer_) {
    qDebug() << "  No observer to handle message!";
    return;
  }
  
  QString from = message["from"].toString();
  QJsonObject payload = message["payload"].toObject();
  
  switch (msg_type) {
    case SignalMessageType::Registered: {
      qDebug() << "Client registered successfully";
      
      // 解析 ICE 服务器配置
      if (payload.contains("iceServers")) {
        QJsonArray ice_servers_array = payload["iceServers"].toArray();
        ice_servers_.clear();
        
        for (const QJsonValue& server_value : ice_servers_array) {
          QJsonObject server_obj = server_value.toObject();
          IceServerConfig config;
          
          // 解析 URLs
          QJsonArray urls_array = server_obj["urls"].toArray();
          for (const QJsonValue& url : urls_array) {
            config.urls.push_back(url.toString().toStdString());
          }
          
          // 解析用户名和凭据
          if (server_obj.contains("username")) {
            config.username = server_obj["username"].toString().toStdString();
          }
          if (server_obj.contains("credential")) {
            config.credential = server_obj["credential"].toString().toStdString();
          }
          
          ice_servers_.push_back(config);
        }
        
        qDebug() << "Received" << ice_servers_.size() << "ICE server configurations";
        
        // 通知观察者 ICE 服务器配置已更新
        if (observer_) {
          observer_->OnIceServersReceived(ice_servers_);
        }
      }
      
      // 请求客户端列表
      RequestClientList();
      break;
    }
      
    case SignalMessageType::ClientList: {
      QJsonArray clients = payload["clients"].toArray();
      qDebug() << "Received client list with" << clients.size() << "clients";
      qDebug() << "Client list payload:" << payload;
      observer_->OnClientListUpdate(clients);
      break;
    }
    
    case SignalMessageType::UserOffline: {
      QString offline_id = payload["clientId"].toString();
      observer_->OnUserOffline(offline_id.toStdString());
      break;
    }
    
    case SignalMessageType::CallRequest:
      observer_->OnCallRequest(from.toStdString(), payload);
      break;
      
    case SignalMessageType::CallResponse: {
      bool accepted = payload["accepted"].toBool();
      QString reason = payload["reason"].toString();
      observer_->OnCallResponse(from.toStdString(), accepted, reason.toStdString());
      break;
    }
    
    case SignalMessageType::CallCancel: {
      QString reason = payload["reason"].toString();
      observer_->OnCallCancel(from.toStdString(), reason.toStdString());
      break;
    }
    
    case SignalMessageType::CallEnd: {
      QString reason = payload["reason"].toString();
      observer_->OnCallEnd(from.toStdString(), reason.toStdString());
      break;
    }
    
    case SignalMessageType::Offer:
      qDebug() << "!!! ABOUT TO CALL OnOffer !!!" << "from:" << from;
      observer_->OnOffer(from.toStdString(), payload);
      qDebug() << "!!! OnOffer RETURNED !!!";
      break;
      
    case SignalMessageType::Answer:
      qDebug() << "!!! ABOUT TO CALL OnAnswer !!!" << "from:" << from;
      observer_->OnAnswer(from.toStdString(), payload);
      qDebug() << "!!! OnAnswer RETURNED !!!";
      break;
      
    case SignalMessageType::IceCandidate:
      observer_->OnIceCandidate(from.toStdString(), payload);
      break;
      
    default:
      qWarning() << "Unknown message type:" << type_str;
      break;
  }
}

SignalMessageType SignalClient::GetMessageType(const QString& type_str) const {
  if (type_str == "register") return SignalMessageType::Register;
  if (type_str == "registered") return SignalMessageType::Registered;
  if (type_str == "client-list") return SignalMessageType::ClientList;
  if (type_str == "user-offline") return SignalMessageType::UserOffline;
  if (type_str == "call-request") return SignalMessageType::CallRequest;
  if (type_str == "call-response") return SignalMessageType::CallResponse;
  if (type_str == "call-cancel") return SignalMessageType::CallCancel;
  if (type_str == "call-end") return SignalMessageType::CallEnd;
  if (type_str == "offer") return SignalMessageType::Offer;
  if (type_str == "answer") return SignalMessageType::Answer;
  if (type_str == "ice-candidate") return SignalMessageType::IceCandidate;
  return SignalMessageType::Unknown;
}

void SignalClient::AttemptReconnect() {
  if (reconnect_attempts_ >= kMaxReconnectAttempts) {
    qWarning() << "Max reconnection attempts reached";
    return;
  }
  
  reconnect_attempts_++;
  
  // 指数退避算法：1s, 2s, 4s, 8s, 10s
  int delay = qMin(1000 * (1 << (reconnect_attempts_ - 1)), 10000);
  
  qDebug() << "Will attempt reconnection in" << delay << "ms";
  reconnect_timer_->start(delay);
}

void SignalClient::ClearReconnectTimer() {
  if (reconnect_timer_->isActive()) {
    reconnect_timer_->stop();
  }
  reconnect_attempts_ = 0;
}
