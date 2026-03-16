#include "ffmpeg_file_source.h"

#include <chrono>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "api/make_ref_counted.h"
#include "api/video/i420_buffer.h"
#include "rtc_base/logging.h"
#include "switchable_audio_input_win.h"

namespace {

constexpr uint32_t kSyntheticAudioSampleRateHz = 48000;
constexpr size_t kSyntheticAudioChannels = 2;
constexpr size_t kMaxBufferedAudioFrames = kSyntheticAudioSampleRateHz * 2;

std::string FfmpegErrorToString(int error_code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(error_code, buffer, sizeof(buffer));
  return std::string(buffer);
}

int64_t FramePtsUs(const AVFrame* frame, const AVStream* stream) {
  const int64_t pts =
      frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts
                                                     : frame->best_effort_timestamp;
  if (pts == AV_NOPTS_VALUE) {
    return 0;
  }
  return av_rescale_q(pts, stream->time_base, AVRational{1, 1000000});
}

bool OpenCodecContext(AVFormatContext* format_context,
                      AVMediaType media_type,
                      int* stream_index,
                      AVCodecContext** codec_context,
                      std::string* error_message) {
  *stream_index = av_find_best_stream(format_context, media_type, -1, -1,
                                      nullptr, 0);
  if (*stream_index < 0) {
    if (media_type == AVMEDIA_TYPE_AUDIO) {
      return false;
    }
    if (error_message) {
      *error_message = "Failed to locate a decodable video stream: " +
                       FfmpegErrorToString(*stream_index);
    }
    return false;
  }

  const AVStream* stream = format_context->streams[*stream_index];
  const AVCodec* codec =
      avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    if (error_message) {
      *error_message = "Failed to find a decoder for the media stream.";
    }
    return false;
  }

  AVCodecContext* context = avcodec_alloc_context3(codec);
  if (!context) {
    if (error_message) {
      *error_message = "Failed to allocate an FFmpeg codec context.";
    }
    return false;
  }

  int result = avcodec_parameters_to_context(context, stream->codecpar);
  if (result < 0) {
    if (error_message) {
      *error_message = "Failed to copy codec parameters: " +
                       FfmpegErrorToString(result);
    }
    avcodec_free_context(&context);
    return false;
  }

  result = avcodec_open2(context, codec, nullptr);
  if (result < 0) {
    if (error_message) {
      *error_message = "Failed to open codec: " +
                       FfmpegErrorToString(result);
    }
    avcodec_free_context(&context);
    return false;
  }

  *codec_context = context;
  return true;
}

}  // namespace

bool FfmpegFileSource::ProbeFile(const std::string& file_path,
                                 std::string* error_message) {
  AVFormatContext* format_context = nullptr;
  int result = avformat_open_input(&format_context, file_path.c_str(),
                                   nullptr, nullptr);
  if (result < 0) {
    if (error_message) {
      *error_message = "Failed to open media file: " +
                       FfmpegErrorToString(result);
    }
    return false;
  }

  result = avformat_find_stream_info(format_context, nullptr);
  if (result < 0) {
    if (error_message) {
      *error_message = "Failed to read media stream information: " +
                       FfmpegErrorToString(result);
    }
    avformat_close_input(&format_context);
    return false;
  }

  const int video_stream_index =
      av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr,
                          0);
  bool ok = video_stream_index >= 0;
  if (!ok && error_message) {
    *error_message = "Failed to locate a decodable video stream: " +
                     FfmpegErrorToString(video_stream_index);
  }

  avformat_close_input(&format_context);
  return ok;
}

webrtc::scoped_refptr<FfmpegFileSource> FfmpegFileSource::Create(
    const webrtc::Environment& env,
    const std::string& file_path,
    SwitchableAudioInput* audio_input,
    std::string* error_message) {
  if (!audio_input) {
    if (error_message) {
      *error_message = "Internal error: missing switchable audio input.";
    }
    return nullptr;
  }

  std::string probe_error;
  if (!ProbeFile(file_path, &probe_error)) {
    if (error_message) {
      *error_message = probe_error;
    }
    return nullptr;
  }

  auto source =
      webrtc::make_ref_counted<FfmpegFileSource>(env, file_path, audio_input);
  if (!source->Start(error_message)) {
    return nullptr;
  }
  return source;
}

FfmpegFileSource::FfmpegFileSource(const webrtc::Environment& env,
                                   std::string file_path,
                                   SwitchableAudioInput* audio_input)
    : env_(env),
      file_path_(std::move(file_path)),
      audio_input_(audio_input) {}

FfmpegFileSource::~FfmpegFileSource() {
  Stop();
}

void FfmpegFileSource::Stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (decode_thread_.joinable()) {
    decode_thread_.join();
  }
  if (audio_input_) {
    audio_input_->ClearSyntheticAudio();
  }
  SetState(kMuted);
}

webrtc::VideoSourceInterface<webrtc::VideoFrame>* FfmpegFileSource::source() {
  return &broadcaster_;
}

bool FfmpegFileSource::Start(std::string* /*error_message*/) {
  running_.store(true);
  decode_thread_ = std::thread(&FfmpegFileSource::DecodeLoop, this);
  SetState(kLive);
  return true;
}

void FfmpegFileSource::DecodeLoop() {
  audio_input_->UseSyntheticPcm(kSyntheticAudioSampleRateHz,
                                kSyntheticAudioChannels);
  audio_input_->ClearSyntheticAudio();

  AVFormatContext* format_context = nullptr;
  AVCodecContext* video_codec_context = nullptr;
  AVCodecContext* audio_codec_context = nullptr;
  AVFrame* frame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  SwsContext* sws_context = nullptr;
  SwrContext* swr_context = nullptr;
  int result = 0;
  int video_stream_index = -1;
  int audio_stream_index = -1;
  std::string error_message;
  const AVStream* video_stream = nullptr;
  const AVStream* audio_stream = nullptr;
  auto playback_start = std::chrono::steady_clock::now();
  int64_t first_video_pts_us = -1;

  if (!frame || !packet) {
    RTC_LOG(LS_ERROR) << "Failed to allocate FFmpeg frame or packet.";
    goto cleanup;
  }

  result = avformat_open_input(&format_context, file_path_.c_str(), nullptr,
                               nullptr);
  if (result < 0) {
    RTC_LOG(LS_ERROR) << "Failed to open media file " << file_path_ << ": "
                      << FfmpegErrorToString(result);
    goto cleanup;
  }

  result = avformat_find_stream_info(format_context, nullptr);
  if (result < 0) {
    RTC_LOG(LS_ERROR) << "Failed to read stream info for " << file_path_ << ": "
                      << FfmpegErrorToString(result);
    goto cleanup;
  }

  if (!OpenCodecContext(format_context, AVMEDIA_TYPE_VIDEO,
                        &video_stream_index, &video_codec_context,
                        &error_message)) {
    RTC_LOG(LS_ERROR) << error_message;
    goto cleanup;
  }

  OpenCodecContext(format_context, AVMEDIA_TYPE_AUDIO, &audio_stream_index,
                   &audio_codec_context, nullptr);

  video_stream = format_context->streams[video_stream_index];
  audio_stream =
      audio_stream_index >= 0 ? format_context->streams[audio_stream_index]
                              : nullptr;

  while (running_.load()) {
    result = av_read_frame(format_context, packet);
    if (result == AVERROR_EOF) {
      audio_input_->ClearSyntheticAudio();
      av_seek_frame(format_context, -1, 0, AVSEEK_FLAG_BACKWARD);
      avcodec_flush_buffers(video_codec_context);
      if (audio_codec_context) {
        avcodec_flush_buffers(audio_codec_context);
      }
      playback_start = std::chrono::steady_clock::now();
      first_video_pts_us = -1;
      continue;
    }
    if (result < 0) {
      RTC_LOG(LS_WARNING) << "Error while reading media packets: "
                          << FfmpegErrorToString(result);
      break;
    }

    AVCodecContext* active_codec_context = nullptr;
    const AVStream* active_stream = nullptr;
    const bool is_video_packet = packet->stream_index == video_stream_index;
    const bool is_audio_packet =
        audio_stream_index >= 0 && packet->stream_index == audio_stream_index;

    if (is_video_packet) {
      active_codec_context = video_codec_context;
      active_stream = video_stream;
    } else if (is_audio_packet) {
      active_codec_context = audio_codec_context;
      active_stream = audio_stream;
    } else {
      av_packet_unref(packet);
      continue;
    }

    result = avcodec_send_packet(active_codec_context, packet);
    av_packet_unref(packet);
    if (result < 0) {
      RTC_LOG(LS_WARNING) << "Failed to send packet to decoder: "
                          << FfmpegErrorToString(result);
      continue;
    }

    while (running_.load()) {
      result = avcodec_receive_frame(active_codec_context, frame);
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
        break;
      }
      if (result < 0) {
        RTC_LOG(LS_WARNING) << "Failed to receive decoded frame: "
                            << FfmpegErrorToString(result);
        break;
      }

      if (is_video_packet) {
        const int64_t frame_pts_us = FramePtsUs(frame, active_stream);
        if (first_video_pts_us < 0) {
          first_video_pts_us = frame_pts_us;
          playback_start = std::chrono::steady_clock::now();
        }
        const int64_t relative_pts_us =
            std::max<int64_t>(0, frame_pts_us - first_video_pts_us);
        const auto target_time =
            playback_start + std::chrono::microseconds(relative_pts_us);
        std::this_thread::sleep_until(target_time);

        sws_context = sws_getCachedContext(
            sws_context, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), frame->width,
            frame->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr,
            nullptr);
        if (!sws_context) {
          RTC_LOG(LS_WARNING) << "Failed to create FFmpeg scaling context.";
          continue;
        }

        auto buffer = webrtc::I420Buffer::Create(frame->width, frame->height);
        uint8_t* destination_data[] = {buffer->MutableDataY(),
                                       buffer->MutableDataU(),
                                       buffer->MutableDataV(), nullptr};
        int destination_linesize[] = {buffer->StrideY(), buffer->StrideU(),
                                      buffer->StrideV(), 0};
        sws_scale(sws_context, frame->data, frame->linesize, 0, frame->height,
                  destination_data, destination_linesize);

        webrtc::VideoFrame video_frame =
            webrtc::VideoFrame::Builder()
                .set_video_frame_buffer(buffer)
                .set_timestamp_us(relative_pts_us)
                .build();
        broadcaster_.OnFrame(video_frame);
      } else if (audio_codec_context) {
        AVChannelLayout output_layout;
        av_channel_layout_default(&output_layout,
                                  static_cast<int>(kSyntheticAudioChannels));
        result = swr_alloc_set_opts2(
            &swr_context, &output_layout, AV_SAMPLE_FMT_S16,
            kSyntheticAudioSampleRateHz, &frame->ch_layout,
            static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0,
            nullptr);
        av_channel_layout_uninit(&output_layout);
        if (result < 0 || !swr_context || swr_init(swr_context) < 0) {
          RTC_LOG(LS_WARNING) << "Failed to configure FFmpeg audio resampler.";
          continue;
        }

        const int dst_nb_samples = av_rescale_rnd(
            swr_get_delay(swr_context, frame->sample_rate) + frame->nb_samples,
            kSyntheticAudioSampleRateHz, frame->sample_rate, AV_ROUND_UP);
        std::vector<int16_t> pcm(static_cast<size_t>(dst_nb_samples) *
                                 kSyntheticAudioChannels);
        uint8_t* output[] = {
            reinterpret_cast<uint8_t*>(pcm.data()),
            nullptr,
        };
        const uint8_t* const* input_data =
            reinterpret_cast<const uint8_t* const*>(frame->extended_data);
        const int converted = swr_convert(swr_context, output, dst_nb_samples,
                                          input_data, frame->nb_samples);
        if (converted > 0) {
          audio_input_->PushSyntheticAudio(pcm.data(),
                                           static_cast<size_t>(converted));
          while (running_.load() &&
                 audio_input_->QueuedSyntheticFrames() >
                     kMaxBufferedAudioFrames) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
        }
      }
    }
  }

cleanup:
  if (swr_context) {
    swr_free(&swr_context);
  }
  if (sws_context) {
    sws_freeContext(sws_context);
  }
  if (packet) {
    av_packet_free(&packet);
  }
  if (frame) {
    av_frame_free(&frame);
  }
  if (audio_codec_context) {
    avcodec_free_context(&audio_codec_context);
  }
  if (video_codec_context) {
    avcodec_free_context(&video_codec_context);
  }
  if (format_context) {
    avformat_close_input(&format_context);
  }
  audio_input_->ClearSyntheticAudio();
}
