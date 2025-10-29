# WebRTC 信令服务器

这是一个独立的WebRTC信令服务器，同时支持C++客户端和Web应用。

## 功能特性

- ✅ 支持C++客户端和Web客户端（统一的连接方式）
- ✅ 通过URL参数传递客户端ID（`/ws/webrtc?uid=xxx`）
- ✅ 提供静态文件服务（Web应用界面）
- ✅ 自动处理重复登录
- ✅ 实时广播客户端列表
- ✅ 心跳检测和自动重连
- ✅ ICE服务器配置（STUN/TURN）

## 快速开始

### 1. 编译

```bash
cd webrtc-signaling-server
go build -o webrtc-signaling-server.exe
```

### 2. 运行

```bash
# 使用默认配置运行
.\webrtc-signaling-server.exe

# 自定义端口
.\webrtc-signaling-server.exe -addr=:9000
```

### 3. 访问

服务器启动后，你会看到：

```
WebRTC信令服务器启动在 :8081
WebSocket地址: ws://localhost:8081/ws/webrtc?uid=YOUR_CLIENT_ID
```

Web应用访问地址: `http://localhost:8081`（自动连接到 `ws://localhost:8081/ws/webrtc?uid=xxx`）

## 客户端接入

### 统一连接方式

**C++客户端和Web客户端都使用相同的连接方式**：

```
ws://server:port/ws/webrtc?uid=YOUR_CLIENT_ID
```

#### C++客户端

```cpp
// 连接示例
QString url = "ws://localhost:8081/ws/webrtc?uid=cpp_client_001";
websocket->open(QUrl(url));
```

#### Web客户端

```javascript
// JavaScript连接示例（已在signaling-manager.js中实现）
const clientId = 'web_client_001';
const wsUrl = `ws://localhost:8081/ws/webrtc?uid=${clientId}`;
const socket = new WebSocket(wsUrl);
```

连接成功后会自动收到`registered`消息，包含ICE服务器配置。

## 消息协议

### 注册确认 (registered)

客户端连接后，服务器会自动发送注册确认消息：

```json
{
    "type": "registered",
    "from": "client_id",
    "payload": {
        "iceServers": [
            {
                "urls": ["stun:stun.l.google.com:19302"]
            },
            {
                "urls": ["turn:113.46.159.182:3478"],
                "username": "myuser",
                "credential": "mypassword"
            }
        ]
    }
}
```

### 客户端列表 (client-list)

**请求**:
```json
{
    "type": "list-clients"
}
```

**响应**:
```json
{
    "type": "client-list",
    "payload": {
        "clients": [
            {"id": "client_1"},
            {"id": "client_2"}
        ]
    }
}
```

### 信令消息转发

所有信令消息（offer、answer、ice-candidate等）会自动转发到目标客户端。

**发送方**:
```json
{
    "type": "offer",
    "from": "client_1",
    "to": "client_2",
    "payload": {
        "sdp": "..."
    }
}
```

**接收方**收到:
```json
{
    "type": "offer",
    "from": "client_1",
    "to": "client_2",
    "payload": {
        "sdp": "..."
    }
}
```

### 支持的消息类型

- `register` - 注册客户端（可选，客户端已通过URL注册）
- `registered` - 注册确认（服务器发送）
- `list-clients` - 请求客户端列表
- `client-list` - 客户端列表（服务器发送）
- `user-offline` - 用户下线通知（服务器发送）
- `offer` - WebRTC Offer
- `answer` - WebRTC Answer
- `ice-candidate` - ICE候选
- `conflict-resolution` - 冲突解决
- `call-request` - 呼叫请求
- `call-response` - 呼叫响应
- `call-cancel` - 取消呼叫
- `call-end` - 结束通话

## ICE服务器配置

服务器配置了以下ICE服务器：

### STUN服务器
- `stun:stun.l.google.com:19302`
- `stun:stun1.l.google.com:19302`

### TURN服务器
- `turn:113.46.159.182:3478` (UDP)
- `turn:113.46.159.182:3478?transport=tcp` (TCP)
- `turn:openrelay.metered.ca:80` (备用)
- `turn:openrelay.metered.ca:443` (备用)

可以在`getIceServers()`函数中修改配置。

## 命令行参数

```bash
-addr string
    HTTP服务地址 (默认 ":8081")
```

## 目录结构

```
webrtc-signaling-server/
├── main.go                 # 主程序
├── go.mod                  # Go模块定义
├── go.sum                  # 依赖校验
├── README.md              # 本文档
├── static/                # Web应用静态文件
│   ├── index.html
│   ├── css/
│   └── js/
│       ├── app.js
│       ├── signaling-manager.js
│       ├── webrtc-manager.js
│       └── ...
└── webrtc-signaling-server.exe  # 编译后的可执行文件
```

## 日志输出

服务器会输出详细的日志信息：

```
2025/10/29 15:30:00 main.go:60: WebRTC信令服务器启动在 :8081
2025/10/29 15:30:05 main.go:130: 新的WebRTC连接: uid=cpp_client, remote=127.0.0.1:12345
2025/10/29 15:30:05 main.go:165: WebRTC客户端连接成功: cpp_client (当前在线: 1)
2025/10/29 15:30:06 main.go:175: 发送注册消息给客户端: cpp_client
2025/10/29 15:30:10 main.go:130: 新的WebRTC连接: uid=web_client, remote=127.0.0.1:12346
2025/10/29 15:30:10 main.go:165: WebRTC客户端连接成功: web_client (当前在线: 2)
2025/10/29 15:30:15 main.go:324: 收到消息: type=offer, from=cpp_client, to=web_client
2025/10/29 15:30:15 main.go:298: ✓ 消息转发: cpp_client -> web_client (type: offer)
```

## 健康检查

服务器提供健康检查端点：

```bash
curl http://localhost:8081/health
# 返回: OK
```

## 部署建议

### 开发环境
直接运行即可：
```bash
.\webrtc-signaling-server.exe
```

### 生产环境

1. **使用HTTPS/WSS**
   建议使用Nginx反向代理添加SSL支持

2. **配置TURN服务器**
   修改`getIceServers()`配置自己的TURN服务器

3. **使用进程管理**
   使用systemd、supervisor或Windows服务管理器

4. **监控和日志**
   将日志输出到文件：
   ```bash
   .\webrtc-signaling-server.exe >> log.txt 2>&1
   ```

## 与C++客户端集成

C++客户端代码示例：

```cpp
// 在 video_call_window.cc 中
QString serverUrl = "ws://localhost:8081/ws/webrtc?uid=" + clientId;
signaling_client_->Connect(serverUrl);
```

## 与Web应用集成

Web应用已经配置好，直接访问：
```
http://localhost:8081
```

Web应用会自动连接到 `ws://localhost:8081/ws/webrtc?uid=xxx`

## 连接方式说明

### 为什么使用URL参数？

C++客户端和Web客户端都使用统一的连接方式 `/ws/webrtc?uid=xxx`，原因如下：

1. **即时识别**: 服务器在连接建立时立即知道客户端ID，无需等待注册消息
2. **简化逻辑**: 避免了"临时连接 → 注册消息 → 转为正式连接"的复杂状态管理
3. **更可靠**: 不依赖客户端发送注册消息，减少了潜在的时序问题
4. **统一接口**: C++和Web使用完全相同的连接方式，代码更统一

### register消息还有用吗？

虽然客户端已通过URL参数注册，但仍然可以发送 `register` 消息来保持兼容性。服务器会忽略这个消息（因为客户端已经注册）。

## 故障排查

### 问题1: 端口被占用
```
启动服务器失败: listen tcp :8081: bind: Only one usage of each socket address
```

**解决**: 更改端口
```bash
.\webrtc-signaling-server.exe -addr=:9000
```

### 问题2: 连接失败

检查浏览器控制台，确保：
1. WebSocket URL正确（`ws://localhost:8081/ws/webrtc?uid=xxx`）
2. uid参数已正确添加
3. 没有被防火墙拦截

### 问题3: C++客户端连接失败

确保：
1. URL格式正确: `ws://server:port/ws/webrtc?uid=YOUR_CLIENT_ID`
2. uid不为空
3. 服务器已启动

## 许可证

遵循项目主LICENSE
