# 重构后的项目调用链详解

## 文档概述

本文档详细描述了重构后的 WebRTC 视频通话客户端的完整调用链，展示了各个组件之间的交互流程。

---

## 核心架构

```
┌─────────────────────────────────────────────────────────────┐
│                        main.cc                              │
│  - 初始化 WebRTC 环境                                        │
│  - 创建 Qt 应用程序                                          │
│  - 启动事件循环                                              │
└─────────────────────────────────────────────────────────────┘
                          │
                          │ creates & initializes
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   CallCoordinator                           │
│  - 业务协调层（Business Logic Layer）                        │
│  - 实现 WebRTCEngineObserver                                 │
│  - 实现 SignalClientObserver                                 │
│  - 实现 CallManagerObserver                                  │
│  - 实现 ICallController (for UI)                            │
└─────────────────────────────────────────────────────────────┘
        │                   │                    │
        │ owns              │ owns               │ owns
        ▼                   ▼                    ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│ WebRTCEngine │   │ SignalClient │   │ CallManager  │
│  - WebRTC    │   │  - WebSocket │   │  - 呼叫状态  │
│  - 媒体流    │   │  - 信令消息  │   │  - 计时器    │
└──────────────┘   └──────────────┘   └──────────────┘
                          │
                          │ observes via
                          │ ICallUIObserver
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                  VideoCallWindow                            │
│  - UI 展示层（Presentation Layer）                           │
│  - 实现 ICallUIObserver                                      │
│  - Qt Widgets 界面                                           │
└─────────────────────────────────────────────────────────────┘
```

---

## 详细调用链

### 1. 应用启动流程

```
main.cc::main()
│
├─► webrtc::WinsockInitializer() ──────────────────► 初始化 Winsock
├─► webrtc::PhysicalSocketServer() ────────────────► 创建网络服务器
├─► webrtc::CreateEnvironment() ───────────────────► 创建 WebRTC 环境
├─► webrtc::InitializeSSL() ───────────────────────► 初始化 SSL/TLS
│
├─► QApplication() ────────────────────────────────► 创建 Qt 应用
│
├─► CallCoordinator::new(env) ─────────────────────► 创建业务协调器
│   │
│   └─► CallCoordinator::Initialize()
│       │
│       ├─► WebRTCEngine::new() ──────────────────► 创建 WebRTC 引擎
│       ├─► SignalClient::new() ──────────────────► 创建信令客户端
│       ├─► CallManager::new() ───────────────────► 创建呼叫管理器
│       │
│       ├─► webrtc_engine_->SetObserver(this) ────► 注册为 Engine 观察者
│       ├─► signal_client_->SetObserver(this) ────► 注册为 Signal 观察者
│       ├─► call_manager_->SetObserver(this) ─────► 注册为 Manager 观察者
│       │
│       └─► WebRTCEngine::Initialize() ───────────► 初始化 WebRTC
│
├─► VideoCallWindow::new(coordinator) ─────────────► 创建主窗口
│   │
│   └─► VideoCallWindow::CreateUI() ──────────────► 创建界面组件
│
├─► coordinator->SetUIObserver(&main_window) ──────► 注册 UI 观察者
│
├─► main_window.show() ────────────────────────────► 显示主窗口
│
├─► QTimer::start(10ms) ───────────────────────────► 启动消息处理定时器
│
└─► app.exec() ────────────────────────────────────► 运行 Qt 事件循环
```

---

### 2. 连接信令服务器流程

```
用户操作: 点击"连接"按钮
│
VideoCallWindow::OnConnectClicked()
│
└─► ICallController::ConnectToServer(url, client_id)
    │
    CallCoordinator::ConnectToServer(url, client_id)
    │
    └─► SignalClient::Connect(url, client_id)
        │
        ├─► QWebSocket::open(url) ─────────────────► 建立 WebSocket 连接
        │
        └─► [异步] QWebSocket::connected 信号触发
            │
            SignalClient::OnConnected()
            │
            ├─► SendMessage({ type: "register" }) ──► 发送注册消息
            │
            └─► [异步] 服务器响应 "registered"
                │
                SignalClient::OnTextMessageReceived()
                │
                └─► SignalClient::HandleMessage()
                    │
                    └─► SignalClientObserver::OnRegistered(ice_servers)
                        │
                        CallCoordinator::OnRegistered(ice_servers)
                        │
                        ├─► WebRTCEngine::SetIceServers() ────────► 配置 ICE 服务器
                        │
                        └─► ICallUIObserver::OnConnected()
                            │
                            VideoCallWindow::OnConnected()
                            │
                            └─► 更新 UI 显示"已连接"
```

---

### 3. 发起呼叫流程（主叫方）

```
用户操作: 选择用户列表中的某个用户，点击"呼叫"
│
VideoCallWindow::OnUserDoubleClicked(user_id)
│
└─► ICallController::StartCall(user_id)
    │
    CallCoordinator::StartCall(user_id)
    │
    └─► CallManager::StartCall(user_id)
        │
        ├─► SetCallState(Calling) ────────────────────► 设置状态为"呼叫中"
        ├─► emit CallStateChanged(Calling) ───────────► 发射状态变化信号
        │   │
        │   └─► CallCoordinator::OnCallStateChanged() ► 协调器收到通知
        │       │
        │       └─► ICallUIObserver::OnCallStateChanged()
        │           │
        │           VideoCallWindow::OnCallStateChanged() ► UI 更新
        │
        ├─► SignalClient::SendCallRequest(user_id) ───► 发送呼叫请求
        │   │
        │   └─► WebSocket 发送: { type: "call-request", to: user_id }
        │
        └─► StartCallRequestTimer() ──────────────────► 启动超时定时器

[等待对方响应...]

服务器转发 call-request 到被叫方 ──────────────────────────►

◄─────────────────────────── 被叫方接受呼叫（详见接听流程）

SignalClient::OnTextMessageReceived()
│
└─► SignalClient::HandleMessage({ type: "call-response" })
    │
    └─► SignalClientObserver::OnCallResponse(from, accepted)
        │
        CallCoordinator::OnCallResponse(from, accepted)
        │
        └─► CallManager::HandleCallResponse(from, accepted)
            │
            ├─► StopCallRequestTimer() ───────────────► 停止超时定时器
            ├─► SetCallState(Connecting) ─────────────► 设置状态为"连接中"
            │
            └─► CallManagerObserver::OnNeedCreatePeerConnection(peer, true)
                │
                CallCoordinator::OnNeedCreatePeerConnection(peer, true)
                │                                           ↑ is_caller=true
                ├─► current_peer_id_ = peer
                ├─► is_caller_ = true
                │
                └─► WebRTCEngine::CreatePeerConnection()
                    │
                    ├─► 创建 RTCPeerConnection 对象
                    ├─► 设置 ICE、DTLS 回调
                    │
                    └─► WebRTCEngine::AddTracks()
                        │
                        ├─► 添加本地视频轨道
                        ├─► 添加本地音频轨道
                        │
                        └─► WebRTCEngine::CreateOffer()
                            │
                            └─► PeerConnection::CreateOffer()
                                │
                                [异步] CreateSessionDescriptionObserver::OnSuccess()
                                │
                                WebRTCEngine::OnSessionDescriptionSuccess(desc, true)
                                │
                                └─► PeerConnection::SetLocalDescription(desc)
                                    │
                                    [异步] SetLocalDescriptionObserver::OnSuccess()
                                    │
                                    └─► WebRTCEngineObserver::OnOfferCreated(sdp)
                                        │
                                        CallCoordinator::OnOfferCreated(sdp)
                                        │
                                        └─► QMetaObject::invokeMethod() ────► 切换到主线程
                                            │
                                            SignalClient::SendOffer(peer, sdp)
                                            │
                                            └─► WebSocket 发送: {
                                                  type: "offer",
                                                  to: peer,
                                                  payload: { sdp, type: "offer" }
                                                }

[同时] ICE 候选者生成 ────────────────────────────────────►

PeerConnection::OnIceCandidate()
│
└─► WebRTCEngineObserver::OnIceCandidateGenerated()
    │
    CallCoordinator::OnIceCandidateGenerated()
    │
    └─► QMetaObject::invokeMethod() ──────────────────────► 切换到主线程
        │
        SignalClient::SendIceCandidate(peer, candidate)
        │
        └─► WebSocket 发送: {
              type: "ice-candidate",
              to: peer,
              payload: { candidate, sdpMid, sdpMLineIndex }
            }
```

---

### 4. 接听呼叫流程（被叫方）

```
SignalClient::OnTextMessageReceived()
│
└─► SignalClient::HandleMessage({ type: "call-request", from: caller })
    │
    └─► SignalClientObserver::OnCallRequest(caller)
        │
        CallCoordinator::OnCallRequest(caller)
        │
        └─► CallManager::HandleCallRequest(caller)
            │
            ├─► SetCallState(Receiving) ──────────────────► 设置状态为"接听中"
            ├─► emit IncomingCall(caller) ────────────────► 发射来电信号
            │
            └─► CallManagerObserver::OnIncomingCall(caller)
                │
                CallCoordinator::OnIncomingCall(caller)
                │
                └─► ICallUIObserver::OnIncomingCall(caller)
                    │
                    VideoCallWindow::OnIncomingCall(caller)
                    │
                    └─► QMessageBox::exec() ──────────────► 显示接听/拒绝对话框

用户操作: 点击"接听"按钮
│
└─► ICallController::AcceptCall()
    │
    CallCoordinator::AcceptCall()
    │
    └─► CallManager::AcceptCall()
        │
        ├─► SignalClient::SendCallResponse(caller, true) ► 发送接受响应
        │   │
        │   └─► WebSocket 发送: {
        │         type: "call-response",
        │         to: caller,
        │         payload: { accepted: true }
        │       }
        │
        ├─► SetCallState(Connecting) ─────────────────────► 设置状态为"连接中"
        │
        └─► CallManagerObserver::OnNeedCreatePeerConnection(caller, false)
            │
            CallCoordinator::OnNeedCreatePeerConnection(caller, false)
            │                                              ↑ is_caller=false
            ├─► current_peer_id_ = caller
            ├─► is_caller_ = false
            │
            └─► WebRTCEngine::CreatePeerConnection()
                │
                ├─► 创建 RTCPeerConnection 对象
                ├─► 设置 ICE、DTLS 回调
                │
                └─► WebRTCEngine::AddTracks()
                    │
                    ├─► 添加本地视频轨道
                    └─► 添加本地音频轨道

[等待接收 Offer...]

SignalClient::OnTextMessageReceived()
│
└─► SignalClient::HandleMessage({ type: "offer", from: caller })
    │
    └─► SignalClientObserver::OnOffer(caller, sdp)
        │
        CallCoordinator::OnOffer(caller, sdp)
        │
        └─► CallCoordinator::ProcessOffer(caller, sdp)
            │
            ├─► WebRTCEngine::SetRemoteOffer(sdp)
            │   │
            │   └─► PeerConnection::SetRemoteDescription(offer_desc)
            │       │
            │       [异步] SetRemoteDescriptionObserver::OnSuccess()
            │
            └─► WebRTCEngine::CreateAnswer()
                │
                └─► PeerConnection::CreateAnswer()
                    │
                    [异步] CreateSessionDescriptionObserver::OnSuccess()
                    │
                    WebRTCEngine::OnSessionDescriptionSuccess(desc, false)
                    │
                    └─► PeerConnection::SetLocalDescription(desc)
                        │
                        [异步] SetLocalDescriptionObserver::OnSuccess()
                        │
                        └─► WebRTCEngineObserver::OnAnswerCreated(sdp)
                            │
                            CallCoordinator::OnAnswerCreated(sdp)
                            │
                            └─► QMetaObject::invokeMethod() ────► 切换到主线程
                                │
                                SignalClient::SendAnswer(caller, sdp)
                                │
                                └─► WebSocket 发送: {
                                      type: "answer",
                                      to: caller,
                                      payload: { sdp, type: "answer" }
                                    }

[处理 ICE 候选者...]

SignalClient::OnTextMessageReceived()
│
└─► SignalClient::HandleMessage({ type: "ice-candidate", from: caller })
    │
    └─► SignalClientObserver::OnIceCandidate(caller, candidate)
        │
        CallCoordinator::OnIceCandidate(caller, candidate)
        │
        └─► CallCoordinator::ProcessIceCandidate(caller, candidate)
            │
            └─► WebRTCEngine::AddIceCandidate(candidate)
                │
                └─► PeerConnection::AddIceCandidate(ice_candidate)
```

---

### 5. 主叫方接收 Answer 流程

```
SignalClient::OnTextMessageReceived()
│
└─► SignalClient::HandleMessage({ type: "answer", from: callee })
    │
    └─► SignalClientObserver::OnAnswer(callee, sdp)
        │
        CallCoordinator::OnAnswer(callee, sdp)
        │
        └─► CallCoordinator::ProcessAnswer(callee, sdp)
            │
            └─► WebRTCEngine::SetRemoteAnswer(sdp)
                │
                └─► PeerConnection::SetRemoteDescription(answer_desc)
                    │
                    [异步] SetRemoteDescriptionObserver::OnSuccess()
                    │
                    └─► ICE 连接开始...
```

---

### 6. 媒体流建立流程

```
PeerConnection ICE 连接成功
│
├─► PeerConnectionObserver::OnIceConnectionChange(Connected)
│   │
│   └─► WebRTCEngineObserver::OnIceConnectionChange(Connected)
│       │
│       CallCoordinator::OnIceConnectionChange(Connected)
│       │
│       └─► ICallUIObserver::OnCallStateChanged(Connected)
│           │
│           VideoCallWindow::OnCallStateChanged(Connected)
│           │
│           └─► 更新 UI："通话中"
│
└─► PeerConnectionObserver::OnTrack(track)
    │
    └─► WebRTCEngineObserver::OnRemoteTrack(track)
        │
        CallCoordinator::OnRemoteTrack(track)
        │
        └─► ICallUIObserver::OnAddRemoteTrack(track)
            │
            VideoCallWindow::OnAddRemoteTrack(track)
            │
            ├─► 创建 VideoRenderer 对象
            ├─► track->AddOrUpdateSink(renderer)
            │
            └─► ICallUIObserver::OnStartRemoteRenderer(renderer)
                │
                VideoCallWindow::OnStartRemoteRenderer(renderer)
                │
                └─► remote_renderer_->start() ──────────► 开始渲染远程视频
```

---

### 7. 结束通话流程

```
用户操作: 点击"挂断"按钮
│
VideoCallWindow::OnHangupClicked()
│
└─► ICallController::EndCall()
    │
    CallCoordinator::EndCall()
    │
    └─► CallManager::EndCall()
        │
        ├─► SignalClient::SendCallEnd(peer, "normal") ───► 发送结束消息
        │   │
        │   └─► WebSocket 发送: {
        │         type: "call-end",
        │         to: peer,
        │         payload: { reason: "normal" }
        │       }
        │
        └─► CallManager::CleanupCall()
            │
            ├─► SetCallState(Idle) ───────────────────────► 重置状态
            │
            └─► CallManagerObserver::OnNeedClosePeerConnection()
                │
                CallCoordinator::OnNeedClosePeerConnection()
                │
                ├─► ICallUIObserver::OnStopLocalRenderer() ► 停止本地渲染
                ├─► ICallUIObserver::OnStopRemoteRenderer() ► 停止远程渲染
                │
                └─► WebRTCEngine::ClosePeerConnection()
                    │
                    ├─► 停止所有媒体轨道
                    ├─► 关闭 PeerConnection
                    └─► 释放资源
```

---

## 关键设计模式

### 1. Observer Pattern（观察者模式）

**目的**: 解耦组件之间的依赖

```
WebRTCEngine ──observes──► WebRTCEngineObserver (CallCoordinator)
SignalClient ──observes──► SignalClientObserver (CallCoordinator)
CallManager  ──observes──► CallManagerObserver  (CallCoordinator)
CallCoordinator ──observes──► ICallUIObserver (VideoCallWindow)
```

**优势**:
- ✅ 低耦合：各层之间通过接口通信
- ✅ 可测试：可以轻松 mock 观察者接口
- ✅ 可扩展：新增观察者不影响原有代码

### 2. Facade Pattern（外观模式）

**CallCoordinator** 作为外观，隐藏了 WebRTC、信令、呼叫管理的复杂性：

```
VideoCallWindow (UI)
        ↓
ICallController (简单接口)
        ↓
CallCoordinator (外观)
    ↙   ↓   ↘
WebRTC Signal CallMgr (复杂子系统)
```

### 3. Dependency Injection（依赖注入）

```cpp
// main.cc 中创建依赖
auto coordinator = std::make_unique<CallCoordinator>(env);
VideoCallWindow main_window(coordinator.get());

// 注入观察者
coordinator->SetUIObserver(&main_window);
```

**优势**:
- ✅ 控制反转：依赖由外部注入
- ✅ 灵活性：可以注入不同的实现
- ✅ 可测试：可以注入 mock 对象

---

## 线程模型

### 线程分布

```
┌──────────────────────────────────────────────────────────┐
│ Qt Main Thread                                           │
│  - VideoCallWindow (UI 操作)                              │
│  - SignalClient (WebSocket 发送接收)                      │
│  - CallManager (状态管理)                                 │
│  - CallCoordinator (协调逻辑)                             │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ WebRTC Signaling Thread                                  │
│  - PeerConnection 创建/销毁                                │
│  - SDP Offer/Answer 生成                                  │
│  - ICE Candidate 生成                                     │
│  - SetLocalDescription / SetRemoteDescription            │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ WebRTC Worker Thread                                     │
│  - 编解码                                                  │
│  - 网络 I/O                                                │
│  - RTP 包处理                                              │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ WebRTC Network Thread                                    │
│  - Socket 操作                                             │
│  - ICE 连接                                                │
│  - DTLS 握手                                               │
└──────────────────────────────────────────────────────────┘
```

### 跨线程通信

**问题**: WebRTC 回调在 signaling 线程，但 QWebSocket 必须在主线程使用

**解决方案**: 使用 `QMetaObject::invokeMethod` 切换线程

```cpp
// WebRTC 线程中的回调
void CallCoordinator::OnOfferCreated(const std::string& sdp) {
  // ... 准备数据 ...
  
  // 切换到主线程执行 WebSocket 发送
  QMetaObject::invokeMethod(signal_client_.get(), [this, peer_id, json_sdp]() {
    signal_client_->SendOffer(peer_id, json_sdp);
  }, Qt::QueuedConnection);  // ← 异步调用，切换到主线程
}
```

---

## 重构前后对比

### 重构前的问题

1. **紧耦合**
   ```cpp
   // Conductor 直接依赖 MainWnd
   class Conductor {
     MainWnd* main_wnd_;  // ❌ 强依赖具体实现
     void OnOfferCreated() {
       main_wnd_->ShowSpecificUI();  // ❌ 直接调用 UI
     }
   };
   ```

2. **重复通知**
   ```cpp
   // 同时使用 observer 和 Qt signal，导致双重调用
   if (observer_) observer_->OnIncomingCall(caller);  // 第一次
   emit IncomingCall(caller);  // 第二次 ❌
   ```

3. **线程不安全**
   ```cpp
   void OnOfferCreated(sdp) {
     // 在 WebRTC 线程中直接调用 Qt WebSocket ❌
     signal_client_->SendOffer(peer, sdp);  
     // 导致: "QSocketNotifier cannot be enabled from another thread"
   }
   ```

### 重构后的改进

1. **接口解耦**
   ```cpp
   // CallCoordinator 依赖抽象接口
   class CallCoordinator {
     ICallUIObserver* ui_observer_;  // ✅ 依赖接口
     void OnOfferCreated() {
       ui_observer_->OnLogMessage(...);  // ✅ 通用接口
     }
   };
   ```

2. **单一通知**
   ```cpp
   // 只使用 observer 模式，移除重复的 Qt signal 连接
   void HandleCallRequest(caller) {
     if (observer_) observer_->OnIncomingCall(caller);  // ✅ 只调用一次
     // 移除了 emit IncomingCall(caller);
   }
   ```

3. **线程安全**
   ```cpp
   void OnOfferCreated(sdp) {
     // 使用 QMetaObject 切换到主线程 ✅
     QMetaObject::invokeMethod(signal_client_.get(), [=]() {
       signal_client_->SendOffer(peer, sdp);
     }, Qt::QueuedConnection);
   }
   ```

---

## 总结

### 架构优势

✅ **清晰的分层**
- UI 层：VideoCallWindow（展示）
- 业务层：CallCoordinator（协调）
- 服务层：WebRTCEngine、SignalClient、CallManager（具体功能）

✅ **松耦合**
- 通过接口（ICallUIObserver、ICallController）通信
- 各层可独立测试和替换

✅ **可维护性**
- 代码职责单一
- 易于定位问题
- 便于扩展新功能

✅ **线程安全**
- 明确的线程边界
- 正确的跨线程通信

✅ **可测试性**
- 可以 mock 各个接口
- 单元测试更简单

### 已解决的问题

1. ✅ Conductor 强依赖 MainWnd → CallCoordinator 依赖 ICallUIObserver 接口
2. ✅ 双重呼叫通知 → 移除重复的 Qt signal 连接
3. ✅ 跨线程 WebSocket 调用 → 使用 QMetaObject::invokeMethod
4. ✅ Offer/Answer 无法发送 → 线程安全问题已修复
5. ✅ 远程视频不显示 → 完整的 SDP 交换流程建立

---

**文档版本**: 1.0  
**最后更新**: 2025年10月20日  
**作者**: GitHub Copilot  
