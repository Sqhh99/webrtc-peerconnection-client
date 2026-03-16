#ifndef SWITCHABLE_AUDIO_INPUT_WIN_H_GUARD
#define SWITCHABLE_AUDIO_INPUT_WIN_H_GUARD

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "api/environment/environment.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/win/audio_device_module_win.h"
#include "modules/audio_device/win/core_audio_input_win.h"

class SwitchableAudioInput : public webrtc::webrtc_win::AudioInput {
 public:
  SwitchableAudioInput(const webrtc::Environment& env, bool automatic_restart);
  ~SwitchableAudioInput() override;

  SwitchableAudioInput(const SwitchableAudioInput&) = delete;
  SwitchableAudioInput& operator=(const SwitchableAudioInput&) = delete;

  void UseMicrophone();
  void UseSyntheticPcm(uint32_t sample_rate_hz, size_t channels);
  void ClearSyntheticAudio();
  void PushSyntheticAudio(const int16_t* samples, size_t frames);
  size_t QueuedSyntheticFrames() const;

  int Init() override;
  int Terminate() override;
  int NumDevices() const override;
  int SetDevice(int index) override;
  int SetDevice(webrtc::AudioDeviceModule::WindowsDeviceType device) override;
  int DeviceName(int index, std::string* name, std::string* guid) override;
  void AttachAudioBuffer(webrtc::AudioDeviceBuffer* audio_buffer) override;
  bool RecordingIsInitialized() const override;
  int InitRecording() override;
  int StartRecording() override;
  int StopRecording() override;
  bool Recording() override;
  int VolumeIsAvailable(bool* available) override;
  int RestartRecording() override;
  bool Restarting() const override;
  int SetSampleRate(uint32_t sample_rate) override;

 private:
  enum class CaptureMode {
    kMicrophone,
    kSynthetic,
  };

  void StartSyntheticThreadLocked();
  void StopSyntheticThread();
  void SyntheticLoop();

  const webrtc::Environment env_;
  const bool automatic_restart_;
  std::unique_ptr<webrtc::webrtc_win::CoreAudioInput> live_input_;

  mutable std::mutex mutex_;
  std::condition_variable condition_variable_;
  webrtc::AudioDeviceBuffer* audio_buffer_ = nullptr;
  CaptureMode mode_ = CaptureMode::kMicrophone;
  bool initialized_ = false;
  bool recording_initialized_ = false;
  bool recording_requested_ = false;
  bool synthetic_thread_running_ = false;
  bool stop_synthetic_thread_ = false;
  uint32_t synthetic_sample_rate_hz_ = 48000;
  size_t synthetic_channels_ = 2;
  std::deque<int16_t> synthetic_samples_;
  std::thread synthetic_thread_;
};

#endif  // SWITCHABLE_AUDIO_INPUT_WIN_H_GUARD
