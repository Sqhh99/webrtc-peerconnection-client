# WebRTC 点对点音视频通话客户端

基于 WebRTC 和 Qt 6 的跨平台音视频通话应用。

## 快速开始

### 环境要求
- Windows 10/11
- MSVC 2019+
- CMake 3.15+
- Qt 6.6.3
- WebRTC 111
- Go 1.19+ (信令服务器)

### 编译运行

1. **编译客户端**
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

2. **启动信令服务器**
```bash
cd webrtc
go run main.go
```

3. **运行客户端**
```bash
.\Release\peerconnection_client.exe
```

## 功能特性

- ✅ 点对点音视频通话
- ✅ 自动设备检测
- ✅ 实时视频渲染
- ✅ 呼叫状态管理
- ✅ 自动重连机制
- ✅ 客户端列表显示
- ✅ 完善的错误处理

## 架构特点

- **分层设计**: UI 层、连接层、引擎层完全解耦
- **观察者模式**: 组件间松耦合通信
- **线程安全**: Qt 信号槽机制保证线程安全
- **状态管理**: 完善的呼叫状态机

## 项目结构

```
client/
├── include/          # 头文件
├── src/             # 源代码
├── test/            # 测试工具
├── webrtc/          # 信令服务器
└── build/           # 编译输出
```

## 核心组件

- **WebRTCEngine**: WebRTC 核心功能封装
- **Conductor**: 连接层协调器
- **SignalClient**: WebSocket 信令客户端
- **CallManager**: 呼叫状态管理
- **MainWnd**: Qt 主窗口界面
- **VideoRenderer**: 视频流渲染

## 详细文档

📖 **完整文档请查看**: [PROJECT_GUIDE.md](PROJECT_GUIDE.md)

包含以下内容：
- 详细架构说明
- 完整 API 协议文档
- 呼叫流程图解
- 故障排除指南
- 扩展开发建议

## 信令协议

支持以下消息类型：
- `register` - 客户端注册
- `list-clients` - 获取客户端列表
- `call-request` - 发起呼叫
- `call-response` - 响应呼叫
- `offer` / `answer` - SDP 交换
- `ice-candidate` - ICE 候选交换
- `call-end` - 结束通话

详细协议格式请参考 [PROJECT_GUIDE.md](PROJECT_GUIDE.md)

## 许可证

BSD-3-Clause License (与 WebRTC 保持一致)

---

**最后更新**: 2025年10月15日  
**项目状态**: ✅ 生产可用
