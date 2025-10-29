# 重构前后架构对比

## 重构前架构（存在问题）

```
┌────────────────────────────────────────────────────────────┐
│                         MainWnd                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ - 管理UI界面                                          │  │
│  │ - 持有SignalClient                                    │  │
│  │ - 持有CallManager                                     │  │
│  │ - 提供GetSignalClient()、GetCallManager()方法        │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────┬───────────────────────────────────────────┘
                 │
                 │ 直接持有指针
                 ▼
┌────────────────────────────────────────────────────────────┐
│                       Conductor                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ - 持有MainWnd*指针（强依赖）                          │  │
│  │ - 通过main_wnd_->GetSignalClient()访问组件            │  │
│  │ - 通过main_wnd_->GetCallManager()访问组件             │  │
│  │ - 调用main_wnd_->StartLocalRenderer()等UI方法         │  │
│  │ - 持有WebRTCEngine                                    │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘

问题:
❌ Conductor强依赖MainWnd，无法单独使用
❌ MainWnd持有业务组件，职责不清
❌ 难以编写单元测试（需要完整UI环境）
❌ WebRTC组件难以复用到其他场景
❌ 循环依赖风险
```

## 重构后架构（解耦清晰）

```
┌─────────────────────────────────────────────────────────────┐
│                      UI层（可替换）                          │
│   ┌───────────────────────────────────────────────────────┐ │
│   │            VideoCallWindow                            │ │
│   │  ┌─────────────────────────────────────────────────┐  │ │
│   │  │ 职责:                                            │  │ │
│   │  │ - 纯UI展示和用户交互                             │  │ │
│   │  │ - 实现ICallUIObserver接收业务回调                │  │ │
│   │  │ - 通过ICallController*调用业务方法               │  │ │
│   │  │ - 不持有任何业务组件                             │  │ │
│   │  └─────────────────────────────────────────────────┘  │ │
│   └───────────────────────────────────────────────────────┘ │
└─────────────┬────────────────────┬──────────────────────────┘
              │ 实现               │ 调用
              ▼                    ▼
┌─────────────────────────┐  ┌─────────────────────────────┐
│   ICallUIObserver       │  │    ICallController          │
│   (UI回调接口)          │  │    (业务控制接口)            │
├─────────────────────────┤  ├─────────────────────────────┤
│ OnStartLocalRenderer()  │  │ Initialize()                │
│ OnStopLocalRenderer()   │  │ Shutdown()                  │
│ OnStartRemoteRenderer() │  │ ConnectToSignalServer()     │
│ OnLogMessage()          │  │ StartCall()                 │
│ OnShowError()           │  │ AcceptCall()                │
│ OnSignalConnected()     │  │ EndCall()                   │
│ OnClientListUpdate()    │  │ IsInCall()                  │
│ OnCallStateChanged()    │  │ GetCallState()              │
│ OnIncomingCall()        │  │ ...                         │
└──────────▲──────────────┘  └────────────┬────────────────┘
           │                              │
           │ 回调                         │ 实现
           │                              │
┌──────────┴───────────────────────────────▼─────────────────┐
│                     业务层（核心逻辑）                       │
│   ┌────────────────────────────────────────────────────┐   │
│   │            CallCoordinator                         │   │
│   │  ┌──────────────────────────────────────────────┐  │   │
│   │  │ 职责:                                         │  │   │
│   │  │ - 协调WebRTC引擎、信令、呼叫管理              │  │   │
│   │  │ - 实现所有业务逻辑                            │  │   │
│   │  │ - 持有ICallUIObserver*接口指针                │  │   │
│   │  │ - 持有所有底层组件                            │  │   │
│   │  │ - 完全与UI解耦，可独立测试                    │  │   │
│   │  └──────────────────────────────────────────────┘  │   │
│   └────────────────────────────────────────────────────┘   │
└─────────┬──────────────┬──────────────┬────────────────────┘
          │              │              │
          │ 管理         │ 管理         │ 管理
          ▼              ▼              ▼
┌─────────────────┐ ┌─────────────┐ ┌──────────────┐
│  WebRTCEngine   │ │SignalClient │ │ CallManager  │
│  (WebRTC核心)   │ │ (信令客户端)│ │ (呼叫管理器) │
└─────────────────┘ └─────────────┘ └──────────────┘

优点:
✅ 通过接口解耦，WebRTC组件可独立使用
✅ 职责清晰，每个类只做一件事
✅ 易于测试（使用Mock对象）
✅ 易于扩展（可替换UI实现）
✅ 无循环依赖
```

## 数据流向

### 用户发起呼叫流程
```
1. 用户点击"呼叫"按钮
   VideoCallWindow::OnCallButtonClicked()
           ▼
2. UI调用控制器接口
   controller_->StartCall("peer-id")
           ▼
3. 业务协调器处理请求
   CallCoordinator::StartCall()
           ▼
4. 调用底层组件
   call_manager_->InitiateCall()
           ▼
5. 回调通知UI
   ui_observer_->OnCallStateChanged()
           ▼
6. UI更新界面
   VideoCallWindow::OnCallStateChanged()
```

### WebRTC引擎回调流程
```
1. WebRTC事件发生（如收到视频轨道）
   WebRTCEngine::OnRemoteVideoTrackAdded()
           ▼
2. 通知观察者（CallCoordinator）
   observer_->OnRemoteVideoTrackAdded(track)
           ▼
3. 业务协调器处理
   CallCoordinator::OnRemoteVideoTrackAdded()
           ▼
4. 通知UI观察者
   ui_observer_->OnStartRemoteRenderer(track)
           ▼
5. UI更新显示
   VideoCallWindow::OnStartRemoteRenderer()
           ▼
6. 启动视频渲染器
   remote_renderer_->SetVideoTrack(track)
```

## 组件依赖关系

### 重构前（耦合）
```
MainWnd ←──── Conductor
  │              │
  │owns          │owns
  ▼              ▼
SignalClient   WebRTCEngine
CallManager

依赖方向混乱，相互依赖
```

### 重构后（单向依赖）
```
VideoCallWindow ──implements──→ ICallUIObserver
       │                              ▲
       │uses                          │observes
       ▼                              │
ICallController ←──implements── CallCoordinator
                                      │
                                      │owns
                                      ▼
                         ┌────────────┴────────────┐
                         ▼            ▼            ▼
                   WebRTCEngine  SignalClient  CallManager

依赖方向清晰，自上而下
```

## 可扩展性示例

### 添加命令行UI
```cpp
class ConsoleUI : public ICallUIObserver {
  void OnLogMessage(const std::string& msg, ...) override {
    std::cout << msg << std::endl;
  }
  
  void OnIncomingCall(const std::string& caller) override {
    std::cout << "Call from: " << caller << std::endl;
    controller_->AcceptCall();  // 自动接听
  }
  // ...
};

// 使用
CallCoordinator coordinator(env);
ConsoleUI console;
coordinator.SetUIObserver(&console);
```

### 添加Web界面（通过WebSocket）
```cpp
class WebSocketUI : public ICallUIObserver {
  void OnLogMessage(const std::string& msg, ...) override {
    SendToWebClient(json{{"type", "log"}, {"message", msg}});
  }
  
  void OnCallStateChanged(CallState state, ...) override {
    SendToWebClient(json{{"type", "call_state"}, {"state", state}});
  }
  // ...
};
```

### 添加单元测试
```cpp
class MockUIObserver : public ICallUIObserver {
  MOCK_METHOD(void, OnLogMessage, (const std::string&, const std::string&));
  MOCK_METHOD(void, OnIncomingCall, (const std::string&));
  // ...
};

TEST(CallCoordinatorTest, HandleIncomingCall) {
  MockUIObserver mock_ui;
  CallCoordinator coordinator(env);
  coordinator.SetUIObserver(&mock_ui);
  
  EXPECT_CALL(mock_ui, OnIncomingCall("caller-123")).Times(1);
  
  // 触发来电
  coordinator.SimulateIncomingCall("caller-123");
}
```

## 性能对比

| 指标 | 重构前 | 重构后 | 说明 |
|------|--------|--------|------|
| 接口调用开销 | 直接调用 | 虚函数调用 | 性能差异可忽略（纳秒级） |
| 内存占用 | 较少 | 略增 | 增加接口指针，约8字节 |
| 编译时间 | 较慢 | 较快 | 减少头文件依赖 |
| 可测试性 | 差 | 优秀 | 可使用Mock对象 |
| 可维护性 | 差 | 优秀 | 职责清晰 |
| 可复用性 | 差 | 优秀 | 完全解耦 |

## 关键改进点总结

1. **接口驱动**: 使用抽象接口而非具体类
2. **依赖注入**: 通过构造函数和setter注入依赖
3. **单一职责**: 每个类只做一件事
4. **开闭原则**: 对扩展开放，对修改关闭
5. **依赖倒置**: 高层不依赖低层，都依赖抽象
6. **接口隔离**: 接口设计精简，不强迫实现不需要的方法

---

**说明**: 这是一次典型的面向接口编程重构，遵循SOLID原则，大幅提升了代码质量。
