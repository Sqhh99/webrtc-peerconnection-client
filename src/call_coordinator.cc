#include "call_coordinator.h"

#include <utility>

#include "api/stats/rtcstats_objects.h"
#include "rtc_base/logging.h"

CallCoordinator::CallCoordinator(const webrtc::Environment& env)
    : env_(env),
      ui_observer_(nullptr),
      is_caller_(false),
      last_ice_state_("Not connected") {
  webrtc_engine_ = std::make_unique<WebRTCEngine>(env);
  webrtc_engine_->SetObserver(this);

  signal_client_ = std::make_unique<SignalClient>();
  call_manager_ = std::make_unique<CallManager>();
  call_manager_->SetSignalClient(signal_client_.get());

  last_stats_.ice_state = last_ice_state_;
  last_stats_.valid = false;
  last_stats_.local_candidate_summary = "-";
  last_stats_.remote_candidate_summary = "-";
}

CallCoordinator::~CallCoordinator() {
  Shutdown();
}

void CallCoordinator::SetUIObserver(ICallUIObserver* ui_observer) {
  ui_observer_ = ui_observer;
}

bool CallCoordinator::Initialize() {
  signal_client_->RegisterObserver(this);
  call_manager_->RegisterObserver(this);
  return webrtc_engine_->Initialize();
}

void CallCoordinator::Shutdown() {
  if (signal_client_) {
    signal_client_->Disconnect();
  }
  if (webrtc_engine_) {
    webrtc_engine_->Shutdown();
  }
  current_peer_id_.clear();
}

void CallCoordinator::ConnectToSignalServer(const std::string& url,
                                            const std::string& client_id) {
  if (signal_client_) {
    signal_client_->Connect(url, client_id);
  }
}

void CallCoordinator::DisconnectFromSignalServer() {
  if (signal_client_) {
    signal_client_->Disconnect();
  }
}

void CallCoordinator::StartCall(const std::string& peer_id) {
  if (call_manager_) {
    call_manager_->InitiateCall(peer_id);
  }
}

void CallCoordinator::AcceptCall() {
  if (call_manager_) {
    call_manager_->AcceptCall();
  }
}

void CallCoordinator::RejectCall(const std::string& reason) {
  if (call_manager_) {
    call_manager_->RejectCall(reason);
  }
}

void CallCoordinator::EndCall() {
  if (call_manager_) {
    call_manager_->EndCall();
  }
}

bool CallCoordinator::IsConnectedToSignalServer() const {
  return signal_client_ && signal_client_->IsConnected();
}

bool CallCoordinator::IsInCall() const {
  return call_manager_ && call_manager_->IsInCall();
}

CallState CallCoordinator::GetCallState() const {
  return call_manager_ ? call_manager_->GetCallState() : CallState::Idle;
}

std::string CallCoordinator::GetCurrentPeerId() const {
  return current_peer_id_;
}

std::string CallCoordinator::GetClientId() const {
  return signal_client_ ? signal_client_->GetClientId() : std::string();
}

RtcStatsSnapshot CallCoordinator::GetLatestRtcStats() {
  if (webrtc_engine_ && webrtc_engine_->HasPeerConnection()) {
    webrtc_engine_->CollectStats(
        [this](const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
          ExtractAndStoreRtcStats(report);
        });
  }

  std::lock_guard<std::mutex> lock(stats_mutex_);
  if (!has_stats_) {
    RtcStatsSnapshot snapshot = last_stats_;
    snapshot.valid = false;
    return snapshot;
  }
  return last_stats_;
}

void CallCoordinator::OnLocalVideoTrackAdded(webrtc::VideoTrackInterface* track) {
  if (ui_observer_) {
    ui_observer_->OnStartLocalRenderer(track);
  }
}

void CallCoordinator::OnRemoteVideoTrackAdded(
    webrtc::VideoTrackInterface* track) {
  if (ui_observer_) {
    ui_observer_->OnStartRemoteRenderer(track);
  }
}

void CallCoordinator::OnRemoteVideoTrackRemoved() {
  if (ui_observer_) {
    ui_observer_->OnStopRemoteRenderer();
  }
}

void CallCoordinator::OnIceConnectionStateChanged(
    webrtc::PeerConnectionInterface::IceConnectionState state) {
  const std::string state_text = IceStateToString(state);
  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    last_ice_state_ = state_text;
    last_stats_.ice_state = state_text;
  }

  if (state == webrtc::PeerConnectionInterface::kIceConnectionConnected ||
      state == webrtc::PeerConnectionInterface::kIceConnectionCompleted) {
    if (call_manager_) {
      call_manager_->NotifyPeerConnectionEstablished();
    }
  } else if (state == webrtc::PeerConnectionInterface::kIceConnectionFailed ||
             state == webrtc::PeerConnectionInterface::kIceConnectionDisconnected ||
             state == webrtc::PeerConnectionInterface::kIceConnectionClosed) {
    if (ui_observer_) {
      ui_observer_->OnLogMessage("ICE connection closed", "warning");
    }
  }
}

void CallCoordinator::OnOfferCreated(const std::string& sdp) {
  if (signal_client_) {
    signal_client_->SendOffer(current_peer_id_, {"offer", sdp});
  }
}

void CallCoordinator::OnAnswerCreated(const std::string& sdp) {
  if (signal_client_) {
    signal_client_->SendAnswer(current_peer_id_, {"answer", sdp});
  }
}

void CallCoordinator::OnIceCandidateGenerated(const std::string& sdp_mid,
                                              int sdp_mline_index,
                                              const std::string& candidate) {
  if (signal_client_) {
    signal_client_->SendIceCandidate(current_peer_id_,
                                     {sdp_mid, sdp_mline_index, candidate});
  }
}

void CallCoordinator::OnError(const std::string& error) {
  if (ui_observer_) {
    ui_observer_->OnShowError("WebRTC Error", error);
  }
}

void CallCoordinator::OnConnected(const std::string& client_id) {
  if (ui_observer_) {
    ui_observer_->OnSignalConnected(client_id);
    ui_observer_->OnLogMessage("Connected to signaling server as " + client_id,
                               "success");
  }
}

void CallCoordinator::OnDisconnected() {
  if (ui_observer_) {
    ui_observer_->OnSignalDisconnected();
    ui_observer_->OnLogMessage("Disconnected from signaling server", "warning");
  }
}

void CallCoordinator::OnConnectionError(const std::string& error) {
  if (ui_observer_) {
    ui_observer_->OnSignalError(error);
    ui_observer_->OnLogMessage("Signaling error: " + error, "error");
  }
}

void CallCoordinator::OnIceServersReceived(
    const std::vector<IceServerConfig>& ice_servers) {
  ice_servers_ = ice_servers;
  webrtc_engine_->SetIceServers(ice_servers);

  if (ui_observer_) {
    ui_observer_->OnLogMessage(
        "Received " + std::to_string(ice_servers.size()) +
            " ICE server configuration entries",
        "info");
  }
}

void CallCoordinator::OnClientListUpdate(const std::vector<ClientInfo>& clients) {
  if (ui_observer_) {
    ui_observer_->OnClientListUpdate(clients);
  }
}

void CallCoordinator::OnUserOffline(const std::string& client_id) {
  if (client_id == current_peer_id_ && call_manager_) {
    call_manager_->EndCall();
  }
}

void CallCoordinator::OnCallRequest(const std::string& from) {
  if (call_manager_) {
    call_manager_->HandleCallRequest(from);
  }
}

void CallCoordinator::OnCallResponse(const std::string& from,
                                     bool accepted,
                                     const std::string& reason) {
  if (call_manager_) {
    call_manager_->HandleCallResponse(from, accepted, reason);
  }
}

void CallCoordinator::OnCallCancel(const std::string& from,
                                   const std::string& reason) {
  if (call_manager_) {
    call_manager_->HandleCallCancel(from, reason);
  }
}

void CallCoordinator::OnCallEnd(const std::string& from,
                                const std::string& reason) {
  if (call_manager_) {
    call_manager_->HandleCallEnd(from, reason);
  }
}

void CallCoordinator::OnOffer(const std::string& from,
                              const SessionDescriptionPayload& sdp) {
  ProcessOffer(from, sdp);
}

void CallCoordinator::OnAnswer(const std::string& from,
                               const SessionDescriptionPayload& sdp) {
  ProcessAnswer(from, sdp);
}

void CallCoordinator::OnIceCandidate(const std::string& from,
                                     const IceCandidatePayload& candidate) {
  ProcessIceCandidate(from, candidate);
}

void CallCoordinator::OnCallStateChanged(CallState state,
                                         const std::string& peer_id) {
  if (ui_observer_) {
    ui_observer_->OnCallStateChanged(state, peer_id);
  }
}

void CallCoordinator::OnIncomingCall(const std::string& caller_id) {
  if (ui_observer_) {
    ui_observer_->OnIncomingCall(caller_id);
  }
}

void CallCoordinator::OnCallAccepted(const std::string& peer_id) {
  RTC_LOG(LS_INFO) << "Call accepted by " << peer_id;
}

void CallCoordinator::OnCallRejected(const std::string& peer_id,
                                     const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call rejected by " << peer_id << ": " << reason;
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnCallCancelled(const std::string& peer_id,
                                      const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call cancelled by " << peer_id << ": " << reason;
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnCallEnded(const std::string& peer_id,
                                  const std::string& reason) {
  RTC_LOG(LS_INFO) << "Call ended with " << peer_id << ": " << reason;
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnCallTimeout() {
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::OnNeedCreatePeerConnection(const std::string& peer_id,
                                                 bool is_caller) {
  current_peer_id_ = peer_id;
  is_caller_ = is_caller;

  if (webrtc_engine_->HasPeerConnection()) {
    return;
  }

  if (!webrtc_engine_->CreatePeerConnection()) {
    if (ui_observer_) {
      ui_observer_->OnShowError("Error", "Failed to create peer connection");
    }
    return;
  }

  webrtc_engine_->AddTracks();
  if (is_caller) {
    webrtc_engine_->CreateOffer();
  }
}

void CallCoordinator::OnNeedClosePeerConnection() {
  if (ui_observer_) {
    ui_observer_->OnStopLocalRenderer();
    ui_observer_->OnStopRemoteRenderer();
  }
  if (webrtc_engine_) {
    webrtc_engine_->ClosePeerConnection();
  }
}

void CallCoordinator::ProcessOffer(const std::string& from,
                                   const SessionDescriptionPayload& sdp) {
  if (!webrtc_engine_->HasPeerConnection()) {
    if (ui_observer_) {
      ui_observer_->OnLogMessage(
          "Received offer before peer connection was created", "error");
    }
    return;
  }

  current_peer_id_ = from;
  is_caller_ = false;

  if (sdp.sdp.empty()) {
    if (ui_observer_) {
      ui_observer_->OnLogMessage("Offer payload did not contain SDP text",
                                 "error");
    }
    return;
  }

  webrtc_engine_->SetRemoteOffer(sdp.sdp);
  webrtc_engine_->CreateAnswer();
}

void CallCoordinator::ProcessAnswer(const std::string& from,
                                    const SessionDescriptionPayload& sdp) {
  if (from != current_peer_id_ || sdp.sdp.empty()) {
    return;
  }
  webrtc_engine_->SetRemoteAnswer(sdp.sdp);
}

void CallCoordinator::ProcessIceCandidate(const std::string& from,
                                          const IceCandidatePayload& candidate) {
  if (from != current_peer_id_) {
    return;
  }
  if (candidate.sdp_mid.empty() || candidate.sdp_mline_index < 0 ||
      candidate.candidate.empty()) {
    if (ui_observer_) {
      ui_observer_->OnLogMessage("Discarded malformed ICE candidate", "error");
    }
    return;
  }

  webrtc_engine_->AddIceCandidate(candidate.sdp_mid,
                                  candidate.sdp_mline_index,
                                  candidate.candidate);
}

void CallCoordinator::ExtractAndStoreRtcStats(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
  if (!report) {
    return;
  }

  RtcStatsSnapshot snapshot;
  snapshot.valid = true;
  snapshot.timestamp_ms =
      static_cast<uint64_t>(report->timestamp().us() / 1000);
  snapshot.ice_state = last_ice_state_;
  snapshot.local_candidate_summary = "-";
  snapshot.remote_candidate_summary = "-";

  uint64_t inbound_bytes = 0;
  uint64_t outbound_bytes = 0;

  const webrtc::RTCInboundRtpStreamStats* audio_inbound = nullptr;
  const webrtc::RTCInboundRtpStreamStats* video_inbound = nullptr;

  const auto inbound_stats =
      report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>();
  for (const auto* stat : inbound_stats) {
    inbound_bytes += stat->bytes_received.value_or(0u);
    const std::string kind = stat->kind.value_or("");
    if (!audio_inbound && kind == "audio") {
      audio_inbound = stat;
    } else if (!video_inbound && kind == "video") {
      video_inbound = stat;
    }
  }

  const auto outbound_stats =
      report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>();
  for (const auto* stat : outbound_stats) {
    outbound_bytes += stat->bytes_sent.value_or(0u);
  }

  const auto candidate_pairs =
      report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>();
  const webrtc::RTCIceCandidatePairStats* selected_pair = nullptr;
  for (const auto* pair : candidate_pairs) {
    if (pair->nominated.value_or(false) && pair->state.has_value() &&
        *pair->state == "succeeded") {
      selected_pair = pair;
      break;
    }
  }

  if (selected_pair) {
    snapshot.current_rtt_ms =
        selected_pair->current_round_trip_time.value_or(0.0) * 1000.0;
    const double outgoing_bps =
        selected_pair->available_outgoing_bitrate.value_or(0.0);
    const double incoming_bps =
        selected_pair->available_incoming_bitrate.value_or(0.0);
    if (outgoing_bps > 0.0) {
      snapshot.outbound_bitrate_kbps = outgoing_bps / 1000.0;
    }
    if (incoming_bps > 0.0) {
      snapshot.inbound_bitrate_kbps = incoming_bps / 1000.0;
    }
  }

  if (audio_inbound) {
    snapshot.inbound_audio_jitter_ms =
        audio_inbound->jitter.value_or(0.0) * 1000.0;
    const double packets_lost =
        static_cast<double>(audio_inbound->packets_lost.value_or(0));
    const double packets_received =
        static_cast<double>(audio_inbound->packets_received.value_or(0u));
    const double total_packets = packets_lost + packets_received;
    if (total_packets > 0.0) {
      snapshot.inbound_audio_packet_loss_percent =
          (packets_lost / total_packets) * 100.0;
    }
  }

  if (video_inbound) {
    const double packets_lost =
        static_cast<double>(video_inbound->packets_lost.value_or(0));
    const double packets_received =
        static_cast<double>(video_inbound->packets_received.value_or(0u));
    const double total_packets = packets_lost + packets_received;
    if (total_packets > 0.0) {
      snapshot.inbound_video_packet_loss_percent =
          (packets_lost / total_packets) * 100.0;
    }
    snapshot.inbound_video_fps = video_inbound->frames_per_second.value_or(0.0);
    snapshot.inbound_video_width = video_inbound->frame_width.value_or(0);
    snapshot.inbound_video_height = video_inbound->frame_height.value_or(0);
  }

  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (last_rate_sample_.valid) {
      const uint64_t delta_ms =
          snapshot.timestamp_ms - last_rate_sample_.timestamp_ms;
      if (delta_ms > 0) {
        const double inbound_delta =
            static_cast<double>(inbound_bytes - last_rate_sample_.inbound_bytes);
        const double outbound_delta = static_cast<double>(
            outbound_bytes - last_rate_sample_.outbound_bytes);
        if (snapshot.inbound_bitrate_kbps <= 0.0 && inbound_delta >= 0.0) {
          snapshot.inbound_bitrate_kbps = (inbound_delta * 8.0) / delta_ms;
        }
        if (snapshot.outbound_bitrate_kbps <= 0.0 && outbound_delta >= 0.0) {
          snapshot.outbound_bitrate_kbps = (outbound_delta * 8.0) / delta_ms;
        }
      }
    }

    last_rate_sample_.inbound_bytes = inbound_bytes;
    last_rate_sample_.outbound_bytes = outbound_bytes;
    last_rate_sample_.timestamp_ms = snapshot.timestamp_ms;
    last_rate_sample_.valid = true;

    last_stats_ = snapshot;
    has_stats_ = true;
  }
}

std::string CallCoordinator::IceStateToString(
    webrtc::PeerConnectionInterface::IceConnectionState state) const {
  switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      return "New";
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      return "Checking";
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
      return "Connected";
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      return "Completed";
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      return "Failed";
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      return "Disconnected";
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      return "Closed";
    default:
      return "Unknown";
  }
}
