#include "switchable_audio_input_win.h"

#include <algorithm>
#include <chrono>

namespace {

constexpr uint32_t kDefaultSyntheticSampleRateHz = 48000;
constexpr size_t kDefaultSyntheticChannels = 2;
constexpr std::chrono::milliseconds kSyntheticChunkDuration(10);
constexpr size_t kMaxBufferedSyntheticMs = 4000;

}  // namespace

SwitchableAudioInput::SwitchableAudioInput(const webrtc::Environment& env,
                                           bool automatic_restart)
    : env_(env),
      automatic_restart_(automatic_restart),
      live_input_(
          std::make_unique<webrtc::webrtc_win::CoreAudioInput>(env, automatic_restart)) {}

SwitchableAudioInput::~SwitchableAudioInput() {
  Terminate();
}

void SwitchableAudioInput::UseMicrophone() {
  bool should_start_live = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (mode_ == CaptureMode::kMicrophone) {
      return;
    }
    mode_ = CaptureMode::kMicrophone;
    synthetic_samples_.clear();
    should_start_live = recording_requested_;
  }

  StopSyntheticThread();
  if (should_start_live) {
    live_input_->InitRecording();
    live_input_->StartRecording();
  }
}

void SwitchableAudioInput::UseSyntheticPcm(uint32_t sample_rate_hz,
                                           size_t channels) {
  bool should_start_synthetic = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = CaptureMode::kSynthetic;
    synthetic_sample_rate_hz_ =
        sample_rate_hz == 0 ? kDefaultSyntheticSampleRateHz : sample_rate_hz;
    synthetic_channels_ = channels == 0 ? kDefaultSyntheticChannels : channels;
    recording_initialized_ = true;
    should_start_synthetic = recording_requested_;
    if (audio_buffer_) {
      audio_buffer_->SetRecordingSampleRate(synthetic_sample_rate_hz_);
      audio_buffer_->SetRecordingChannels(synthetic_channels_);
    }
  }

  live_input_->StopRecording();
  if (should_start_synthetic) {
    std::lock_guard<std::mutex> lock(mutex_);
    StartSyntheticThreadLocked();
  }
}

void SwitchableAudioInput::ClearSyntheticAudio() {
  std::lock_guard<std::mutex> lock(mutex_);
  synthetic_samples_.clear();
}

void SwitchableAudioInput::PushSyntheticAudio(const int16_t* samples,
                                              size_t frames) {
  if (!samples || frames == 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const size_t channels = std::max<size_t>(1, synthetic_channels_);
  const size_t max_samples =
      (synthetic_sample_rate_hz_ * channels * kMaxBufferedSyntheticMs) / 1000;
  const size_t total_samples = frames * channels;
  for (size_t i = 0; i < total_samples; ++i) {
    synthetic_samples_.push_back(samples[i]);
  }
  while (synthetic_samples_.size() > max_samples) {
    synthetic_samples_.pop_front();
  }
  condition_variable_.notify_all();
}

size_t SwitchableAudioInput::QueuedSyntheticFrames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t channels = std::max<size_t>(1, synthetic_channels_);
  return synthetic_samples_.size() / channels;
}

int SwitchableAudioInput::Init() {
  const int result = live_input_->Init();
  initialized_ = (result == 0);
  return result;
}

int SwitchableAudioInput::Terminate() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    recording_requested_ = false;
  }
  StopSyntheticThread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    synthetic_samples_.clear();
  }
  recording_initialized_ = false;
  initialized_ = false;
  return live_input_->Terminate();
}

int SwitchableAudioInput::NumDevices() const {
  return live_input_->NumDevices();
}

int SwitchableAudioInput::SetDevice(int index) {
  return live_input_->SetDevice(index);
}

int SwitchableAudioInput::SetDevice(
    webrtc::AudioDeviceModule::WindowsDeviceType device) {
  return live_input_->SetDevice(device);
}

int SwitchableAudioInput::DeviceName(int index,
                                     std::string* name,
                                     std::string* guid) {
  return live_input_->DeviceName(index, name, guid);
}

void SwitchableAudioInput::AttachAudioBuffer(
    webrtc::AudioDeviceBuffer* audio_buffer) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_buffer_ = audio_buffer;
    if (audio_buffer_ && mode_ == CaptureMode::kSynthetic) {
      audio_buffer_->SetRecordingSampleRate(synthetic_sample_rate_hz_);
      audio_buffer_->SetRecordingChannels(synthetic_channels_);
    }
  }
  live_input_->AttachAudioBuffer(audio_buffer);
}

bool SwitchableAudioInput::RecordingIsInitialized() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == CaptureMode::kSynthetic) {
    return recording_initialized_;
  }
  return live_input_->RecordingIsInitialized();
}

int SwitchableAudioInput::InitRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == CaptureMode::kSynthetic) {
    recording_initialized_ = true;
    if (audio_buffer_) {
      audio_buffer_->SetRecordingSampleRate(synthetic_sample_rate_hz_);
      audio_buffer_->SetRecordingChannels(synthetic_channels_);
    }
    return 0;
  }
  return live_input_->InitRecording();
}

int SwitchableAudioInput::StartRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  recording_requested_ = true;
  if (mode_ == CaptureMode::kSynthetic) {
    StartSyntheticThreadLocked();
    return 0;
  }
  return live_input_->StartRecording();
}

int SwitchableAudioInput::StopRecording() {
  bool stop_synthetic = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    recording_requested_ = false;
    stop_synthetic = mode_ == CaptureMode::kSynthetic;
  }

  if (stop_synthetic) {
    StopSyntheticThread();
    return 0;
  }
  return live_input_->StopRecording();
}

bool SwitchableAudioInput::Recording() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == CaptureMode::kSynthetic) {
    return synthetic_thread_running_;
  }
  return live_input_->Recording();
}

int SwitchableAudioInput::VolumeIsAvailable(bool* available) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == CaptureMode::kSynthetic) {
    *available = false;
    return 0;
  }
  return live_input_->VolumeIsAvailable(available);
}

int SwitchableAudioInput::RestartRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == CaptureMode::kSynthetic) {
    return 0;
  }
  return live_input_->RestartRecording();
}

bool SwitchableAudioInput::Restarting() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == CaptureMode::kSynthetic) {
    return false;
  }
  return live_input_->Restarting();
}

int SwitchableAudioInput::SetSampleRate(uint32_t sample_rate) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == CaptureMode::kSynthetic) {
    synthetic_sample_rate_hz_ = sample_rate;
    if (audio_buffer_) {
      audio_buffer_->SetRecordingSampleRate(synthetic_sample_rate_hz_);
    }
    return 0;
  }
  return live_input_->SetSampleRate(sample_rate);
}

void SwitchableAudioInput::StartSyntheticThreadLocked() {
  if (synthetic_thread_running_) {
    return;
  }
  stop_synthetic_thread_ = false;
  synthetic_thread_running_ = true;
  synthetic_thread_ = std::thread(&SwitchableAudioInput::SyntheticLoop, this);
}

void SwitchableAudioInput::StopSyntheticThread() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!synthetic_thread_running_ && !synthetic_thread_.joinable()) {
      return;
    }
    stop_synthetic_thread_ = true;
    condition_variable_.notify_all();
  }
  if (synthetic_thread_.joinable()) {
    synthetic_thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  synthetic_thread_running_ = false;
  stop_synthetic_thread_ = false;
}

void SwitchableAudioInput::SyntheticLoop() {
  auto next_tick = std::chrono::steady_clock::now();
  while (true) {
    std::vector<int16_t> chunk;
    webrtc::AudioDeviceBuffer* audio_buffer = nullptr;
    uint32_t sample_rate_hz = kDefaultSyntheticSampleRateHz;
    size_t channels = kDefaultSyntheticChannels;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (stop_synthetic_thread_) {
        break;
      }
      audio_buffer = audio_buffer_;
      sample_rate_hz = synthetic_sample_rate_hz_;
      channels = std::max<size_t>(1, synthetic_channels_);
      const size_t frames_per_chunk = sample_rate_hz / 100;
      const size_t samples_per_chunk = frames_per_chunk * channels;
      chunk.resize(samples_per_chunk, 0);
      const size_t available_samples =
          std::min(samples_per_chunk, synthetic_samples_.size());
      for (size_t i = 0; i < available_samples; ++i) {
        chunk[i] = synthetic_samples_.front();
        synthetic_samples_.pop_front();
      }
    }

    if (audio_buffer) {
      const size_t frames_per_chunk = sample_rate_hz / 100;
      audio_buffer->SetRecordingSampleRate(sample_rate_hz);
      audio_buffer->SetRecordingChannels(channels);
      audio_buffer->SetRecordedBuffer(chunk.data(), frames_per_chunk);
      audio_buffer->SetVQEData(0, 0);
      audio_buffer->DeliverRecordedData();
    }

    next_tick += kSyntheticChunkDuration;
    std::this_thread::sleep_until(next_tick);
  }
}
