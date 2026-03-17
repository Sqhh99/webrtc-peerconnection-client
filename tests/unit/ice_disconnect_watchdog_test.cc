#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "ice_disconnect_watchdog.h"

namespace {

using namespace std::chrono_literals;

TEST(IceDisconnectWatchdogTest, ArmTriggersTimeoutCallback) {
  IceDisconnectWatchdog watchdog(120ms);
  std::atomic<int> fired{0};

  watchdog.Arm([&]() { fired.fetch_add(1); });
  std::this_thread::sleep_for(500ms);

  EXPECT_EQ(fired.load(), 1);
}

TEST(IceDisconnectWatchdogTest, DisarmPreventsTimeoutCallback) {
  IceDisconnectWatchdog watchdog(300ms);
  std::atomic<int> fired{0};

  watchdog.Arm([&]() { fired.fetch_add(1); });
  std::this_thread::sleep_for(80ms);
  watchdog.Disarm();
  std::this_thread::sleep_for(400ms);

  EXPECT_EQ(fired.load(), 0);
}

TEST(IceDisconnectWatchdogTest, ReArmingInvalidatesPreviousCallback) {
  IceDisconnectWatchdog watchdog(180ms);
  std::atomic<int> first_fired{0};
  std::atomic<int> second_fired{0};

  watchdog.Arm([&]() { first_fired.fetch_add(1); });
  std::this_thread::sleep_for(50ms);
  watchdog.Arm([&]() { second_fired.fetch_add(1); });
  std::this_thread::sleep_for(500ms);

  EXPECT_EQ(first_fired.load(), 0);
  EXPECT_EQ(second_fired.load(), 1);
}

}  // namespace
