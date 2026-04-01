#include "callmanager.h"

#include <chrono>
#include <sstream>
#include <utility>

namespace {

std::string GenerateCallId() {
  static std::atomic<uint64_t> counter{1};
  const uint64_t tick = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  std::ostringstream stream;
  stream << std::hex << tick << "-" << counter.fetch_add(1);
  return stream.str();
}

}  // namespace

CallManager::CallManager(std::chrono::milliseconds call_request_timeout)
    : signal_transport_(nullptr),
      observer_(nullptr),
      call_state_(CallState::Idle),
      is_caller_(false),
      call_request_timeout_(call_request_timeout) {}

CallManager::~CallManager() {
  StopCallRequestTimer();
}

void CallManager::SetSignalTransport(CallSignalingTransport* signal_transport) {
  std::lock_guard<std::mutex> lock(mutex_);
  signal_transport_ = signal_transport;
}

void CallManager::RegisterObserver(CallManagerObserver* observer) {
  std::lock_guard<std::mutex> lock(mutex_);
  observer_ = observer;
}

bool CallManager::InitiateCall(const std::string& target_client_id) {
  CallSignalingTransport* signal_transport = nullptr;
  std::string call_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!signal_transport_ || !signal_transport_->IsConnected() ||
        call_state_ != CallState::Idle) {
      return false;
    }

    current_peer_ = target_client_id;
    current_call_id_ = GenerateCallId();
    is_caller_ = true;
    signal_transport = signal_transport_;
    call_id = current_call_id_;
  }

  SetCallState(CallState::Calling);
  signal_transport->SendCallRequest(target_client_id, call_id);
  StartCallRequestTimer();
  return true;
}

void CallManager::CancelCall() {
  std::string current_peer;
  CallSignalingTransport* signal_transport = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Calling) {
      return;
    }
    current_peer = current_peer_;
    signal_transport = signal_transport_;
  }

  if (signal_transport && !current_peer.empty()) {
    signal_transport->SendCallCancel(current_peer, GetCurrentCallId(),
                                     "cancelled");
  }
  CleanupCall();
}

void CallManager::AcceptCall() {
  std::string current_peer;
  std::string current_call_id;
  CallManagerObserver* observer = nullptr;
  CallSignalingTransport* signal_transport = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Receiving) {
      return;
    }
    current_peer = current_peer_;
    current_call_id = current_call_id_;
    observer = observer_;
    signal_transport = signal_transport_;
  }

  if (!signal_transport) {
    return;
  }

  signal_transport->SendCallResponse(current_peer, current_call_id, true);
  SetCallState(CallState::Connecting);

  if (observer) {
    observer->OnNeedCreatePeerConnection(current_peer, false);
  }
}

void CallManager::RejectCall(const std::string& reason) {
  std::string current_peer;
  std::string current_call_id;
  CallSignalingTransport* signal_transport = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Receiving) {
      return;
    }
    current_peer = current_peer_;
    current_call_id = current_call_id_;
    signal_transport = signal_transport_;
  }

  if (signal_transport && !current_peer.empty()) {
    signal_transport->SendCallResponse(
        current_peer, current_call_id, false,
        reason.empty() ? "rejected" : reason);
  }
  CleanupCall();
}

void CallManager::EndCall() {
  std::string current_peer;
  std::string current_call_id;
  CallManagerObserver* observer = nullptr;
  CallSignalingTransport* signal_transport = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ == CallState::Idle || call_state_ == CallState::Ending) {
      return;
    }
    current_peer = current_peer_;
    current_call_id = current_call_id_;
    observer = observer_;
    signal_transport = signal_transport_;
    call_state_ = CallState::Ending;
  }

  if (signal_transport && !current_peer.empty()) {
    signal_transport->SendCallEnd(current_peer, current_call_id, "hangup");
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

std::string CallManager::GetCurrentCallId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_call_id_;
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

void CallManager::HandleCallRequest(const std::string& from,
                                    const std::string& call_id) {
  CallSignalingTransport* signal_transport = nullptr;
  CallManagerObserver* observer = nullptr;
  bool busy = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_id.empty()) {
      return;
    }
    if (call_state_ != CallState::Idle) {
      busy = true;
      signal_transport = signal_transport_;
    } else {
      current_peer_ = from;
      current_call_id_ = call_id;
      is_caller_ = false;
      observer = observer_;
      call_state_ = CallState::Receiving;
    }
  }

  if (busy) {
    if (signal_transport) {
      signal_transport->SendCallResponse(from, call_id, false, "busy");
    }
    return;
  }

  if (observer) {
    observer->OnCallStateChanged(CallState::Receiving, from);
    observer->OnIncomingCall(from);
  }
}

void CallManager::HandleCallResponse(const std::string& from,
                                     const std::string& call_id,
                                     bool accepted,
                                     const std::string& reason) {
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Calling || current_peer_ != from ||
        current_call_id_ != call_id || call_id.empty()) {
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
                                   const std::string& call_id,
                                   const std::string& reason) {
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (call_state_ != CallState::Receiving || current_peer_ != from ||
        current_call_id_ != call_id || call_id.empty()) {
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
                                const std::string& call_id,
                                const std::string& reason) {
  CallManagerObserver* observer = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_peer_ != from || current_call_id_ != call_id ||
        call_state_ == CallState::Idle || call_state_ == CallState::Ending ||
        call_id.empty()) {
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
    current_call_id_.clear();
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
        call_request_timeout_;
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
