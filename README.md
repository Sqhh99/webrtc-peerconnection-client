# WebRTC 视频通话客户端

基于 WebRTC M120+ 的跨平台视频通话应用，支持 Qt GUI 和 Web 客户端互通。

## 功能特性

- ✅ 点对点视频通话
- ✅ 音频通话
- ✅ 现代化 Qt 界面
- ✅ Web 客户端支持
- ✅ 独立的信令服务器
- ✅ C++ 客户端与 Web 客户端互通

## 系统要求

- Windows 10/11
- Visual Studio 2019/2022
- CMake 3.15+
- Qt 6.6.3+
- Go 1.20+ (用于信令服务器)

## 构建步骤

### 1. 下载 WebRTC 源码

```bash
# 安装 depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
set PATH=%PATH%;C:\path\to\depot_tools

# 下载 WebRTC
mkdir webrtc-checkout
cd webrtc-checkout
fetch --nohooks webrtc
gclient sync

# 切换到 src 目录
cd src

# 生成构建文件
# Debug 版本
gn gen out\Debug --ide=vs2022 --args='is_debug=true use_custom_libcxx=false rtc_include_tests=false'

# Release 版本
gn gen out\Release --ide=vs2022 --args='is_debug=false use_custom_libcxx=false rtc_include_tests=false'

# 编译
ninja -C out\Release
```

### 2. 编译依赖库

#### Abseil 库
```bash
cd D:\webrtc-checkout\src\third_party\abseil-cpp
mkdir build && cd build

cmake .. -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_STANDARD=20 ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DABSL_PROPAGATE_CXX_STD=ON ^
  -DABSL_BUILD_TESTING=OFF ^
  -DCMAKE_INSTALL_PREFIX=install

cmake --build . --config Release
cmake --install . --config Release
```

#### JsonCpp 库
```bash
cd D:\webrtc-checkout\src\third_party\jsoncpp
mkdir build_static && cd build_static

cmake ../source -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_STANDARD=20 ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DJSONCPP_WITH_TESTS=OFF ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DCMAKE_INSTALL_PREFIX=install

cmake --build . --config Release
cmake --install . --config Release
```

### 3. 编译客户端

```bash
cd peerconnection\client

# 生成项目文件
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.6.3\msvc2019_64

# 编译
cmake --build build --config Release
```

### 4. 运行信令服务器

```bash
cd webrtc-signaling-server
go run main.go
```

服务器将在 `http://localhost:8081` 启动

### 5. 运行客户端

**C++ 客户端**:
```bash
cd build\Release
peerconnection_client.exe
```

**Web 客户端**:  
在浏览器中打开 `http://localhost:8081`

## 项目结构

```
peerconnection/client/
├── include/              # 头文件
├── src/                  # 源代码
├── test/                 # 测试相关
├── doc/                  # 文档
├── webrtc-signaling-server/  # Go 信令服务器
│   ├── main.go
│   └── static/          # Web 客户端
└── CMakeLists.txt       # CMake 配置
```

## 使用方法

1. 启动信令服务器
2. 启动多个客户端（C++ 或 Web）
3. 点击"连接"按钮连接到服务器
4. 在用户列表中双击目标用户发起通话

## 配置说明

### 信令服务器地址
默认: `ws://localhost:8081/ws/webrtc`

### ICE 服务器
项目配置了以下 STUN/TURN 服务器：
- Google STUN: `stun:stun.l.google.com:19302`
- 自定义 TURN: `turn:113.46.159.182:3478`

## 常见问题

### 编译错误

如果遇到 WebRTC API 相关错误，请参考 `doc/WEBRTC_API_MIGRATION.md`

### 跨平台兼容性

如果 C++ 客户端和 Web 客户端无法互通，请参考 `doc/CROSS_PLATFORM_COMPATIBILITY_FIX.md`

## 开发文档

- [架构设计](doc/ARCHITECTURE_EVOLUTION.md)
- [重构指南](doc/REFACTORING_GUIDE.md)
- [API 迁移](doc/WEBRTC_API_MIGRATION.md)
- [UI 美化](doc/UI_BEAUTIFICATION.md)

## 技术栈

- **WebRTC**: M120+
- **UI 框架**: Qt 6.6.3
- **构建系统**: CMake 3.15+
- **信令服务器**: Go + WebSocket
- **Web 前端**: 原生 JavaScript + Bootstrap 5