#include "ice_disconnect_watchdog.h"

#include <thread>
#include <utility>

IceDisconnectWatchdog::IceDisconnectWatchdog(std::chrono::milliseconds timeout)
    : timeout_(timeout) {}

IceDisconnectWatchdog::~IceDisconnectWatchdog() {
  Disarm();
}

void IceDisconnectWatchdog::Arm(std::function<void()> on_timeout) {
  std::lock_guard<std::mutex> lock(mutex_);
  DisarmLocked();

  const uint64_t generation = ++generation_;
  watchdog_thread_ = std::jthread(
      [this, generation, on_timeout = std::move(on_timeout)](
          std::stop_token stop_token) mutable {
        const auto deadline = std::chrono::steady_clock::now() + timeout_;
        while (!stop_token.stop_requested() &&
               std::chrono::steady_clock::now() < deadline) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (stop_token.stop_requested() || generation_.load() != generation) {
          return;
        }
        if (on_timeout) {
          on_timeout();
        }
      });
}

void IceDisconnectWatchdog::Disarm() {
  std::lock_guard<std::mutex> lock(mutex_);
  DisarmLocked();
}

void IceDisconnectWatchdog::DisarmLocked() {
  ++generation_;
  if (watchdog_thread_.joinable()) {
    watchdog_thread_.request_stop();
    if (watchdog_thread_.get_id() == std::this_thread::get_id()) {
      watchdog_thread_.detach();
      return;
    }
    watchdog_thread_.join();
  }
}
