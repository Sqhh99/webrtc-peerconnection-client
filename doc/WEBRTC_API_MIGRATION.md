# WebRTC API 迁移指南

## 概述

本文档记录了从旧版 WebRTC API 迁移到新版 API 的变化。

## API 变化清单

### 1. 头文件路径变化

**旧版API**:
```cpp
#include "api/stats/rtcstatscollectorcallback.h"  // 驼峰命名
```

**新版API**:
```cpp
#include "api/stats/rtc_stats_collector_callback.h"  // 下划线命名
```

**修改原因**: 统一头文件命名风格，使用下划线分隔。

---

### 2. RTCStatsReport 时间戳

**旧版API**:
```cpp
report->timestamp_us()  // 直接返回微秒数（uint64_t）
```

**新版API**:
```cpp
report->timestamp().us()  // 返回 Timestamp 对象，需要调用 .us() 获取微秒数
```

**修改原因**: 新版使用了更强类型的时间戳对象。

---

### 2. RTCStatsReport 时间戳

**旧版API**:
```cpp
report->timestamp_us()  // 直接返回微秒数（uint64_t）
```

**新版API**:
```cpp
report->timestamp().us()  // 返回 Timestamp 对象，需要调用 .us() 获取微秒数
```

**修改原因**: 新版使用了更强类型的时间戳对象。

---

### 3. RTP Stream Stats 类型名称

**旧版API**:
```cpp
webrtc::RTCInboundRTPStreamStats   // RTP 全大写
webrtc::RTCOutboundRTPStreamStats  // RTP 全大写
```

**新版API**:
```cpp
webrtc::RTCInboundRtpStreamStats   // Rtp 驼峰命名
webrtc::RTCOutboundRtpStreamStats  // Rtp 驼峰命名
```

**修改原因**: 统一命名风格，采用驼峰命名法。

---

### 3. RTP Stream Stats 类型名称

**旧版API**:
```cpp
webrtc::RTCInboundRTPStreamStats   // RTP 全大写
webrtc::RTCOutboundRTPStreamStats  // RTP 全大写
```

**新版API**:
```cpp
webrtc::RTCInboundRtpStreamStats   // Rtp 驼峰命名
webrtc::RTCOutboundRtpStreamStats  // Rtp 驼峰命名
```

**修改原因**: 统一命名风格，采用驼峰命名法。

---

### 4. Optional 字段访问方法

**旧版API**:
```cpp
stat->bytes_received.ValueOrDefault(0u)
stat->kind.ValueOrDefault("")
pair->selected.ValueOrDefault(false)
```

**新版API**:
```cpp
stat->bytes_received.value_or(0u)
stat->kind.value_or("")
// selected 字段已移除，使用 nominated 和 state 代替
```

**修改原因**: 
- 使用标准 C++ `std::optional::value_or()` 方法
- WebRTC 自定义的 `ValueOrDefault()` 已弃用

---

### 4. Optional 字段访问方法

**旧版API**:
```cpp
stat->bytes_received.ValueOrDefault(0u)
stat->kind.ValueOrDefault("")
pair->selected.ValueOrDefault(false)
```

**新版API**:
```cpp
stat->bytes_received.value_or(0u)
stat->kind.value_or("")
// selected 字段已移除，使用 nominated 和 state 代替
```

**修改原因**: 
- 使用标准 C++ `std::optional::value_or()` 方法
- WebRTC 自定义的 `ValueOrDefault()` 已弃用

---

### 5. ICE Candidate Pair 选择判断

**旧版API**:
```cpp
if (pair->selected.ValueOrDefault(false)) {
    // 这是选中的候选对
}
```

**新版API**:
```cpp
if (pair->nominated.value_or(false) && pair->state.has_value()) {
    std::string state_str = *pair->state;
    if (state_str == "succeeded") {
        // 这是选中的候选对
    }
}
```

**修改原因**: 
- `selected` 字段已移除
- 新版使用 `nominated` + `state` 组合来判断
- `state` 为字符串类型，值为 "succeeded" 表示连接成功

---

## 完整修改示例

### 修改前
```cpp
RtcStatsSnapshot snapshot;
snapshot.timestamp_ms = 
    static_cast<uint64_t>(report->timestamp_us() / 1000);

const webrtc::RTCInboundRTPStreamStats* audio_inbound = nullptr;
const auto inbound_stats = report->GetStatsOfType<webrtc::RTCInboundRTPStreamStats>();
for (const auto* stat : inbound_stats) {
    inbound_bytes += stat->bytes_received.ValueOrDefault(0u);
    const std::string kind = stat->kind.ValueOrDefault("");
}

const auto candidate_pairs = report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();
for (const auto* pair : candidate_pairs) {
    if (pair->selected.ValueOrDefault(false)) {
        selected_pair = pair;
        break;
    }
}

const double rtt_seconds = 
    selected_pair->current_round_trip_time.ValueOrDefault(0.0);
```

### 修改后
```cpp
RtcStatsSnapshot snapshot;
snapshot.timestamp_ms = 
    static_cast<uint64_t>(report->timestamp().us() / 1000);

const webrtc::RTCInboundRtpStreamStats* audio_inbound = nullptr;
const auto inbound_stats = report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();
for (const auto* stat : inbound_stats) {
    inbound_bytes += stat->bytes_received.value_or(0u);
    const std::string kind = stat->kind.value_or("");
}

const auto candidate_pairs = report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();
for (const auto* pair : candidate_pairs) {
    if (pair->nominated.value_or(false) && pair->state.has_value()) {
        std::string state_str = *pair->state;
        if (state_str == "succeeded") {
            selected_pair = pair;
            break;
        }
    }
}

const double rtt_seconds = 
    selected_pair->current_round_trip_time.value_or(0.0);
```

---

## 迁移检查清单

- [x] 将头文件 `api/stats/rtcstatscollectorcallback.h` 改为 `api/stats/rtc_stats_collector_callback.h`
- [x] 将 `timestamp_us()` 改为 `timestamp().us()`
- [x] 将 `RTCInboundRTPStreamStats` 改为 `RTCInboundRtpStreamStats`
- [x] 将 `RTCOutboundRTPStreamStats` 改为 `RTCOutboundRtpStreamStats`
- [x] 将所有 `ValueOrDefault()` 改为 `value_or()`
- [x] 将 `pair->selected` 改为 `pair->nominated` + `pair->state` 判断

---

## 兼容性说明

- **WebRTC 版本**: M120+
- **C++ 标准**: C++17 或更高（需要 `std::optional`）
- **命名空间**: 统一使用 `webrtc::`（不是 `rtc::`）

---

## 常见错误及解决方案

### 错误 1: 'api/stats/rtcstatscollectorcallback.h' file not found
```
fatal error: 'api/stats/rtcstatscollectorcallback.h' file not found
```
**解决**: 使用 `#include "api/stats/rtc_stats_collector_callback.h"`

---

### 错误 2: no member named 'timestamp_us'
```
error: no member named 'timestamp_us' in 'webrtc::RTCStatsReport'
```
**解决**: 使用 `report->timestamp().us()`

---

### 错误 2: no member named 'timestamp_us'
```
error: no member named 'timestamp_us' in 'webrtc::RTCStatsReport'
```
**解决**: 使用 `report->timestamp().us()`

---

### 错误 3: no type named 'RTCInboundRTPStreamStats'
```
error: no type named 'RTCInboundRTPStreamStats' in namespace 'webrtc'
```
**解决**: 使用 `RTCInboundRtpStreamStats`（注意 Rtp 的大小写）

---

### 错误 3: no type named 'RTCInboundRTPStreamStats'
```
error: no type named 'RTCInboundRTPStreamStats' in namespace 'webrtc'
```
**解决**: 使用 `RTCInboundRtpStreamStats`（注意 Rtp 的大小写）

---

### 错误 4: no member named 'ValueOrDefault'
```
error: no member named 'ValueOrDefault' in 'std::optional<double>'
```
**解决**: 使用标准的 `.value_or()` 方法

---

### 错误 4: no member named 'ValueOrDefault'
```
error: no member named 'ValueOrDefault' in 'std::optional<double>'
```
**解决**: 使用标准的 `.value_or()` 方法

---

### 错误 5: no member named 'selected'
```
error: no member named 'selected' in 'webrtc::RTCIceCandidatePairStats'
```
**解决**: 使用 `nominated` 和 `state` 字段组合判断

---

## 参考资料

- [WebRTC Stats API](https://w3c.github.io/webrtc-stats/)
- [WebRTC Native Code API](https://webrtc.googlesource.com/src/+/main/docs/native-code/index.md)
- [RTCStats Objects Header](https://webrtc.googlesource.com/src/+/main/api/stats/rtcstats_objects.h)

---

**更新日期**: 2025年11月5日  
**WebRTC 版本**: M120+  
**作者**: AI Assistant
