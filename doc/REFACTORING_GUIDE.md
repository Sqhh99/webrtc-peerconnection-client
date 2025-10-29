# WebRTC客户端架构重构指南

## 重构概述

本次重构的目标是解决原有架构中`Conductor`强依赖`MainWnd`的问题，提升代码的可复用性、可维护性和可测试性。

## 重构前的问题

### 1. **强耦合问题**
- `Conductor`直接持有`MainWnd`指针
- WebRTC组件难以在其他场景复用
- 测试困难，必须创建完整的UI环境

### 2. **职责不清**
- `MainWnd`既管理UI又管理业务逻辑（SignalClient、CallManager）
- `Conductor`需要了解UI的具体实现细节
- 难以扩展和维护

### 3. **命名不够语义化**
- `MainWnd`名称不能体现其职责
- `Conductor`的名称过于通用

## 重构后的架构

### 新架构分层

```
┌─────────────────────────────────────────────────────┐
│                    UI层（可替换）                      │
│  ┌───────────────────────────────────────────────┐  │
│  │     VideoCallWindow (原MainWnd)               │  │
│  │     职责: 纯UI展示和用户交互                    │  │
│  └───────────────────────────────────────────────┘  │
└──────────────────┬──────────────────────────────────┘
                   │ 实现接口
                   ▼
┌─────────────────────────────────────────────────────┐
│               接口层（解耦关键）                        │
│  ┌───────────────────┐  ┌───────────────────────┐  │
│  │ ICallUIObserver   │  │  ICallController      │  │
│  │ (UI回调接口)       │  │  (业务控制接口)        │  │
│  └───────────────────┘  └───────────────────────┘  │
└──────────────────┬──────────────────────────────────┘
                   │ 观察者模式
                   ▼
┌─────────────────────────────────────────────────────┐
│                  业务层（核心逻辑）                     │
│  ┌───────────────────────────────────────────────┐  │
│  │    CallCoordinator (原Conductor)              │  │
│  │    职责: 协调WebRTC、信令、呼叫管理            │  │
│  └───────────────────────────────────────────────┘  │
└──────────────────┬──────────────────────────────────┘
                   │ 管理和协调
                   ▼
┌─────────────────────────────────────────────────────┐
│                 引擎层（可复用）                        │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐   │
│  │ WebRTC     │  │ Signal     │  │ Call       │   │
│  │ Engine     │  │ Client     │  │ Manager    │   │
│  └────────────┘  └────────────┘  └────────────┘   │
└─────────────────────────────────────────────────────┘
```

## 核心改进

### 1. **接口驱动设计**

#### ICallUIObserver - UI回调接口
```cpp
class ICallUIObserver {
 public:
  virtual void OnStartLocalRenderer(webrtc::VideoTrackInterface* track) = 0;
  virtual void OnStopLocalRenderer() = 0;
  virtual void OnStartRemoteRenderer(webrtc::VideoTrackInterface* track) = 0;
  virtual void OnStopRemoteRenderer() = 0;
  virtual void OnLogMessage(const std::string& message, const std::string& level) = 0;
  virtual void OnShowError(const std::string& title, const std::string& message) = 0;
  virtual void OnSignalConnected(const std::string& client_id) = 0;
  virtual void OnSignalDisconnected() = 0;
  // ... 更多回调
};
```

**优点:**
- 业务层不需要知道UI的具体实现
- 可以轻松替换不同的UI实现
- 便于单元测试（使用Mock对象）

#### ICallController - 业务控制接口
```cpp
class ICallController {
 public:
  virtual bool Initialize() = 0;
  virtual void Shutdown() = 0;
  virtual void ConnectToSignalServer(const std::string& url, const std::string& client_id) = 0;
  virtual void StartCall(const std::string& peer_id) = 0;
  virtual void AcceptCall() = 0;
  virtual void EndCall() = 0;
  virtual bool IsInCall() const = 0;
  // ... 更多控制方法
};
```

**优点:**
- UI层通过统一接口调用业务逻辑
- 隐藏内部实现细节
- 便于添加新功能而不影响UI

### 2. **职责分离**

#### CallCoordinator（业务协调器）
- **原名**: Conductor
- **职责**: 
  - 协调WebRTC引擎、信令客户端和呼叫管理器
  - 实现业务逻辑流程
  - 管理内部组件生命周期
- **特点**: 
  - 完全与UI解耦
  - 可独立测试
  - 可在不同场景复用

#### VideoCallWindow（视频通话窗口）
- **原名**: MainWnd
- **职责**: 
  - 纯UI展示和用户交互
  - 实现ICallUIObserver接收回调
  - 通过ICallController控制业务
- **特点**: 
  - 不持有业务组件
  - 更容易理解和维护
  - 可替换为其他UI实现

### 3. **组件管理改进**

#### 重构前
```cpp
// MainWnd持有业务组件
class MainWnd {
  std::unique_ptr<SignalClient> signal_client_;
  std::unique_ptr<CallManager> call_manager_;
  // Conductor需要通过MainWnd访问这些组件
};

// Conductor依赖MainWnd
class Conductor {
  MainWnd* main_wnd_;  // 强依赖
  // 需要调用 main_wnd_->GetSignalClient()
};
```

#### 重构后
```cpp
// CallCoordinator自己管理所有业务组件
class CallCoordinator : public ICallController {
  std::unique_ptr<WebRTCEngine> webrtc_engine_;
  std::unique_ptr<SignalClient> signal_client_;
  std::unique_ptr<CallManager> call_manager_;
  ICallUIObserver* ui_observer_;  // 仅持有接口
};

// VideoCallWindow只负责UI
class VideoCallWindow : public ICallUIObserver {
  ICallController* controller_;  // 仅持有接口
  // 通过controller_->StartCall()等方法控制业务
};
```

## 使用示例

### 基本用法

```cpp
// main.cc
int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  
  // 1. 创建业务协调器
  auto coordinator = std::make_unique<CallCoordinator>(env);
  coordinator->Initialize();
  
  // 2. 创建UI窗口
  VideoCallWindow window(coordinator.get());
  
  // 3. 建立观察者关系
  coordinator->SetUIObserver(&window);
  
  // 4. 显示窗口
  window.show();
  
  // 5. 运行应用
  return app.exec();
}
```

### 扩展示例：创建自定义UI

```cpp
// 实现一个简化版的控制台UI
class ConsoleUI : public ICallUIObserver {
 public:
  void OnLogMessage(const std::string& message, const std::string& level) override {
    std::cout << "[" << level << "] " << message << std::endl;
  }
  
  void OnIncomingCall(const std::string& caller_id) override {
    std::cout << "Incoming call from: " << caller_id << std::endl;
    // 自动接听
    controller_->AcceptCall();
  }
  
  // 实现其他接口方法...
  
 private:
  ICallController* controller_;
};

// 使用
int main() {
  auto coordinator = std::make_unique<CallCoordinator>(env);
  ConsoleUI console_ui;
  
  coordinator->SetUIObserver(&console_ui);
  coordinator->Initialize();
  
  // 启动呼叫
  coordinator->ConnectToSignalServer("ws://localhost:8081", "console-client");
  coordinator->StartCall("peer-123");
}
```

### 单元测试示例

```cpp
// 创建Mock对象进行测试
class MockUIObserver : public ICallUIObserver {
 public:
  MOCK_METHOD(void, OnLogMessage, (const std::string&, const std::string&), (override));
  MOCK_METHOD(void, OnIncomingCall, (const std::string&), (override));
  // ... 其他Mock方法
};

// 测试
TEST(CallCoordinatorTest, IncomingCallNotification) {
  MockUIObserver mock_ui;
  CallCoordinator coordinator(env);
  coordinator.SetUIObserver(&mock_ui);
  
  // 期望OnIncomingCall被调用
  EXPECT_CALL(mock_ui, OnIncomingCall("test-caller"))
    .Times(1);
  
  // 模拟收到呼叫
  coordinator.SimulateIncomingCall("test-caller");
}
```

## 迁移指南

### 从旧代码迁移

#### 步骤1: 替换头文件引用
```cpp
// 旧代码
#include "conductor.h"
#include "mainwindow.h"

// 新代码
#include "call_coordinator.h"
#include "video_call_window.h"
#include "icall_observer.h"
```

#### 步骤2: 更新对象创建
```cpp
// 旧代码
MainWnd main_wnd;
auto conductor = std::make_unique<Conductor>(env, &main_wnd);
main_wnd.SetConductor(conductor.get());

// 新代码
auto coordinator = std::make_unique<CallCoordinator>(env);
VideoCallWindow window(coordinator.get());
coordinator->SetUIObserver(&window);
```

#### 步骤3: 更新业务调用
```cpp
// 旧代码（直接访问组件）
main_wnd.GetSignalClient()->Connect(url, id);
main_wnd.GetCallManager()->StartCall(peer_id);

// 新代码（通过接口调用）
coordinator->ConnectToSignalServer(url, id);
coordinator->StartCall(peer_id);
```

## 文件对照表

| 重构前 | 重构后 | 说明 |
|--------|--------|------|
| `conductor.h/cc` | `call_coordinator.h/cc` | 业务协调器 |
| `mainwindow.h/cc` | `video_call_window.h/cc` | UI窗口 |
| - | `icall_observer.h` | 接口定义（新增） |
| `webrtcengine.h/cc` | 保持不变 | WebRTC引擎 |
| `signalclient.h/cc` | 保持不变 | 信令客户端 |
| `callmanager.h/cc` | 保持不变 | 呼叫管理器 |

## 优势总结

### 1. **可复用性**
- WebRTC组件可以在不同场景使用（桌面应用、Web服务、移动应用）
- 业务逻辑与UI完全解耦

### 2. **可维护性**
- 职责清晰，每个类只做一件事
- 接口稳定，内部实现可以自由修改
- 更容易定位和修复问题

### 3. **可测试性**
- 可以使用Mock对象进行单元测试
- 不需要启动UI即可测试业务逻辑
- 测试覆盖率更高

### 4. **可扩展性**
- 可以轻松添加新的UI实现（控制台、Web界面等）
- 可以替换底层组件而不影响上层
- 支持插件化架构

### 5. **团队协作**
- UI开发和业务逻辑开发可以并行
- 接口定义清晰，减少沟通成本
- 代码review更容易

## 注意事项

### 线程安全
- UI回调统一使用`QMetaObject::invokeMethod`确保线程安全
- 所有跨线程调用都通过Qt的信号槽机制

### 生命周期管理
- `CallCoordinator`管理所有业务组件的生命周期
- UI层只持有接口指针，不负责销毁
- 确保在关闭应用前调用`Shutdown()`

### 向后兼容
- 旧文件暂时保留在项目中（已注释）
- 可以平滑过渡，逐步迁移
- CMakeLists.txt中明确标注了新旧文件

## 下一步改进建议

1. **添加日志系统**: 统一的日志接口和实现
2. **配置管理**: 将硬编码的配置提取到配置文件
3. **错误处理**: 更完善的错误处理和恢复机制
4. **性能监控**: 添加性能指标收集接口
5. **插件系统**: 支持动态加载扩展功能
6. **国际化**: 分离UI文本，支持多语言

## 相关资源

- [设计模式 - 观察者模式](https://refactoring.guru/design-patterns/observer)
- [依赖倒置原则](https://en.wikipedia.org/wiki/Dependency_inversion_principle)
- [接口隔离原则](https://en.wikipedia.org/wiki/Interface_segregation_principle)
- [Qt信号槽机制](https://doc.qt.io/qt-6/signalsandslots.html)

---

**重构日期**: 2025年10月20日  
**重构人**: AI Assistant  
**版本**: v2.0
