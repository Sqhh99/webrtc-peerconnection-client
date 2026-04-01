#ifndef ICE_DISCONNECT_WATCHDOG_H_GUARD
#define ICE_DISCONNECT_WATCHDOG_H_GUARD

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

class IceDisconnectWatchdog {
 public:
  explicit IceDisconnectWatchdog(std::chrono::milliseconds timeout);
  ~IceDisconnectWatchdog();

  IceDisconnectWatchdog(const IceDisconnectWatchdog&) = delete;
  IceDisconnectWatchdog& operator=(const IceDisconnectWatchdog&) = delete;

  void Arm(std::function<void()> on_timeout);
  void Disarm();

 private:
  void DisarmLocked();

  const std::chrono::milliseconds timeout_;
  std::mutex mutex_;
  std::jthread watchdog_thread_;
  std::atomic<uint64_t> generation_{0};
};

#endif  // ICE_DISCONNECT_WATCHDOG_H_GUARD
