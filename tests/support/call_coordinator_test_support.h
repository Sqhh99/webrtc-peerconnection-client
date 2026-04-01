#ifndef TESTS_SUPPORT_CALL_COORDINATOR_TEST_SUPPORT_H_
#define TESTS_SUPPORT_CALL_COORDINATOR_TEST_SUPPORT_H_

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "call_coordinator.h"

class FakeWebRTCEnginePort : public IWebRTCEnginePort {
 public:
  void SetObserver(WebRTCEngineObserver* observer) override { observer_ = observer; }
  void SetIceServers(const std::vector<IceServerConfig>& ice_servers) override {
    ice_servers_ = ice_servers;
  }
  bool Initialize() override {
    ++initialize_count;
    return initialize_result;
  }
  bool CreatePeerConnection() override {
    ++create_peer_connection_count;
    if (create_peer_connection_result) {
      has_peer_connection = true;
    }
    return create_peer_connection_result;
  }
  void ClosePeerConnection() override {
    ++close_peer_connection_count;
    has_peer_connection = false;
  }
  bool AddTracks() override {
    ++add_tracks_count;
    return add_tracks_result;
  }
  void CreateOffer() override { ++create_offer_count; }
  void CreateAnswer() override { ++create_answer_count; }
  void SetRemoteOffer(const std::string& sdp) override {
    ++set_remote_offer_count;
    last_remote_offer = sdp;
  }
  void SetRemoteAnswer(const std::string& sdp) override {
    ++set_remote_answer_count;
    last_remote_answer = sdp;
  }
  void AddIceCandidate(const std::string& sdp_mid,
                       int sdp_mline_index,
                       const std::string& candidate) override {
    ++add_ice_candidate_count;
    last_sdp_mid = sdp_mid;
    last_sdp_mline_index = sdp_mline_index;
    last_candidate = candidate;
  }
  bool HasPeerConnection() const override { return has_peer_connection; }
  void CollectStats(
      std::function<void(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)>
          callback) override {
    ++collect_stats_count;
    if (callback) {
      callback(nullptr);
    }
  }
  AudioTransportState GetAudioTransportState() const override {
    return audio_state;
  }
  bool SetLocalVideoSource(const LocalVideoSourceConfig& config,
                           std::string* error_message) override {
    last_video_source_config = config;
    if (!set_local_video_source_result && error_message) {
      *error_message = "fake-set-local-video-source-failure";
    }
    return set_local_video_source_result;
  }
  LocalVideoSourceState GetLocalVideoSourceState() const override {
    return local_video_source_state;
  }
  void Shutdown() override { ++shutdown_count; }

  WebRTCEngineObserver* observer_ = nullptr;
  bool initialize_result = true;
  bool create_peer_connection_result = true;
  bool add_tracks_result = true;
  bool set_local_video_source_result = true;
  bool has_peer_connection = false;
  AudioTransportState audio_state;
  LocalVideoSourceConfig last_video_source_config;
  LocalVideoSourceState local_video_source_state;
  std::vector<IceServerConfig> ice_servers_;
  std::string last_remote_offer;
  std::string last_remote_answer;
  std::string last_sdp_mid;
  int last_sdp_mline_index = -1;
  std::string last_candidate;
  int initialize_count = 0;
  int create_peer_connection_count = 0;
  int close_peer_connection_count = 0;
  int add_tracks_count = 0;
  int create_offer_count = 0;
  int create_answer_count = 0;
  int set_remote_offer_count = 0;
  int set_remote_answer_count = 0;
  int add_ice_candidate_count = 0;
  int collect_stats_count = 0;
  int shutdown_count = 0;
};

class FakeSignalClientPort : public ISignalClientPort {
 public:
  bool IsConnected() const override { return is_connected; }
  void Connect(const std::string& server_url,
               const std::string& client_id) override {
    ++connect_count;
    last_server_url = server_url;
    client_id_ = client_id;
    is_connected = true;
  }
  void Disconnect() override {
    ++disconnect_count;
    is_connected = false;
  }
  std::string GetClientId() const override { return client_id_; }
  std::vector<IceServerConfig> GetIceServers() const override {
    return ice_servers;
  }
  void RegisterObserver(SignalClientObserver* observer) override {
    observer_ = observer;
  }
  void SendCallRequest(const std::string& to,
                       const std::string& call_id) override {
    last_call_request_to = to;
    last_call_request_id = call_id;
  }
  void SendCallResponse(const std::string& to,
                        const std::string& call_id,
                        bool accepted,
                        const std::string& reason) override {
    last_call_response_to = to;
    last_call_response_id = call_id;
    last_call_response_accepted = accepted;
    last_call_response_reason = reason;
  }
  void SendCallCancel(const std::string& to,
                      const std::string& call_id,
                      const std::string& reason) override {
    last_call_cancel_to = to;
    last_call_cancel_id = call_id;
    last_call_cancel_reason = reason;
  }
  void SendCallEnd(const std::string& to,
                   const std::string& call_id,
                   const std::string& reason) override {
    last_call_end_to = to;
    last_call_end_id = call_id;
    last_call_end_reason = reason;
  }
  void SendOffer(const std::string& to,
                 const SessionDescriptionPayload& sdp) override {
    ++send_offer_count;
    last_offer_to = to;
    last_offer_payload = sdp;
  }
  void SendAnswer(const std::string& to,
                  const SessionDescriptionPayload& sdp) override {
    ++send_answer_count;
    last_answer_to = to;
    last_answer_payload = sdp;
  }
  void SendIceCandidate(const std::string& to,
                        const IceCandidatePayload& candidate) override {
    ++send_ice_candidate_count;
    last_candidate_to = to;
    last_ice_candidate_payload = candidate;
  }
  void RequestClientList() override { ++request_client_list_count; }
  bool InvokeOnIoThread(std::function<void()> task) override {
    if (!task) {
      return false;
    }
    if (!enqueue_invoke_tasks) {
      task();
      return true;
    }
    {
      std::lock_guard<std::mutex> lock(task_mutex_);
      pending_tasks_.push_back(std::move(task));
    }
    return true;
  }

  void RunPendingTasks() {
    std::deque<std::function<void()>> tasks;
    {
      std::lock_guard<std::mutex> lock(task_mutex_);
      tasks.swap(pending_tasks_);
    }
    while (!tasks.empty()) {
      std::function<void()> task = std::move(tasks.front());
      tasks.pop_front();
      task();
    }
  }

  SignalClientObserver* observer_ = nullptr;
  bool is_connected = true;
  bool enqueue_invoke_tasks = false;
  std::string client_id_ = "local";
  std::vector<IceServerConfig> ice_servers;
  std::string last_server_url;
  int connect_count = 0;
  int disconnect_count = 0;
  int request_client_list_count = 0;
  int send_offer_count = 0;
  int send_answer_count = 0;
  int send_ice_candidate_count = 0;
  std::string last_offer_to;
  std::string last_answer_to;
  std::string last_candidate_to;
  SessionDescriptionPayload last_offer_payload;
  SessionDescriptionPayload last_answer_payload;
  IceCandidatePayload last_ice_candidate_payload;

  std::string last_call_request_to;
  std::string last_call_request_id;
  std::string last_call_response_to;
  std::string last_call_response_id;
  bool last_call_response_accepted = false;
  std::string last_call_response_reason;
  std::string last_call_cancel_to;
  std::string last_call_cancel_id;
  std::string last_call_cancel_reason;
  std::string last_call_end_to;
  std::string last_call_end_id;
  std::string last_call_end_reason;

 private:
  mutable std::mutex task_mutex_;
  std::deque<std::function<void()>> pending_tasks_;
};

class FakeCallManagerPort : public ICallManagerPort {
 public:
  void SetSignalTransport(CallSignalingTransport* signal_transport) override {
    signal_transport_ = signal_transport;
  }
  void RegisterObserver(CallManagerObserver* observer) override {
    observer_ = observer;
  }
  bool InitiateCall(const std::string& target_client_id) override {
    ++initiate_call_count;
    last_target_peer = target_client_id;
    return initiate_call_result;
  }
  void AcceptCall() override { ++accept_call_count; }
  void RejectCall(const std::string& reason) override {
    ++reject_call_count;
    last_reject_reason = reason;
  }
  void EndCall() override {
    ++end_call_count;
    call_state = CallState::Idle;
  }
  CallState GetCallState() const override { return call_state; }
  std::string GetCurrentCallId() const override { return current_call_id; }
  bool IsInCall() const override { return call_state != CallState::Idle; }
  void NotifyPeerConnectionEstablished() override {
    ++notify_peer_connection_established_count;
  }
  void HandleCallRequest(const std::string& from,
                         const std::string& call_id) override {
    ++handle_call_request_count;
    last_from = from;
    current_call_id = call_id;
  }
  void HandleCallResponse(const std::string& from,
                          const std::string& call_id,
                          bool accepted,
                          const std::string& reason) override {
    ++handle_call_response_count;
    last_from = from;
    current_call_id = call_id;
    last_response_accepted = accepted;
    last_response_reason = reason;
  }
  void HandleCallCancel(const std::string& from,
                        const std::string& call_id,
                        const std::string& reason) override {
    ++handle_call_cancel_count;
    last_from = from;
    current_call_id = call_id;
    last_cancel_reason = reason;
  }
  void HandleCallEnd(const std::string& from,
                     const std::string& call_id,
                     const std::string& reason) override {
    ++handle_call_end_count;
    last_from = from;
    current_call_id = call_id;
    last_end_reason = reason;
  }

  CallSignalingTransport* signal_transport_ = nullptr;
  CallManagerObserver* observer_ = nullptr;
  bool initiate_call_result = true;
  CallState call_state = CallState::Idle;
  std::string current_call_id = "call-id";
  std::string last_target_peer;
  std::string last_from;
  std::string last_reject_reason;
  std::string last_response_reason;
  std::string last_cancel_reason;
  std::string last_end_reason;
  bool last_response_accepted = false;
  int initiate_call_count = 0;
  int accept_call_count = 0;
  int reject_call_count = 0;
  int end_call_count = 0;
  int notify_peer_connection_established_count = 0;
  int handle_call_request_count = 0;
  int handle_call_response_count = 0;
  int handle_call_cancel_count = 0;
  int handle_call_end_count = 0;
};

class FakeIceDisconnectWatchdogPort : public IIceDisconnectWatchdogPort {
 public:
  void Arm(std::function<void()> on_timeout) override {
    ++arm_count;
    armed = true;
    callback = std::move(on_timeout);
  }
  void Disarm() override {
    ++disarm_count;
    armed = false;
    callback = nullptr;
  }

  void Fire() {
    if (!armed || !callback) {
      return;
    }
    callback();
  }

  bool armed = false;
  int arm_count = 0;
  int disarm_count = 0;
  std::function<void()> callback;
};

class FakeCallUIObserver : public ICallUIObserver {
 public:
  void OnStartLocalRenderer(webrtc::VideoTrackInterface* /*track*/) override {
    ++start_local_renderer_count;
  }
  void OnStopLocalRenderer() override { ++stop_local_renderer_count; }
  void OnStartRemoteRenderer(webrtc::VideoTrackInterface* /*track*/) override {
    ++start_remote_renderer_count;
  }
  void OnStopRemoteRenderer() override { ++stop_remote_renderer_count; }
  void OnLogMessage(const std::string& message, const std::string& level) override {
    logs.emplace_back(level, message);
  }
  void OnShowError(const std::string& title, const std::string& message) override {
    errors.emplace_back(title, message);
  }
  void OnShowInfo(const std::string& title, const std::string& message) override {
    infos.emplace_back(title, message);
  }
  void OnSignalConnected(const std::string& client_id) override {
    connected_ids.push_back(client_id);
  }
  void OnSignalDisconnected() override { ++signal_disconnected_count; }
  void OnSignalError(const std::string& error) override {
    signal_errors.push_back(error);
  }
  void OnClientListUpdate(const std::vector<ClientInfo>& clients) override {
    latest_clients = clients;
  }
  void OnCallStateChanged(CallState state, const std::string& peer_id) override {
    call_states.emplace_back(state, peer_id);
  }
  void OnIncomingCall(const std::string& caller_id) override {
    incoming_callers.push_back(caller_id);
  }

  int start_local_renderer_count = 0;
  int stop_local_renderer_count = 0;
  int start_remote_renderer_count = 0;
  int stop_remote_renderer_count = 0;
  int signal_disconnected_count = 0;
  std::vector<std::pair<std::string, std::string>> logs;
  std::vector<std::pair<std::string, std::string>> errors;
  std::vector<std::pair<std::string, std::string>> infos;
  std::vector<std::string> connected_ids;
  std::vector<std::string> signal_errors;
  std::vector<ClientInfo> latest_clients;
  std::vector<std::pair<CallState, std::string>> call_states;
  std::vector<std::string> incoming_callers;
};

#endif  // TESTS_SUPPORT_CALL_COORDINATOR_TEST_SUPPORT_H_
