#include "webrtcengine_internal.h"

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

bool WebRTCEngine::CreatePeerConnection() {
  RTC_DCHECK(peer_connection_factory_);
  RTC_DCHECK(!peer_connection_);

  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

  if (!ice_servers_.empty()) {
    RTC_LOG(LS_INFO) << "Using " << ice_servers_.size()
                     << " ICE servers from signaling server";
    for (const auto& ice_config : ice_servers_) {
      webrtc::PeerConnectionInterface::IceServer ice_server;
      ice_server.urls = ice_config.urls;

      if (!ice_config.username.empty()) {
        ice_server.username = ice_config.username;
      }
      if (!ice_config.credential.empty()) {
        ice_server.password = ice_config.credential;
      }

      config.servers.push_back(ice_server);
      for (const auto& url : ice_server.urls) {
        RTC_LOG(LS_INFO) << "  ICE Server: " << url
                         << (ice_server.username.empty() ? "" : " (with auth)");
      }
    }
  } else {
    RTC_LOG(LS_WARNING)
        << "No ICE servers from signaling server, using default STUN";
    webrtc::PeerConnectionInterface::IceServer stun_server;
    stun_server.uri = "stun:stun.l.google.com:19302";
    config.servers.push_back(stun_server);
  }

  config.type = webrtc::PeerConnectionInterface::kAll;
  config.continual_gathering_policy =
      webrtc::PeerConnectionInterface::GATHER_CONTINUALLY;

  RenewPeerConnectionCallbackGuard();
  pc_observer_ = std::make_unique<PeerConnectionObserverImpl>(
      this, GetPeerConnectionCallbackGuard());
  webrtc::PeerConnectionDependencies pc_dependencies(pc_observer_.get());
  auto error_or_peer_connection =
      peer_connection_factory_->CreatePeerConnectionOrError(
          config, std::move(pc_dependencies));

  if (error_or_peer_connection.ok()) {
    peer_connection_ = std::move(error_or_peer_connection.value());
    RTC_LOG(LS_INFO) << "PeerConnection created successfully";
    return true;
  }

  RTC_LOG(LS_ERROR) << "CreatePeerConnection failed: "
                    << error_or_peer_connection.error().message();
  ResetPeerConnectionCallbackGuard();
  pc_observer_.reset();
  if (observer_) {
    observer_->OnError(error_or_peer_connection.error().message());
  }
  return false;
}

void WebRTCEngine::ClosePeerConnection() {
  ResetPeerConnectionCallbackGuard();
  RTC_LOG(LS_INFO) << "Closing peer connection...";

  if (video_source_) {
    video_source_->Stop();
    RTC_LOG(LS_INFO) << "Video source stopped";
  }

  if (local_video_track_) {
    local_video_track_->set_enabled(false);
    RTC_LOG(LS_INFO) << "Local video track disabled";
  }
  if (local_audio_track_) {
    local_audio_track_->set_enabled(false);
    RTC_LOG(LS_INFO) << "Local audio track disabled";
  }

  if (audio_input_switcher_) {
    RTC_LOG(LS_INFO) << "Stopping audio input before peer connection close";
    audio_input_switcher_->StopRecording();
    audio_input_switcher_->ClearSyntheticAudio();
    RTC_LOG(LS_INFO) << "Audio input stopped";
  }

  if (peer_connection_) {
    auto senders = peer_connection_->GetSenders();
    for (const auto& sender : senders) {
      peer_connection_->RemoveTrackOrError(sender);
    }
    RTC_LOG(LS_INFO) << "Removed " << senders.size()
                     << " senders from peer connection";
    peer_connection_->Close();
    peer_connection_ = nullptr;
    RTC_LOG(LS_INFO) << "Peer connection closed";
  }

  local_video_track_ = nullptr;
  local_audio_track_ = nullptr;
  remote_video_track_ = nullptr;
  video_sender_ = nullptr;
  audio_sender_ = nullptr;
  telemetry_->SetLocalAudioTrackAttached(false);
  telemetry_->SetRemoteAudioTrackAttached(false);
  RTC_LOG(LS_INFO) << "Local and remote track references released";

  RTC_LOG(LS_INFO) << "Releasing video source";
  video_source_ = nullptr;
  local_media_pipeline_->MarkInactive();
  RTC_LOG(LS_INFO) << "Video source released";

  for (auto* candidate : pending_ice_candidates_) {
    delete candidate;
  }
  pending_ice_candidates_.clear();
  pc_observer_.reset();

  RTC_LOG(LS_INFO) << "Peer connection closed successfully";
}

void WebRTCEngine::CreateOffer() {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot create offer: no peer connection";
    return;
  }

  RTC_LOG(LS_INFO) << "=== Creating Offer ===";
  is_creating_offer_ = true;
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = true;
  options.offer_to_receive_video = true;

  auto observer = CreateSessionDescriptionObserverImpl::Create(
      this, GetPeerConnectionCallbackGuard(), true);
  peer_connection_->CreateOffer(observer.get(), options);
  RTC_LOG(LS_INFO) << "CreateOffer called on peer_connection";
}

void WebRTCEngine::CreateAnswer() {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot create answer: no peer connection";
    return;
  }

  RTC_LOG(LS_INFO) << "=== Creating Answer ===";
  is_creating_offer_ = false;
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;

  auto observer = CreateSessionDescriptionObserverImpl::Create(
      this, GetPeerConnectionCallbackGuard(), false);
  peer_connection_->CreateAnswer(observer.get(), options);
  RTC_LOG(LS_INFO) << "CreateAnswer called on peer_connection";
}

void WebRTCEngine::SetRemoteOffer(const std::string& sdp) {
  SetRemoteDescription("offer", sdp);
}

void WebRTCEngine::SetRemoteAnswer(const std::string& sdp) {
  SetRemoteDescription("answer", sdp);
}

void WebRTCEngine::SetRemoteDescription(const std::string& type,
                                        const std::string& sdp) {
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "Cannot set remote description: no peer connection";
    return;
  }

  const bool should_create_answer = type == "offer";
  webrtc::SdpType sdp_type =
      (type == "offer") ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer;
  webrtc::SdpParseError error;
  auto session_desc = webrtc::CreateSessionDescription(sdp_type, sdp, &error);

  if (!session_desc) {
    RTC_LOG(LS_ERROR) << "Failed to parse SDP: " << error.description;
    if (observer_) {
      observer_->OnError("Failed to parse SDP: " + error.description);
    }
    return;
  }

  std::weak_ptr<void> callback_guard = GetPeerConnectionCallbackGuard();
  auto observer = webrtcengine_internal::SetRemoteDescriptionObserver::Create(
      [this, callback_guard, should_create_answer](webrtc::RTCError error) {
        if (callback_guard.expired()) {
          return;
        }
        if (!error.ok()) {
          RTC_LOG(LS_ERROR)
              << "SetRemoteDescription failed: " << error.message();
          if (observer_) {
            observer_->OnError(std::string("SetRemoteDescription failed: ") +
                               error.message());
          }
        } else {
          RTC_LOG(LS_INFO) << "SetRemoteDescription succeeded";
          ProcessPendingIceCandidates();
          PublishRemoteTracks(should_create_answer ? "set-remote-offer"
                                                   : "set-remote-answer");
          LogRemoteMediaState(should_create_answer ? "after-set-remote-offer"
                                                   : "after-set-remote-answer");
          if (should_create_answer) {
            if (!AddTracks()) {
              RTC_LOG(LS_ERROR)
                  << "Failed to ensure local tracks before answering";
              return;
            }
            CreateAnswer();
          }
        }
      });

  peer_connection_->SetRemoteDescription(std::move(session_desc), observer);
}

void WebRTCEngine::AddIceCandidate(const std::string& sdp_mid,
                                   int sdp_mline_index,
                                   const std::string& candidate) {
  if (!peer_connection_) {
    RTC_LOG(LS_WARNING) << "Cannot add ICE candidate: no peer connection";
    return;
  }

  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::IceCandidate> ice_candidate(
      webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate, &error));

  if (!ice_candidate) {
    RTC_LOG(LS_ERROR) << "Failed to parse ICE candidate: " << error.description;
    return;
  }

  if (!peer_connection_->remote_description()) {
    RTC_LOG(LS_INFO) << "Remote description not set yet, queueing ICE candidate";
    pending_ice_candidates_.push_back(ice_candidate.release());
    return;
  }

  if (!peer_connection_->AddIceCandidate(ice_candidate.get())) {
    RTC_LOG(LS_WARNING)
        << "Failed to add ICE candidate immediately, queueing for retry";
    pending_ice_candidates_.push_back(ice_candidate.release());
    return;
  }
}

void WebRTCEngine::ProcessPendingIceCandidates() {
  if (!peer_connection_ || !peer_connection_->remote_description()) {
    return;
  }

  std::deque<webrtc::IceCandidate*> remaining_candidates;
  for (auto* candidate : pending_ice_candidates_) {
    if (!peer_connection_->AddIceCandidate(candidate)) {
      RTC_LOG(LS_WARNING)
          << "Failed to add pending ICE candidate, keeping for retry";
      remaining_candidates.push_back(candidate);
      continue;
    }
    delete candidate;
  }
  pending_ice_candidates_.swap(remaining_candidates);
}

bool WebRTCEngine::IsConnected() const {
  if (!peer_connection_) {
    return false;
  }

  const auto state = peer_connection_->ice_connection_state();
  return state == webrtc::PeerConnectionInterface::kIceConnectionConnected ||
         state == webrtc::PeerConnectionInterface::kIceConnectionCompleted;
}

void WebRTCEngine::OnPeerConnectionIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  RTC_LOG(LS_INFO) << "ICE connection state changed: " << new_state;

  if (new_state == webrtc::PeerConnectionInterface::kIceConnectionConnected ||
      new_state == webrtc::PeerConnectionInterface::kIceConnectionCompleted) {
    PublishRemoteTracks("ice-connected");
    LogRemoteMediaState("ice-connected");
  }

  if (observer_) {
    observer_->OnIceConnectionStateChanged(new_state);
  }
}

void WebRTCEngine::OnPeerConnectionIceCandidate(
    const webrtc::IceCandidate* candidate) {
  RTC_LOG(LS_INFO)
      << "ICE candidate generated: " << candidate->sdp_mline_index();

  std::string candidate_str;
  if (candidate->ToString(&candidate_str) && observer_) {
    observer_->OnIceCandidateGenerated(candidate->sdp_mid(),
                                       candidate->sdp_mline_index(),
                                       candidate_str);
  }
}

void WebRTCEngine::OnSessionDescriptionSuccess(
    webrtc::SessionDescriptionInterface* desc,
    bool is_offer) {
  RTC_LOG(LS_INFO) << "=== OnSessionDescriptionSuccess called, is_offer: "
                   << is_offer << " ===";

  std::string sdp;
  desc->ToString(&sdp);

  std::weak_ptr<void> callback_guard = GetPeerConnectionCallbackGuard();
  auto observer = webrtcengine_internal::SetLocalDescriptionObserver::Create(
      [this, callback_guard, sdp, is_offer](webrtc::RTCError error) {
        if (callback_guard.expired()) {
          return;
        }
        if (!error.ok()) {
          RTC_LOG(LS_ERROR)
              << "SetLocalDescription failed: " << error.message();
          if (observer_) {
            observer_->OnError(std::string("SetLocalDescription failed: ") +
                               error.message());
          }
        } else {
          RTC_LOG(LS_INFO) << "SetLocalDescription succeeded, is_offer: "
                           << is_offer;
          ProcessPendingIceCandidates();

          if (observer_) {
            if (is_offer) {
              RTC_LOG(LS_INFO) << "Calling observer_->OnOfferCreated()";
              observer_->OnOfferCreated(sdp);
            } else {
              RTC_LOG(LS_INFO) << "Calling observer_->OnAnswerCreated()";
              observer_->OnAnswerCreated(sdp);
            }
          } else {
            RTC_LOG(LS_ERROR) << "observer_ is null!";
          }
        }
      });

  RTC_LOG(LS_INFO) << "Calling SetLocalDescription...";
  peer_connection_->SetLocalDescription(
      std::unique_ptr<webrtc::SessionDescriptionInterface>(desc), observer);
}

void WebRTCEngine::OnSessionDescriptionFailure(const std::string& error) {
  RTC_LOG(LS_ERROR) << "Create session description failed: " << error;
  if (observer_) {
    observer_->OnError("Create session description failed: " + error);
  }
}
