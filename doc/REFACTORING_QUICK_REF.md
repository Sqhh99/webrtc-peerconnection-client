# 重构快速参考

## 核心改动

### 新增文件
- `include/icall_observer.h` - 接口定义
- `include/call_coordinator.h` - 业务协调器头文件
- `src/call_coordinator.cc` - 业务协调器实现
- `include/video_call_window.h` - 视频通话窗口头文件
- `src/video_call_window.cc` - 视频通话窗口实现

### 修改文件
- `src/main.cc` - 更新为使用新架构
- `CMakeLists.txt` - 添加新文件到构建

### 保留文件（待移除）
- `include/conductor.h`
- `src/conductor.cc`
- `include/mainwindow.h`
- `src/mainwindow.cc`

## 架构对比

### 旧架构
```
MainWnd ←→ Conductor ←→ WebRTC/Signal/Call
  ↓                         ↓
持有业务组件              强依赖UI
```

### 新架构
```
VideoCallWindow → ICallController (接口)
       ↑                    ↓
ICallUIObserver ← CallCoordinator → WebRTC/Signal/Call
  (接口)           (持有所有组件)
```

## 关键接口

### ICallUIObserver（业务→UI）
```cpp
// UI需要实现的回调方法
OnStartLocalRenderer()    // 开始本地视频
OnStopLocalRenderer()     // 停止本地视频
OnStartRemoteRenderer()   // 开始远程视频
OnStopRemoteRenderer()    // 停止远程视频
OnLogMessage()            // 日志消息
OnShowError()             // 显示错误
OnSignalConnected()       // 信令已连接
OnClientListUpdate()      // 客户端列表更新
OnCallStateChanged()      // 呼叫状态改变
OnIncomingCall()          // 来电通知
```

### ICallController（UI→业务）
```cpp
// UI可以调用的业务方法
Initialize()                          // 初始化
Shutdown()                            // 关闭
ConnectToSignalServer(url, id)        // 连接信令服务器
DisconnectFromSignalServer()          // 断开信令服务器
StartCall(peer_id)                    // 发起呼叫
AcceptCall()                          // 接听呼叫
RejectCall(reason)                    // 拒绝呼叫
EndCall()                             // 结束呼叫
IsConnectedToSignalServer()           // 是否已连接
IsInCall()                            // 是否在通话中
GetCallState()                        // 获取呼叫状态
GetCurrentPeerId()                    // 获取当前对等端ID
GetClientId()                         // 获取客户端ID
```

## 使用模式

### 初始化
```cpp
// 1. 创建协调器
auto coordinator = std::make_unique<CallCoordinator>(env);

// 2. 创建窗口（传入协调器接口）
VideoCallWindow window(coordinator.get());

// 3. 设置观察者
coordinator->SetUIObserver(&window);

// 4. 初始化
coordinator->Initialize();
```

### 连接服务器
```cpp
// UI层调用
controller_->ConnectToSignalServer(
    "ws://localhost:8081/ws/webrtc",
    "my-client-id"
);
```

### 发起呼叫
```cpp
// UI层调用
controller_->StartCall("target-peer-id");
```

### 处理来电
```cpp
// CallCoordinator调用UI观察者
void CallCoordinator::OnIncomingCall(const std::string& caller_id) {
  if (ui_observer_) {
    ui_observer_->OnIncomingCall(caller_id);
  }
}

// VideoCallWindow实现回调
void VideoCallWindow::OnIncomingCall(const std::string& caller_id) {
  // 显示对话框询问用户
  if (user_accepts) {
    controller_->AcceptCall();
  } else {
    controller_->RejectCall("用户拒绝");
  }
}
```

## 线程安全

所有UI回调都通过`QMetaObject::invokeMethod`确保在主线程执行：

```cpp
void CallCoordinator::OnLogMessage(...) {
  if (ui_observer_) {
    ui_observer_->OnLogMessage(message, level);
  }
}

void VideoCallWindow::OnLogMessage(...) {
  QMetaObject::invokeMethod(this, [this, message, level]() {
    // 在主线程中执行
    AppendLogInternal(QString::fromStdString(message), ...);
  }, Qt::QueuedConnection);
}
```

## 编译和运行

### 编译
```bash
cd build
cmake ..
cmake --build . --config Release
```

### 运行
```bash
.\Release\peerconnection_client.exe
```

## 常见问题

### Q: 为什么要使用接口？
A: 解耦UI和业务逻辑，提升可复用性和可测试性。

### Q: 旧代码什么时候删除？
A: 确认新代码稳定后可以删除conductor.*和mainwindow.*文件。

### Q: 如何添加新的UI实现？
A: 只需实现`ICallUIObserver`接口，然后设置给`CallCoordinator`即可。

### Q: 业务逻辑在哪里？
A: 都在`CallCoordinator`中，它协调所有底层组件。

### Q: 如何进行单元测试？
A: 创建Mock对象实现接口，不需要真实UI环境。

## 性能影响

- **接口调用开销**: 可忽略（虚函数调用）
- **内存占用**: 略微增加（接口指针）
- **线程切换**: 与之前相同（Qt信号槽）
- **整体性能**: 无明显影响

## 后续优化建议

1. 移除旧的conductor和mainwindow文件
2. 添加更多单元测试
3. 完善错误处理
4. 添加性能监控接口
5. 考虑添加命令模式支持撤销/重做

---

**更新日期**: 2025年10月20日
