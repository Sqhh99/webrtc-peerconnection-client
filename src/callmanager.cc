#include "callmanager.h"

#include <chrono>
#include <utility>

CallManager::CallManager()
    : signal_client_(nullptr),
      observer_(nullptr),
      call_state_(CallState::Idle),
      is_caller_(false) {}

CallManager::~CallManager() {
  StopCallRequestTimer();
}

void CallManager::SetSignalClient(SignalClient* signal_client) {
  std::lock_guard<std::mutex> lock(mutex_);
  signal_client_ = signal_client;
}

void CallManager::RegisterObserver(CallManagerObserver* observer) {
  std::lock_guard<std::mutex> lock(mutex_);
  observer_ = observer;
}

bool CallManager::InitiateCall(const std::string& target_client_id) {
  SignalClient* signal_client = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!signal_client_ || !signal_client_->IsConnected() ||
        call_state_ != CallState::Idle) {
      return false;
    }

    current_peer_ = target_client_id;
    is_caller_ = true;
    signal_client = signal_client_;
  }

  SetCallState(CallState::Calling);
  signal_client->SendCallRequest(target_client_id);
  StartCallRequestTimer();
  return true;
}

void CallManager::CancelCall() {
  std::string current_peer;
  SignalClient* signal_client = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Calling) {
      return;
    }
    current_peer = current_peer_;
    signal_client = signal_client_;
  }

  if (signal_client && !current_peer.empty()) {
    signal_client->SendCallCancel(current_peer, "cancelled");
  }
  CleanupCall();
}

void CallManager::AcceptCall() {
  std::string current_peer;
  CallManagerObserver* observer = nullptr;
  SignalClient* signal_client = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Receiving) {
      return;
    }
    current_peer = current_peer_;
    observer = observer_;
    signal_client = signal_client_;
  }

  if (!signal_client) {
    return;
  }

  signal_client->SendCallResponse(current_peer, true);
  SetCallState(CallState::Connecting);

  if (observer) {
    observer->OnNeedCreatePeerConnection(current_peer, false);
  }
}

void CallManager::RejectCall(const std::string& reason) {
  std::string current_peer;
  SignalClient* signal_client = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Receiving) {
      return;
    }
    current_peer = current_peer_;
    signal_client = signal_client_;
  }

  if (signal_client && !current_peer.empty()) {
    signal_client->SendCallResponse(
        current_peer, false, reason.empty() ? "rejected" : reason);
  }
  CleanupCall();
}

void CallManager::EndCall() {
  std::string current_peer;
  CallManagerObserver* observer = nullptr;
  SignalClient* signal_client = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ == CallState::Idle) {
      return;
    }
    current_peer = current_peer_;
    observer = observer_;
    signal_client = signal_client_;
    call_state_ = CallState::Ending;
  }

  if (signal_client && !current_peer.empty()) {
    signal_client->SendCallEnd(current_peer, "hangup");
  }
  if (observer) {
    observer->OnNeedClosePeerConnection();
    observer->OnCallEnded(current_peer, "hangup");
  }
  CleanupCall();
}

CallState CallManager::GetCallState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return call_state_;
}

std::string CallManager::GetCurrentPeer() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_peer_;
}

bool CallManager::IsInCall() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return call_state_ != CallState::Idle;
}

void CallManager::NotifyPeerConnectionEstablished() {
  std::string current_peer;
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Connecting) {
      return;
    }
    call_state_ = CallState::Connected;
    current_peer = current_peer_;
    observer = observer_;
  }

  if (observer) {
    observer->OnCallStateChanged(CallState::Connected, current_peer);
    observer->OnCallAccepted(current_peer);
  }
}

void CallManager::HandleCallRequest(const std::string& from) {
  SignalClient* signal_client = nullptr;
  CallManagerObserver* observer = nullptr;
  bool busy = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Idle) {
      busy = true;
      signal_client = signal_client_;
    } else {
      current_peer_ = from;
      is_caller_ = false;
      observer = observer_;
      call_state_ = CallState::Receiving;
    }
  }

  if (busy) {
    if (signal_client) {
      signal_client->SendCallResponse(from, false, "busy");
    }
    return;
  }

  if (observer) {
    observer->OnCallStateChanged(CallState::Receiving, from);
    observer->OnIncomingCall(from);
  }
}

void CallManager::HandleCallResponse(const std::string& from,
                                     bool accepted,
                                     const std::string& reason) {
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Calling || current_peer_ != from) {
      return;
    }
    StopCallRequestTimerLocked();
    observer = observer_;
    if (accepted) {
      call_state_ = CallState::Connecting;
    }
  }

  if (!observer) {
    return;
  }

  observer->OnCallStateChanged(accepted ? CallState::Connecting : CallState::Idle,
                               from);
  if (accepted) {
    observer->OnCallAccepted(from);
    observer->OnNeedCreatePeerConnection(from, true);
  } else {
    observer->OnCallRejected(from, reason);
    CleanupCall();
  }
}

void CallManager::HandleCallCancel(const std::string& from,
                                   const std::string& reason) {
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Receiving || current_peer_ != from) {
      return;
    }
    observer = observer_;
  }

  if (observer) {
    observer->OnCallCancelled(from, reason);
  }
  CleanupCall();
}

void CallManager::HandleCallEnd(const std::string& from,
                                const std::string& reason) {
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_peer_ != from || call_state_ == CallState::Idle) {
      return;
    }
    observer = observer_;
  }

  if (observer) {
    observer->OnCallEnded(from, reason);
    observer->OnNeedClosePeerConnection();
  }
  CleanupCall();
}

void CallManager::OnCallRequestTimeout() {
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Calling) {
      return;
    }
    observer = observer_;
  }

  if (observer) {
    observer->OnCallTimeout();
  }
  CleanupCall();
}

void CallManager::SetCallState(CallState state) {
  CallManagerObserver* observer = nullptr;
  std::string current_peer;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ == state) {
      return;
    }
    call_state_ = state;
    observer = observer_;
    current_peer = current_peer_;
  }

  if (observer) {
    observer->OnCallStateChanged(state, current_peer);
  }
}

void CallManager::CleanupCall() {
  CallManagerObserver* observer = nullptr;
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    StopCallRequestTimerLocked();
    notify = call_state_ != CallState::Idle;
    call_state_ = CallState::Idle;
    current_peer_.clear();
    is_caller_ = false;
    observer = observer_;
  }

  if (notify && observer) {
    observer->OnCallStateChanged(CallState::Idle, "");
  }
}

void CallManager::StartCallRequestTimer() {
  if (call_request_timer_thread_.joinable()) {
    call_request_timer_thread_.request_stop();
    call_request_timer_thread_.join();
  }

  const uint64_t generation = ++timer_generation_;
  call_request_timer_thread_ = std::jthread([this, generation](std::stop_token stop_token) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kCallRequestTimeoutMs);
    while (!stop_token.stop_requested() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (stop_token.stop_requested() || timer_generation_.load() != generation) {
      return;
    }
    OnCallRequestTimeout();
  });
}

void CallManager::StopCallRequestTimer() {
  ++timer_generation_;
  if (call_request_timer_thread_.joinable()) {
    call_request_timer_thread_.request_stop();
    call_request_timer_thread_.join();
  }
}

void CallManager::StopCallRequestTimerLocked() {
  ++timer_generation_;
}
