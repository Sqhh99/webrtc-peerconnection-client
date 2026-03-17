#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "api/environment/environment_factory.h"
#include "call_coordinator.h"
#include "tests/support/call_coordinator_test_support.h"

namespace {

std::unique_ptr<CallCoordinator> MakeCoordinatorForTest(
    FakeWebRTCEnginePort** engine_out,
    FakeSignalClientPort** signal_out,
    FakeCallManagerPort** manager_out,
    FakeIceDisconnectWatchdogPort** watchdog_out,
    FakeCallUIObserver* ui_observer) {
  webrtc::Environment env = webrtc::CreateEnvironment();

  auto engine = std::make_unique<FakeWebRTCEnginePort>();
  auto signal = std::make_unique<FakeSignalClientPort>();
  auto manager = std::make_unique<FakeCallManagerPort>();
  auto watchdog = std::make_unique<FakeIceDisconnectWatchdogPort>();

  *engine_out = engine.get();
  *signal_out = signal.get();
  *manager_out = manager.get();
  *watchdog_out = watchdog.get();

  auto coordinator = std::make_unique<CallCoordinator>(
      env, std::move(engine), std::move(signal), std::move(manager),
      std::move(watchdog));
  coordinator->SetUIObserver(ui_observer);
  return coordinator;
}

TEST(CallCoordinatorTest, UserOfflineDoesNotEndConnectedCall) {
  FakeWebRTCEnginePort* engine = nullptr;
  FakeSignalClientPort* signal = nullptr;
  FakeCallManagerPort* manager = nullptr;
  FakeIceDisconnectWatchdogPort* watchdog = nullptr;
  FakeCallUIObserver ui;
  auto coordinator =
      MakeCoordinatorForTest(&engine, &signal, &manager, &watchdog, &ui);

  ASSERT_TRUE(coordinator->Initialize());
  CallManagerObserver* call_observer = coordinator.get();
  call_observer->OnNeedCreatePeerConnection("peer-a", false);
  manager->call_state = CallState::Connected;

  SignalClientObserver* signal_observer = coordinator.get();
  signal_observer->OnUserOffline("peer-a");

  EXPECT_EQ(manager->end_call_count, 0);
  ASSERT_FALSE(ui.logs.empty());
  EXPECT_NE(ui.logs.back().second.find("media session is kept alive"),
            std::string::npos);
}

TEST(CallCoordinatorTest, UserOfflineEndsNonConnectedCall) {
  FakeWebRTCEnginePort* engine = nullptr;
  FakeSignalClientPort* signal = nullptr;
  FakeCallManagerPort* manager = nullptr;
  FakeIceDisconnectWatchdogPort* watchdog = nullptr;
  FakeCallUIObserver ui;
  auto coordinator =
      MakeCoordinatorForTest(&engine, &signal, &manager, &watchdog, &ui);

  ASSERT_TRUE(coordinator->Initialize());
  CallManagerObserver* call_observer = coordinator.get();
  call_observer->OnNeedCreatePeerConnection("peer-b", true);
  manager->call_state = CallState::Connecting;

  SignalClientObserver* signal_observer = coordinator.get();
  signal_observer->OnUserOffline("peer-b");

  EXPECT_EQ(manager->end_call_count, 1);
}

TEST(CallCoordinatorTest, IceDisconnectWatchdogTimeoutEndsConnectedCall) {
  FakeWebRTCEnginePort* engine = nullptr;
  FakeSignalClientPort* signal = nullptr;
  FakeCallManagerPort* manager = nullptr;
  FakeIceDisconnectWatchdogPort* watchdog = nullptr;
  FakeCallUIObserver ui;
  auto coordinator =
      MakeCoordinatorForTest(&engine, &signal, &manager, &watchdog, &ui);

  ASSERT_TRUE(coordinator->Initialize());
  manager->call_state = CallState::Connected;

  WebRTCEngineObserver* engine_observer = coordinator.get();
  engine_observer->OnIceConnectionStateChanged(
      webrtc::PeerConnectionInterface::kIceConnectionDisconnected);
  EXPECT_EQ(watchdog->arm_count, 1);
  EXPECT_TRUE(watchdog->armed);

  watchdog->Fire();

  EXPECT_EQ(manager->end_call_count, 1);
}

TEST(CallCoordinatorTest, IceReconnectDisarmsWatchdogBeforeTimeout) {
  FakeWebRTCEnginePort* engine = nullptr;
  FakeSignalClientPort* signal = nullptr;
  FakeCallManagerPort* manager = nullptr;
  FakeIceDisconnectWatchdogPort* watchdog = nullptr;
  FakeCallUIObserver ui;
  auto coordinator =
      MakeCoordinatorForTest(&engine, &signal, &manager, &watchdog, &ui);

  ASSERT_TRUE(coordinator->Initialize());
  manager->call_state = CallState::Connected;

  WebRTCEngineObserver* engine_observer = coordinator.get();
  engine_observer->OnIceConnectionStateChanged(
      webrtc::PeerConnectionInterface::kIceConnectionDisconnected);
  EXPECT_EQ(watchdog->arm_count, 1);

  engine_observer->OnIceConnectionStateChanged(
      webrtc::PeerConnectionInterface::kIceConnectionConnected);
  EXPECT_FALSE(watchdog->armed);
  EXPECT_GE(watchdog->disarm_count, 1);

  watchdog->Fire();
  EXPECT_EQ(manager->end_call_count, 0);
}

TEST(CallCoordinatorTest, ShutdownDropsQueuedControlTasks) {
  FakeWebRTCEnginePort* engine = nullptr;
  FakeSignalClientPort* signal = nullptr;
  FakeCallManagerPort* manager = nullptr;
  FakeIceDisconnectWatchdogPort* watchdog = nullptr;
  FakeCallUIObserver ui;
  auto coordinator =
      MakeCoordinatorForTest(&engine, &signal, &manager, &watchdog, &ui);

  ASSERT_TRUE(coordinator->Initialize());
  signal->enqueue_invoke_tasks = true;
  manager->call_state = CallState::Connected;

  WebRTCEngineObserver* engine_observer = coordinator.get();
  engine_observer->OnIceConnectionStateChanged(
      webrtc::PeerConnectionInterface::kIceConnectionDisconnected);
  EXPECT_EQ(watchdog->arm_count, 0);

  coordinator->Shutdown();
  signal->RunPendingTasks();

  EXPECT_EQ(watchdog->arm_count, 0);
  EXPECT_EQ(manager->end_call_count, 0);
}

}  // namespace
