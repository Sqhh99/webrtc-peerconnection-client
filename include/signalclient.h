#ifndef EXAMPLES_PEERCONNECTION_CLIENT_SIGNALCLIENT_H_
#define EXAMPLES_PEERCONNECTION_CLIENT_SIGNALCLIENT_H_

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "signal_types.h"

// 信令客户端观察者接口
class SignalClientObserver {
 public:
  virtual ~SignalClientObserver() = default;
  
  // 连接状态变化
  virtual void OnConnected(const std::string& client_id) = 0;
  virtual void OnDisconnected() = 0;
  virtual void OnConnectionError(const std::string& error) = 0;
  
  // ICE 服务器配置更新
  virtual void OnIceServersReceived(const std::vector<IceServerConfig>& ice_servers) = 0;

  // 客户端列表更新
  virtual void OnClientListUpdate(const std::vector<ClientInfo>& clients) = 0;
  virtual void OnUserOffline(const std::string& client_id) = 0;

  // 呼叫相关
  virtual void OnCallRequest(const std::string& from, const std::string& call_id) = 0;
  virtual void OnCallResponse(const std::string& from,
                              const std::string& call_id,
                              bool accepted,
                              const std::string& reason) = 0;
  virtual void OnCallCancel(const std::string& from,
                            const std::string& call_id,
                            const std::string& reason) = 0;
  virtual void OnCallEnd(const std::string& from,
                         const std::string& call_id,
                         const std::string& reason) = 0;

  // WebRTC信令
  virtual void OnOffer(const std::string& from, const SessionDescriptionPayload& sdp) = 0;
  virtual void OnAnswer(const std::string& from, const SessionDescriptionPayload& sdp) = 0;
  virtual void OnIceCandidate(const std::string& from, const IceCandidatePayload& candidate) = 0;
};

// WebSocket信令客户端
class SignalClient {
 public:
  SignalClient();
  ~SignalClient();

  // 连接到信令服务器
  void Connect(const std::string& server_url, const std::string& client_id = "");
  void Disconnect();
  bool IsConnected() const;

  // 获取客户端ID
  std::string GetClientId() const;

  // 获取 ICE 服务器配置
  std::vector<IceServerConfig> GetIceServers() const;

  // 注册观察者
  void RegisterObserver(SignalClientObserver* observer);

  // 发送消息
  void SendCallRequest(const std::string& to, const std::string& call_id);
  void SendCallResponse(const std::string& to,
                        const std::string& call_id,
                        bool accepted,
                        const std::string& reason = "");
  void SendCallCancel(const std::string& to,
                      const std::string& call_id,
                      const std::string& reason = "");
  void SendCallEnd(const std::string& to,
                   const std::string& call_id,
                   const std::string& reason = "");
  void SendOffer(const std::string& to, const SessionDescriptionPayload& sdp);
  void SendAnswer(const std::string& to, const SessionDescriptionPayload& sdp);
  void SendIceCandidate(const std::string& to, const IceCandidatePayload& candidate);
  void RequestClientList();
  bool InvokeOnIoThread(std::function<void()> task);

 private:
  struct ParsedUrl;

  void EnsureIoThreadStarted();
  void StopIoThread();
  void ConnectOnIoThread();
  void RecreateTransport();
  void StartRead();
  void QueueJsonMessage(const std::string& message);
  void WriteNextMessage();
  void HandleIncomingMessage(const std::string& message);
  void AttemptReconnect();
  void CloseTransport();
  void ReportDisconnected(bool notify_observer);
  void ReportConnectionError(const std::string& error);
  ParsedUrl ParseUrl(const std::string& url) const;

  class Impl;
  std::unique_ptr<Impl> impl_;

  mutable std::mutex state_mutex_;
  SignalClientObserver* observer_;
  std::string server_url_;
  std::string client_id_;
  bool is_connected_;
  bool manual_disconnect_;
  int reconnect_attempts_;
  std::vector<IceServerConfig> ice_servers_;  // ICE 服务器配置

  static constexpr int kMaxReconnectAttempts = 5;
};

#endif  // EXAMPLES_PEERCONNECTION_CLIENT_SIGNALCLIENT_H_
