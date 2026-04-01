#ifndef TESTS_SUPPORT_CALLMANAGER_TEST_SUPPORT_H_
#define TESTS_SUPPORT_CALLMANAGER_TEST_SUPPORT_H_

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "callmanager.h"
#include "signaling_codec.h"

struct OutgoingCallSignal {
  SignalingMessageType type = SignalingMessageType::Unknown;
  std::string from;
  std::string to;
  std::string call_id;
  bool accepted = false;
  std::string reason;
  std::string raw_message;
};

class FakeCallSignalingTransport : public CallSignalingTransport {
 public:
  explicit FakeCallSignalingTransport(std::string self_id)
      : self_id_(std::move(self_id)) {}

  bool IsConnected() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
  }

  void SetConnected(bool connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = connected;
  }

  void SetSendHook(std::function<void(const OutgoingCallSignal&)> hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    send_hook_ = std::move(hook);
  }

  std::vector<OutgoingCallSignal> SentSignals() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sent_signals_;
  }

  void SendCallRequest(const std::string& to,
                       const std::string& call_id) override {
    OutgoingCallSignal signal;
    signal.type = SignalingMessageType::CallRequest;
    signal.from = self_id_;
    signal.to = to;
    signal.call_id = call_id;
    signal.raw_message = BuildCallRequestMessage(self_id_, to, call_id);
    Record(std::move(signal));
  }

  void SendCallResponse(const std::string& to,
                        const std::string& call_id,
                        bool accepted,
                        const std::string& reason) override {
    OutgoingCallSignal signal;
    signal.type = SignalingMessageType::CallResponse;
    signal.from = self_id_;
    signal.to = to;
    signal.call_id = call_id;
    signal.accepted = accepted;
    signal.reason = reason;
    signal.raw_message =
        BuildCallResponseMessage(self_id_, to, call_id, accepted, reason);
    Record(std::move(signal));
  }

  void SendCallCancel(const std::string& to,
                      const std::string& call_id,
                      const std::string& reason) override {
    OutgoingCallSignal signal;
    signal.type = SignalingMessageType::CallCancel;
    signal.from = self_id_;
    signal.to = to;
    signal.call_id = call_id;
    signal.reason = reason;
    signal.raw_message = BuildCallCancelMessage(self_id_, to, call_id, reason);
    Record(std::move(signal));
  }

  void SendCallEnd(const std::string& to,
                   const std::string& call_id,
                   const std::string& reason) override {
    OutgoingCallSignal signal;
    signal.type = SignalingMessageType::CallEnd;
    signal.from = self_id_;
    signal.to = to;
    signal.call_id = call_id;
    signal.reason = reason;
    signal.raw_message = BuildCallEndMessage(self_id_, to, call_id, reason);
    Record(std::move(signal));
  }

 private:
  void Record(OutgoingCallSignal signal) {
    std::function<void(const OutgoingCallSignal&)> hook;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      sent_signals_.push_back(signal);
      hook = send_hook_;
    }
    if (hook) {
      hook(signal);
    }
  }

  const std::string self_id_;
  mutable std::mutex mutex_;
  bool connected_ = true;
  std::vector<OutgoingCallSignal> sent_signals_;
  std::function<void(const OutgoingCallSignal&)> send_hook_;
};

class TestCallManagerObserver : public CallManagerObserver {
 public:
  void OnCallStateChanged(CallState state, const std::string& peer_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    state_changes.push_back({state, peer_id});
    cv.notify_all();
  }

  void OnIncomingCall(const std::string& caller_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    incoming_callers.push_back(caller_id);
    cv.notify_all();
  }

  void OnCallAccepted(const std::string& peer_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    accepted_peers.push_back(peer_id);
    cv.notify_all();
  }

  void OnCallRejected(const std::string& peer_id,
                      const std::string& reason) override {
    std::lock_guard<std::mutex> lock(mutex_);
    rejected.push_back({peer_id, reason});
    cv.notify_all();
  }

  void OnCallCancelled(const std::string& peer_id,
                       const std::string& reason) override {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled.push_back({peer_id, reason});
    cv.notify_all();
  }

  void OnCallEnded(const std::string& peer_id, const std::string& reason) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ended.push_back({peer_id, reason});
    cv.notify_all();
  }

  void OnCallTimeout() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++timeout_count;
    cv.notify_all();
  }

  void OnNeedCreatePeerConnection(const std::string& peer_id,
                                  bool is_caller) override {
    std::lock_guard<std::mutex> lock(mutex_);
    create_peer_connection.push_back({peer_id, is_caller});
    cv.notify_all();
  }

  void OnNeedClosePeerConnection() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++need_close_peer_connection_count;
    cv.notify_all();
  }

  bool WaitFor(std::function<bool(const TestCallManagerObserver&)> predicate,
               std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv.wait_for(lock, timeout, [&]() { return predicate(*this); });
  }

  mutable std::mutex mutex_;
  mutable std::condition_variable cv;

  std::vector<std::pair<CallState, std::string>> state_changes;
  std::vector<std::string> incoming_callers;
  std::vector<std::string> accepted_peers;
  std::vector<std::pair<std::string, std::string>> rejected;
  std::vector<std::pair<std::string, std::string>> cancelled;
  std::vector<std::pair<std::string, std::string>> ended;
  std::vector<std::pair<std::string, bool>> create_peer_connection;
  int timeout_count = 0;
  int need_close_peer_connection_count = 0;
};

#endif  // TESTS_SUPPORT_CALLMANAGER_TEST_SUPPORT_H_
