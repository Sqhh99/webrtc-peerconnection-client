/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "defaults.h"

#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <random>
#include <string>
#include <chrono>  // 添加此头文件用于时间戳
#include <process.h>

#ifdef WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

const char kAudioLabel[] = "audio_label";
const char kVideoLabel[] = "video_label";
const char kStreamId[] = "stream_id";
const uint16_t kDefaultServerPort = 8888;

std::string GetEnvVarOrDefault(const char* env_var_name,
                               const char* default_value) {
  std::string value;
  const char* env_var = getenv(env_var_name);
  if (env_var)
    value = env_var;

  if (value.empty())
    value = default_value;

  return value;
}

std::string GetPeerConnectionString() {
  return GetEnvVarOrDefault("WEBRTC_CONNECT", "stun:stun.l.google.com:19302");
}

std::string GetDefaultServerName() {
  return GetEnvVarOrDefault("WEBRTC_SERVER", "localhost");
}

// 原始版本（保留为参考）
std::string GetPeerNameOriginal() {
  char computer_name[256];
  std::string ret(GetEnvVarOrDefault("USERNAME", "user"));
  ret += '@';
  if (gethostname(computer_name, std::size(computer_name)) == 0) {
    ret += computer_name;
  } else {
    ret += "host";
  }
  return ret;
}

// 修改后的版本（添加进程ID和时间戳以确保唯一性）
std::string GetPeerName() {
  char computer_name[256];
  std::string ret(GetEnvVarOrDefault("USERNAME", "user"));
  ret += '@';
  if (gethostname(computer_name, std::size(computer_name)) == 0) {
    ret += computer_name;
  } else {
    ret += "host";
  }
  // 添加进程ID和时间戳以确保唯一性
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
#ifdef _WIN32
  int pid = _getpid();
#else
  int pid = getpid();
#endif
  ret += "_" + std::to_string(pid) + "_" + std::to_string(timestamp);

  return ret;
}

std::string GenerateRandomUsername() {
  static constexpr char kAlphabet[] = "0123456789abcdef";

  std::random_device random_device;
  std::string name = "user-";
  name.reserve(13);
  for (int i = 0; i < 8; ++i) {
    name.push_back(kAlphabet[random_device() % 16]);
  }
  return name;
}
