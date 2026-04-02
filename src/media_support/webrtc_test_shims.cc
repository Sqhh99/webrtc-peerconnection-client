/*
 * WebRTC test shim implementations used by runtime media support.
 */

#include <memory>
#include <optional>

#include "absl/memory/memory.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/test/frame_generator_interface.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/time_utils.h"
#include "system_wrappers/include/clock.h"
#include "media_support/frame_generator.h"
#include "media_support/frame_generator_capturer.h"
#include "media_support/platform_video_capturer.h"
#include "media_support/test_video_capturer.h"
#include "media_support/vcm_capturer.h"

namespace webrtc {
namespace test {

// CreateVideoCapturer is already implemented in platform_video_capturer.cc.

// Implementation of CreateSquareFrameGenerator using SquareGenerator
std::unique_ptr<FrameGeneratorInterface> CreateSquareFrameGenerator(
    int width,
    int height,
    std::optional<FrameGeneratorInterface::OutputType> type,
    std::optional<int> num_squares) {
  return std::make_unique<SquareGenerator>(
      width, height,
      type.value_or(FrameGeneratorInterface::OutputType::kI420),
      num_squares.value_or(10));
}

}  // namespace test
}  // namespace webrtc
