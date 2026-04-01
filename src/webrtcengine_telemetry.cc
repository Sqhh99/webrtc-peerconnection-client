#include "webrtcengine_internal.h"

#include "rtc_base/ref_counted_object.h"

void WebRTCEngine::CollectStats(
    std::function<void(
        const webrtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback) {
  if (!peer_connection_) {
    if (callback) {
      callback(nullptr);
    }
    return;
  }
  std::weak_ptr<void> callback_guard = GetPeerConnectionCallbackGuard();
  peer_connection_->GetStats(new webrtc::RefCountedObject<StatsCollectorCallback>(
      [callback = std::move(callback), callback_guard](
          const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
        if (callback_guard.expired()) {
          return;
        }
        if (callback) {
          callback(report);
        }
      }));
}

WebRTCEngine::AudioTransportState WebRTCEngine::GetAudioTransportState() const {
  return telemetry_->Snapshot(audio_device_module_);
}
