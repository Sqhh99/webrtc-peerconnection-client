#ifndef MANAGED_VIDEO_TRACK_SOURCE_H_GUARD
#define MANAGED_VIDEO_TRACK_SOURCE_H_GUARD

#include "pc/video_track_source.h"

class ManagedVideoTrackSource : public webrtc::VideoTrackSource {
 public:
  ManagedVideoTrackSource() : webrtc::VideoTrackSource(/*remote=*/false) {}
  ~ManagedVideoTrackSource() override = default;

  virtual void Stop() = 0;
};

#endif  // MANAGED_VIDEO_TRACK_SOURCE_H_GUARD
