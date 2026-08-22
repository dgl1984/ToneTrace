#include <clap/clap.h>

#ifndef TONETRACE_VERSION
#define TONETRACE_VERSION "0.0.0-dev"
#endif

#include "tonetrace/tonetrace_engine.h"
#include "tonetrace/tonetrace_realtime.h"

#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
#include "tonetrace_win32_editor.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kPluginId = "com.lanesaudio.tonetrace-eq";
constexpr double kMaximumCaptureSeconds = 30.0;
constexpr std::size_t kMaximumStateBytes = 64U * 1024U * 1024U;

enum class Status : int {
  Ready = 0,
  CapturingReference = 1,
  CapturingTarget = 2,
  Analyzing = 3,
  Preview = 4,
  Frozen = 5,
  ResetArmed = 6,
  CaptureFull = 7,
  InvalidCapture = 8,
  AnalysisFailed = 9,
  RendererBusy = 10,
  ReferenceLowConfidence = 11,
  ReferenceMediumConfidence = 12,
  ReferenceHighConfidence = 13,
  TargetLowConfidence = 14,
  TargetMediumConfidence = 15,
  TargetHighConfidence = 16,
  SetupChanged = 17,
  SetupLocked = 18,
  ReferenceAudioDetected = 19,
  ReferenceCollecting = 20,
  ReferenceUnstable = 21,
  ReferenceReady = 22,
  TargetAudioDetected = 23,
  TargetCollecting = 24,
  TargetUnstable = 25,
  TargetReady = 26,
  CannotSaveYet = 27,
  ImportedReference = 28,
  ImportedTarget = 29,
};

enum WorkFlags : std::uint32_t {
  WorkNone = 0,
  WorkAnalyze = 1U << 0U,
  WorkRebuild = 1U << 1U,
  WorkReset = 1U << 2U,
  WorkImport = 1U << 3U,
  WorkCleanup = 1U << 4U,
};

const char* const kFeatures[]{
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_EQUALIZER,
    CLAP_PLUGIN_FEATURE_ANALYZER,
    CLAP_PLUGIN_FEATURE_UTILITY,
    CLAP_PLUGIN_FEATURE_MIXING,
    CLAP_PLUGIN_FEATURE_MASTERING,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor{
    CLAP_VERSION,
    kPluginId,
    "Tone Trace EQ",
    "LanesAudio",
    "https://lanesaudio.com",
    "https://lanesaudio.com",
    "https://lanesaudio.com",
    TONETRACE_VERSION,
    "Reference-to-Target match EQ",
    kFeatures,
};

std::size_t parameterIndex(clap_id id) noexcept {
  const auto& descriptors = tonetrace::parameterDescriptors();
  for (std::size_t i = 0; i < descriptors.size(); ++i) {
    if (static_cast<clap_id>(descriptors[i].id) == id) return i;
  }
  return descriptors.size();
}

double clampedValue(const tonetrace::ParameterDescriptor& descriptor,
                    double value) noexcept {
  if (!std::isfinite(value)) return descriptor.defaultValue;
  value = std::clamp(value, descriptor.minimum, descriptor.maximum);
  // CLAP defines stepped values with integer-cast/truncation semantics. Keep
  // that contract for real fractional input, but tolerate tiny floating-point
  // error around an integer so 1.999999999 from a normalized host control does
  // not fall back to Workflow Step 1 instead of Step 2.
  if (descriptor.stepped) {
    const double nearest = std::round(value);
    const double tolerance =
        1.0e-7 * std::max(1.0, std::abs(nearest));
    value = std::abs(value - nearest) <= tolerance ? nearest
                                                   : std::trunc(value);
  }
  return std::clamp(value, descriptor.minimum, descriptor.maximum);
}

const char* statusText(int status) noexcept {
  switch (static_cast<Status>(status)) {
    case Status::CapturingReference: return "Capturing Reference; no valid audio";
    case Status::CapturingTarget: return "Learning Target; no valid audio";
    case Status::Analyzing: return "Analyzing";
    case Status::Preview: return "Preview correction";
    case Status::Frozen: return "Frozen correction";
    case Status::ResetArmed: return "Reset armed; confirm or cancel";
    case Status::CaptureFull: return "Capture full; continue workflow";
    case Status::InvalidCapture: return "Invalid or contaminated capture";
    case Status::AnalysisFailed: return "Analysis failed; previous profile preserved";
    case Status::RendererBusy: return "Correction update still completing";
    case Status::ReferenceLowConfidence:
      return "Capturing Reference; low confidence; usable with caution";
    case Status::ReferenceMediumConfidence:
      return "Capturing Reference; medium confidence";
    case Status::ReferenceHighConfidence:
      return "Capturing Reference; high confidence";
    case Status::TargetLowConfidence:
      return "Learning Target; low confidence; usable with caution";
    case Status::TargetMediumConfidence:
      return "Learning Target; medium confidence";
    case Status::TargetHighConfidence:
      return "Learning Target; high confidence";
    case Status::SetupChanged:
      return "Frozen; setup changed; relearn recommended; tones muted";
    case Status::SetupLocked:
      return "Setup locked; reset or restart capture";
    case Status::ReferenceAudioDetected:
      return "Capturing Reference; audio detected";
    case Status::ReferenceCollecting:
      return "Capturing Reference; collecting";
    case Status::ReferenceUnstable:
      return "Capturing Reference; unstable audio";
    case Status::ReferenceReady:
      return "Reference capture ready";
    case Status::TargetAudioDetected:
      return "Learning Target; audio detected";
    case Status::TargetCollecting:
      return "Learning Target; collecting";
    case Status::TargetUnstable:
      return "Learning Target; unstable audio";
    case Status::TargetReady:
      return "Target capture ready";
    case Status::CannotSaveYet:
      return "Cannot save yet; keep capturing";
    case Status::ImportedReference:
      return "Reference imported; record the Target or import one";
    case Status::ImportedTarget:
      return "Target imported; analyzing correction";
    case Status::Ready:
    default: return "Ready";
  }
}

const char* workflowText(int value) noexcept {
  switch (value) {
    case 1: return "Capture Reference";
    case 2: return "Save Reference and Learn Target";
    case 3: return "Correct Target";
    case 4: return "Freeze Correction";
    case 5: return "Arm Reset";
    case 6: return "Confirm Reset";
    case 7: return "Cancel Reset";
    default: return "No action";
  }
}

const char* modeText(int value) noexcept {
  switch (value) {
    case 1: return "Voice";
    case 2: return "Drums";
    case 3: return "Bass or Synth";
    case 4: return "Custom Max Capture";
    default: return "Full Mix";
  }
}

tonetrace::MatchMode matchMode(double value) noexcept {
  switch (static_cast<int>(std::trunc(value))) {
    case 1: return tonetrace::MatchMode::Voice;
    case 2: return tonetrace::MatchMode::Drums;
    case 3: return tonetrace::MatchMode::BassSynth;
    case 4: return tonetrace::MatchMode::CustomMaxCapture;
    default: return tonetrace::MatchMode::FullMix;
  }
}

struct CaptureBuffer {
  std::array<std::vector<float>, 2> channels;
  std::size_t capacityFrames = 0;
  std::size_t frames = 0;
  std::size_t channelCount = 0;
  bool overflowed = false;
  tonetrace::CaptureDiagnostics diagnostics;
  std::uint64_t receivedFrames = 0;
  std::uint64_t validFrames = 0;
  std::uint64_t updateCounter = 0;
  std::uint64_t intervalFrames = 0;
  std::uint64_t acceptedSampleCount = 0;
  std::uint64_t acceptedClippedSamples = 0;
  double smoothedPower = 0.0;
  double stability = 1.0;
  int confidenceLevel = 0;
  std::array<std::array<double, 6>, 2> monitorLowpass{};
  std::array<double, 7> intervalBandPower{};
  std::array<double, 7> bandMomentSum{};
  std::array<double, 7> previousBandAggregate{};

  void prepare(int sampleRate) {
    capacityFrames = static_cast<std::size_t>(
        std::ceil(sampleRate * kMaximumCaptureSeconds));
    for (auto& channel : channels) channel.assign(capacityFrames, 0.0F);
    reset();
  }

  void reset() noexcept {
    frames = 0;
    channelCount = 0;
    overflowed = false;
    diagnostics = {};
    receivedFrames = 0;
    validFrames = 0;
    updateCounter = 0;
    intervalFrames = 0;
    acceptedSampleCount = 0;
    acceptedClippedSamples = 0;
    smoothedPower = 0.0;
    stability = 1.0;
    confidenceLevel = 0;
    monitorLowpass = {};
    intervalBandPower = {};
    bandMomentSum = {};
    previousBandAggregate = {};
  }

  void release() noexcept {
    for (auto& channel : channels) {
      std::vector<float>().swap(channel);
    }
    capacityFrames = 0;
    reset();
  }

  void append(const float* const* input,
              std::size_t inputChannels,
              std::size_t count,
              int sampleRate,
              tonetrace::MatchMode mode) noexcept {
    if (input == nullptr || inputChannels == 0) return;
    inputChannels = std::min<std::size_t>(2, inputChannels);
    if (channelCount == 0) channelCount = inputChannels;
    if (channelCount != inputChannels) {
      diagnostics.nonFiniteSamples = 1;
      return;
    }
    diagnostics.observe(input, inputChannels, count);
    receivedFrames += count;

    const double gateDb = mode == tonetrace::MatchMode::Voice ? -55.0 : -65.0;
    const double gatePower = std::pow(10.0, gateDb / 10.0);
    const double alpha = 1.0 - std::exp(
        -1.0 / std::max(1.0, 0.030 * static_cast<double>(sampleRate)));
    const std::uint64_t updateFrames = static_cast<std::uint64_t>(
        std::max(1.0, 0.050 * static_cast<double>(sampleRate)));
    constexpr std::array<double, 6> monitorCutoffs{
        100.0, 300.0, 1000.0, 3000.0, 8000.0, 16000.0};
    std::array<double, monitorCutoffs.size()> monitorAlpha{};
    for (std::size_t band = 0; band < monitorCutoffs.size(); ++band) {
      const double cutoff = std::min(
          monitorCutoffs[band], sampleRate * 0.45);
      monitorAlpha[band] = 1.0 - std::exp(
          -6.2831853071795864769 * cutoff / sampleRate);
    }

    for (std::size_t frame = 0; frame < count; ++frame) {
      double power = 0.0;
      std::array<double, 7> bandPower{};
      bool finite = true;
      for (std::size_t channel = 0; channel < inputChannels; ++channel) {
        const double sample = input[channel][frame];
        finite = finite && std::isfinite(sample);
        power += sample * sample;
      }
      if (!finite) continue;
      for (std::size_t channel = 0; channel < inputChannels; ++channel) {
        const double sample = input[channel][frame];
        double previousLowpass = 0.0;
        for (std::size_t band = 0; band < monitorCutoffs.size(); ++band) {
          auto& state = monitorLowpass[channel][band];
          state += (sample - state) * monitorAlpha[band];
          const double bandSample = state - previousLowpass;
          bandPower[band] += bandSample * bandSample;
          previousLowpass = state;
        }
        const double highSample = sample - previousLowpass;
        bandPower.back() += highSample * highSample;
      }
      power /= static_cast<double>(inputChannels);
      for (auto& band : bandPower) {
        band /= static_cast<double>(inputChannels);
      }
      smoothedPower += (power - smoothedPower) * alpha;
      if (smoothedPower <= gatePower) continue;

      if (frames >= capacityFrames) {
        overflowed = true;
        continue;
      }
      for (std::size_t channel = 0; channel < inputChannels; ++channel) {
        channels[channel][frames] = input[channel][frame];
        ++acceptedSampleCount;
        if (std::abs(input[channel][frame]) >= 0.999F) {
          ++acceptedClippedSamples;
        }
      }
      ++frames;
      ++validFrames;
      ++intervalFrames;
      for (std::size_t band = 0; band < intervalBandPower.size(); ++band) {
        intervalBandPower[band] += bandPower[band];
      }

      if (intervalFrames >= updateFrames) {
        const double completedIntervalFrames =
            static_cast<double>(intervalFrames);
        intervalFrames = 0;
        double totalMovement = 0.0;
        std::size_t comparedBands = 0;
        double momentPower = 4.0;
        if (mode == tonetrace::MatchMode::Voice) momentPower = 3.0;
        else if (mode == tonetrace::MatchMode::Drums) momentPower = 5.0;
        else if (mode == tonetrace::MatchMode::BassSynth) momentPower = 3.5;
        else if (mode == tonetrace::MatchMode::CustomMaxCapture) {
          momentPower = 6.0;
        }
        for (std::size_t band = 0; band < intervalBandPower.size(); ++band) {
          const double intervalMean =
              intervalBandPower[band] / completedIntervalFrames;
          bandMomentSum[band] += std::pow(
              std::max(0.0, intervalMean), momentPower);
          const double aggregate = std::pow(
              bandMomentSum[band] /
                  static_cast<double>(updateCounter + 1U),
              1.0 / momentPower);
          if (aggregate > 1.0e-20 &&
              previousBandAggregate[band] > 1.0e-20) {
            totalMovement += std::abs(std::log(
                aggregate / previousBandAggregate[band]));
            ++comparedBands;
          }
          previousBandAggregate[band] = aggregate;
          intervalBandPower[band] = 0.0;
        }
        const double movement = comparedBands > 0
                                    ? totalMovement / comparedBands
                                    : 1.0;
        stability += (movement - stability) * 0.1;
        ++updateCounter;
      }
    }
    confidenceLevel = calculateConfidence(mode, sampleRate);
  }

  [[nodiscard]] double validSeconds(int sampleRate) const noexcept {
    return sampleRate > 0
               ? static_cast<double>(validFrames) / sampleRate
               : 0.0;
  }

  [[nodiscard]] bool readyForSave(int sampleRate) const noexcept {
    return validSeconds(sampleRate) >= 0.35 && updateCounter >= 3;
  }

  [[nodiscard]] tonetrace::CaptureDiagnostics profileDiagnostics() const noexcept {
    auto result = diagnostics;
    result.sampleCount = acceptedSampleCount;
    result.clippedSamples = acceptedClippedSamples;
    return result;
  }

  [[nodiscard]] int calculateConfidence(tonetrace::MatchMode mode,
                                        int sampleRate) const noexcept {
    const double validTime = validSeconds(sampleRate);
    double lowTime = 1.8;
    double mediumTime = 7.0;
    double highTime = 16.0;
    double lowStability = 0.100;
    double mediumStability = 0.035;
    double highStability = 0.016;
    if (mode == tonetrace::MatchMode::Voice) {
      lowTime = 2.5;
      mediumTime = 8.0;
      highTime = 18.0;
      lowStability = 0.080;
      mediumStability = 0.028;
      highStability = 0.012;
    } else if (mode == tonetrace::MatchMode::Drums) {
      lowTime = 1.5;
      mediumTime = 5.0;
      highTime = 12.0;
      lowStability = 0.120;
      mediumStability = 0.050;
      highStability = 0.025;
    } else if (mode == tonetrace::MatchMode::BassSynth) {
      lowTime = 1.8;
      mediumTime = 6.0;
      highTime = 14.0;
      lowStability = 0.100;
      mediumStability = 0.040;
      highStability = 0.020;
    }
    int level = 0;
    if (validTime >= lowTime && stability <= lowStability) level = 1;
    if (validTime >= mediumTime && stability <= mediumStability) level = 2;
    if (validTime >= highTime && stability <= highStability) level = 3;
    return level;
  }

  tonetrace::AudioBuffer audio(int sampleRate) const {
    if (frames == 0 || channelCount == 0) {
      throw std::runtime_error("Capture contains no audio");
    }
    tonetrace::AudioBuffer result;
    result.sampleRate = sampleRate;
    result.channels.assign(channelCount, std::vector<double>(frames));
    for (std::size_t channel = 0; channel < channelCount; ++channel) {
      for (std::size_t frame = 0; frame < frames; ++frame) {
        result.channels[channel][frame] = channels[channel][frame];
      }
    }
    return result;
  }
};

struct SweepTone {
  bool active = false;
  // True when this tone is a band reference (a trace band tone or a page
  // sweep). Band references are navigational aids and stay audible while a
  // profile is Frozen, where capture/confidence tones are deliberately silent.
  bool bandReference = false;
  double phase = 0.0;
  double frequencyStart = 0.0;
  double frequencyEnd = 0.0;
  std::uint64_t samplesLeft = 0;
  std::uint64_t samplesTotal = 0;

  void stop() noexcept {
    active = false;
    bandReference = false;
    samplesLeft = 0;
    samplesTotal = 0;
    phase = 0.0;
  }

  void start(double fromHz, double toHz, double durationMs,
             int sampleRate) noexcept {
    frequencyStart = fromHz;
    frequencyEnd = toHz;
    samplesTotal = static_cast<std::uint64_t>(std::max(
        1.0, durationMs * 0.001 * static_cast<double>(sampleRate)));
    samplesLeft = samplesTotal;
    phase = 0.0;
    active = true;
    bandReference = false;
  }

  // Same glide, marked as a band reference so it survives the Frozen phase.
  void startBandReference(double fromHz, double toHz, double durationMs,
                          int sampleRate) noexcept {
    start(fromHz, toHz, durationMs, sampleRate);
    bandReference = true;
  }

  // Log-frequency glide: pitch slides exponentially from the low edge to the
  // high edge, so no step is ever audible and adjacent pages meet at the same
  // boundary frequency with no skipped band.
  [[nodiscard]] double currentFrequency() const noexcept {
    const double fraction = samplesTotal > 1
                                ? static_cast<double>(samplesTotal - samplesLeft) /
                                      static_cast<double>(samplesTotal - 1)
                                : 0.0;
    return frequencyStart *
           std::pow(frequencyEnd / frequencyStart, fraction);
  }
};

#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
// The Win32 Beep() API is synchronous and blocks for the duration of each beep.
// When the DAW transport is stopped the host never calls process(), so trace
// navigation falls back to Beeps; running them inline on the Win32 message loop
// froze the UI and delayed NVDA announcements. This worker plays the beeps on a
// background thread and aborts in-flight sweeps the moment a newer request
// arrives.
class StoppedTransportToneWorker {
 public:
  StoppedTransportToneWorker() : thread_([this]() { run(); }) {}

  ~StoppedTransportToneWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      terminating_ = true;
      ++requestSeq_;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
  }

  void requestTone(double frequencyHz) noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      commandType_ = 1;
      frequency_ = frequencyHz;
      ++requestSeq_;
    }
    cv_.notify_one();
  }

  void requestSweep(double fromHz, double toHz, int bandCount) noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      commandType_ = 2;
      fromHz_ = fromHz;
      toHz_ = toHz;
      bandCount_ = bandCount;
      ++requestSeq_;
    }
    cv_.notify_one();
  }

  void cancel() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      commandType_ = 0;
      ++requestSeq_;
    }
    cv_.notify_one();
  }

 private:
  void run() noexcept {
    while (true) {
      int type = 0;
      double freq = 0.0;
      double from = 0.0;
      double to = 0.0;
      int count = 0;
      std::uint64_t seq = 0;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return terminating_ || commandType_ != 0; });
        if (terminating_) return;
        type = commandType_;
        freq = frequency_;
        from = fromHz_;
        to = toHz_;
        count = bandCount_;
        seq = requestSeq_.load(std::memory_order_relaxed);
        commandType_ = 0;
      }

      if (type == 1) {
        if (std::isfinite(freq) && freq > 0.0) {
          Beep(static_cast<DWORD>(std::clamp(freq, 37.0, 32767.0)), 125);
        }
      } else if (type == 2) {
        if (std::isfinite(from) && std::isfinite(to) && from > 0.0 && to > 0.0 && count > 0) {
          const double ratio = to / from;
          const double octave = std::log2(ratio);
          const int totalBeeps = 2 * count - 1;
          for (int index = 0; index < totalBeeps; ++index) {
            if (requestSeq_.load(std::memory_order_relaxed) != seq || terminating_) {
              break;
            }
            const double f = from * std::pow(2.0, (index + 0.5) * octave / totalBeeps);
            if (std::isfinite(f) && f > 0.0) {
              Beep(static_cast<DWORD>(std::clamp(f, 37.0, 32767.0)), 60);
            }
          }
        }
      }
    }
  }

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool terminating_ = false;
  std::atomic<std::uint64_t> requestSeq_{0};
  int commandType_ = 0;
  double frequency_ = 0.0;
  double fromHz_ = 0.0;
  double toHz_ = 0.0;
  int bandCount_ = 0;
};
#endif

class ToneTraceClap {
 public:
  explicit ToneTraceClap(const clap_host_t* host) : host_(host) {
    plugin_.desc = &kDescriptor;
    plugin_.plugin_data = this;
    plugin_.init = pluginInit;
    plugin_.destroy = pluginDestroy;
    plugin_.activate = pluginActivate;
    plugin_.deactivate = pluginDeactivate;
    plugin_.start_processing = pluginStartProcessing;
    plugin_.stop_processing = pluginStopProcessing;
    plugin_.reset = pluginReset;
    plugin_.process = pluginProcess;
    plugin_.get_extension = pluginGetExtension;
    plugin_.on_main_thread = pluginOnMainThread;

    const auto& descriptors = tonetrace::parameterDescriptors();
    values_.reserve(descriptors.size());
    for (const auto& descriptor : descriptors) {
      values_.push_back(std::make_unique<std::atomic<double>>(
          descriptor.defaultValue));
    }
    dirtyValues_.reserve(descriptors.size());
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
      dirtyValues_.push_back(std::make_unique<std::atomic<bool>>(false));
    }
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
    toneWorker_ = std::make_unique<StoppedTransportToneWorker>();
#endif
    setStatus(Status::Ready);
  }

  const clap_plugin_t* clapPlugin() noexcept { return &plugin_; }

 private:
  static ToneTraceClap* self(const clap_plugin_t* plugin) noexcept {
    return plugin == nullptr
               ? nullptr
               : static_cast<ToneTraceClap*>(plugin->plugin_data);
  }

  double value(tonetrace::ParameterId id) const noexcept {
    const std::size_t index = parameterIndex(static_cast<clap_id>(id));
    return index < values_.size()
               ? values_[index]->load(std::memory_order_acquire)
               : 0.0;
  }

  void setValue(tonetrace::ParameterId id, double newValue,
                bool emitToHost = false) noexcept {
    const auto& descriptors = tonetrace::parameterDescriptors();
    const std::size_t index = parameterIndex(static_cast<clap_id>(id));
    if (index >= descriptors.size()) return;
    const double clamped = clampedValue(descriptors[index], newValue);
    const double previous = values_[index]->exchange(
        clamped, std::memory_order_acq_rel);
    if (previous == clamped) return;
    if (emitToHost) markDirty(index);
  }

  void markDirty(std::size_t index) noexcept {
    if (index < dirtyValues_.size()) {
      dirtyValues_[index]->store(true, std::memory_order_release);
    }
  }

  void setStatus(Status status) noexcept {
    setValue(tonetrace::ParameterId::Status, static_cast<double>(status), true);
  }

  void requestMainThread(std::uint32_t work) noexcept {
    const std::uint32_t previous =
        pendingWork_.fetch_or(work, std::memory_order_acq_rel);
    // One scheduled main-thread callback can service every bit accumulated
    // before it runs. Avoid flooding the host when rapid edits coalesce.
    if (previous == WorkNone && host_ != nullptr &&
        host_->request_callback != nullptr) {
      host_->request_callback(host_);
    }
  }

  const clap_host_params_t* hostParams() const noexcept {
    if (host_ == nullptr || host_->get_extension == nullptr) return nullptr;
    return static_cast<const clap_host_params_t*>(
        host_->get_extension(host_, CLAP_EXT_PARAMS));
  }

  void notifyParameterValuesChanged() noexcept {
    const auto* extension = hostParams();
    if (extension != nullptr && extension->rescan != nullptr) {
      extension->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
    }
  }

  // Native-editor changes originate inside the plug-in, so CLAP requires us
  // to send the resulting parameter value back to the host. In REAPER this is
  // also what keeps the generic parameter surface used by OSARA synchronized
  // with buttons pressed in Tone Trace's own editor. This is main/UI-thread
  // work; audio-thread changes are already returned through process().
  void requestHostParameterFlush() noexcept {
    const auto* extension = hostParams();
    if (extension != nullptr && extension->request_flush != nullptr) {
      extension->request_flush(host_);
    }
  }

  tonetrace::MatchSettings currentMatchSettings(bool uncapped) const {
    tonetrace::MatchSettings settings;
    settings.mode = matchMode(value(tonetrace::ParameterId::MatchMode));
    // Capture always retains the full analysis range. The Low/High controls
    // constrain only the rendered correction delta, matching the JSFX contract.
    settings.rangeLowHz = 10.0;
    settings.rangeHighHz = 30000.0;
    settings.resolution = static_cast<int>(
        value(tonetrace::ParameterId::Resolution));
    const bool complete = value(tonetrace::ParameterId::CompleteMatch) >= 0.5;
    settings.maximumCorrectionDb =
        uncapped || complete
            ? 60.0
            : value(tonetrace::ParameterId::MaximumCorrectionDb);
    return settings;
  }

  tonetrace::IrRenderSettings currentRenderSettings() {
    syncManualGainsSize();
    tonetrace::IrRenderSettings settings;
    settings.sampleRate = sampleRate_;
    settings.correctionStrength =
        value(tonetrace::ParameterId::CorrectionStrength);
    settings.correctionSharpness =
        value(tonetrace::ParameterId::CorrectionSharpness);
    settings.correctionGainDb =
        value(tonetrace::ParameterId::CorrectionGainDb);
    settings.rangeLowHz = value(tonetrace::ParameterId::RangeLowHz);
    settings.rangeHighHz = value(tonetrace::ParameterId::RangeHighHz);
    settings.manualGains = manualGains_;
    return settings;
  }

  // Manual band trims must have exactly one entry per trace band. When the
  // user changes Correction Resolution, preserve the trim CURVE by frequency
  // instead of copying indices (which would move edits to different bands).
  void syncManualGainsSize() noexcept {
    const int resolution = static_cast<int>(std::lround(
        value(tonetrace::ParameterId::Resolution)));
    if (resolution < 1) return;
    const std::size_t target = static_cast<std::size_t>(resolution);
    if (manualGains_.size() == target) return;

    double lowHz = 20.0;
    double highHz = 20000.0;
    if (core_ != nullptr) {
      if (const auto* snapshot = core_->frozenSnapshot(); snapshot != nullptr) {
        lowHz = snapshot->uncappedModel.analysisLowHz;
        highHz = snapshot->uncappedModel.analysisHighHz;
      }
    }
    manualGains_ = tonetrace::resampleManualGains(
        manualGains_, target, lowHz, highHz);
  }

  // Manual band trim in dB, applied additively on top of the auto-matched
  // correction. Touched only on the main thread (editor/apply/rebuild).
  void setBandGain(std::size_t index, double gainDb) noexcept {
    if (index >= manualGains_.size() || !std::isfinite(gainDb)) return;
    manualGains_[index] = std::clamp(gainDb, -120.0, 120.0);
    if (hasProfile_.load(std::memory_order_acquire)) {
      requestMainThread(WorkRebuild);
    }
  }

  [[nodiscard]] double getBandGain(std::size_t index) const noexcept {
    if (index >= manualGains_.size()) return 0.0;
    return manualGains_[index];
  }

  [[nodiscard]] std::size_t bandCount() const noexcept {
    return manualGains_.size();
  }

  // Keep the plugin's manual trim buffer aligned with the frozen snapshot the
  // core owns, so a later settings-driven rebuild keeps the loaded trims.
  void syncManualGainsFromSnapshot() noexcept {
    const auto* snapshot = core_ == nullptr ? nullptr : core_->frozenSnapshot();
    if (snapshot != nullptr) {
      manualGains_ = snapshot->renderSettings.manualGains;
    }
    syncManualGainsSize();
  }

  [[nodiscard]] bool setupMatchesFrozenProfile() const noexcept {
    const auto* snapshot = core_ == nullptr ? nullptr : core_->frozenSnapshot();
    return snapshot == nullptr ||
           snapshot->matchSettings.mode ==
               matchMode(value(tonetrace::ParameterId::MatchMode));
  }

  void stopTones() noexcept {
    tone_.stop();
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
    if (toneWorker_) toneWorker_->cancel();
#endif
  }

  // The host tells us (via the clap.render extension) when it is doing an
  // offline render. The editor stops its 33 ms repaint timer, tones are muted
  // so a render never bakes them into the file, and the editor timer restarts
  // when the host returns to realtime processing.
  void setOfflineRendering(bool offline) noexcept {
    offlineRendering_.store(offline, std::memory_order_release);
    stopTones();
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
    if (editor_ != nullptr) editor_->setOfflineRendering(offline);
#endif
  }

  // Capture/confidence/warning tones: never while Frozen and never while an
  // offline render is running (a render must not bake tones into the file).
  [[nodiscard]] bool tonesAllowed() const noexcept {
    return !offlineRendering_.load(std::memory_order_acquire) &&
           value(tonetrace::ParameterId::ToneNotifications) >= 0.5 &&
           value(tonetrace::ParameterId::ToneLevelDb) > -59.5 &&
           value(tonetrace::ParameterId::Bypass) < 0.5 &&
           phase_.load(std::memory_order_acquire) !=
               tonetrace::WorkflowPhase::Frozen;
  }

  // Band reference tones (band tones + page sweeps) are navigational and stay
  // available while Frozen so the user can still hear what band they are on
  // while adjusting the frozen curve; they are muted only during offline
  // rendering.
  [[nodiscard]] bool traceTonesAllowed() const noexcept {
    return !offlineRendering_.load(std::memory_order_acquire) &&
           value(tonetrace::ParameterId::ToneNotifications) >= 0.5 &&
           value(tonetrace::ParameterId::ToneLevelDb) > -59.5 &&
           value(tonetrace::ParameterId::Bypass) < 0.5;
  }

  // The editor asks for a steady reference tone at a trace band's center
  // frequency. The request is handed to the audio thread so the tone state is
  // only mutated where mixTone reads it. Tone level is the Confidence Tone
  // Volume parameter and the tone is mixed after the correction, so a boosted
  // or cut band never changes how loud it plays.
  void requestTraceTone(double frequencyHz) noexcept {
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0) return;
    traceToneFrequency_.store(frequencyHz, std::memory_order_release);
    traceToneRequested_.store(true, std::memory_order_release);
    // The audio thread mixes the tone through mixTone while the host is
    // processing. When the transport is stopped the host never calls process(),
    // so offload to an asynchronous background worker to keep trace navigation
    // audible without blocking the UI thread or delaying screen-reader events.
    if (!processing_.load(std::memory_order_acquire) && traceTonesAllowed()) {
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
      if (toneWorker_) toneWorker_->requestTone(frequencyHz);
#endif
    }
  }

  void startSweep(double fromHz, double toHz, double durationMs) noexcept {
    if (tonesAllowed()) tone_.start(fromHz, toHz, durationMs, sampleRate_);
  }

  // The editor asks for a sweep across a trace tab's band range when the page
  // is selected. The sweep is one smooth log glide from the page's low edge to
  // its high edge; the audio thread renders it through mixTone while the host
  // is processing. When the transport is stopped the host never calls
  // process(), so offload to the asynchronous background worker that plays
  // Beeps across the band grid's edges and centers without blocking the UI
  // thread.
  void requestBandSweep(double fromHz, double toHz, int bandCount,
                        double durationMs) noexcept {
    if (!std::isfinite(fromHz) || !std::isfinite(toHz) || toHz <= 0.0 ||
        fromHz <= 0.0 || bandCount <= 0 || !std::isfinite(durationMs) ||
        durationMs <= 0.0) {
      return;
    }
    if (!processing_.load(std::memory_order_acquire) && traceTonesAllowed()) {
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
      if (toneWorker_) toneWorker_->requestSweep(fromHz, toHz, bandCount);
#endif
      return;
    }
    traceSweepFromHz_.store(fromHz, std::memory_order_relaxed);
    traceSweepToHz_.store(toHz, std::memory_order_relaxed);
    traceSweepDurationMs_.store(durationMs, std::memory_order_release);
    traceSweepRequested_.store(true, std::memory_order_release);
  }

  void requestWarningSweep() noexcept {
    pendingToneRequest_.store(1, std::memory_order_release);
  }

  void playConfidenceSweep(int oldLevel, int newLevel) noexcept {
    if (newLevel <= 0 || newLevel == oldLevel) return;
    // Coalesce: confidence can flip around a threshold while material varies.
    // Restarting the sweep on every flip produces a burst of cut-off chirps,
    // so let a sweep already sounding play out before firing the next one.
    if (tone_.active) return;
    if (newLevel > oldLevel) {
      if (newLevel == 1) {
        startSweep(180.0, 400.0, 200.0);
      } else if (newLevel == 2) {
        startSweep(220.0, 750.0, 230.0);
      } else {
        startSweep(350.0, 1200.0, 340.0);
      }
    } else if (oldLevel >= 3) {
      startSweep(1200.0, 350.0, 340.0);
    } else if (oldLevel == 2) {
      startSweep(750.0, 220.0, 250.0);
    } else {
      startSweep(400.0, 180.0, 220.0);
    }
  }

  void setWorkflowStep(int step) noexcept {
    lastWorkflowStep_.store(step, std::memory_order_release);
    setValue(tonetrace::ParameterId::WorkflowAction,
             static_cast<double>(step), true);
  }

  [[nodiscard]] Status captureStatus(tonetrace::WorkflowPhase phase,
                                     int level,
                                     const CaptureBuffer& capture) const noexcept {
    if (phase == tonetrace::WorkflowPhase::CapturingReference) {
      if (level >= 3) return Status::ReferenceHighConfidence;
      if (level == 2) return Status::ReferenceMediumConfidence;
      if (level == 1) return Status::ReferenceLowConfidence;
      if (capture.validFrames == 0) return Status::CapturingReference;
      if (capture.updateCounter == 0) return Status::ReferenceAudioDetected;
      if (capture.validSeconds(sampleRate_) < 0.35) {
        return Status::ReferenceCollecting;
      }
      if (capture.stability > 0.18) return Status::ReferenceUnstable;
      return Status::ReferenceReady;
    }
    if (level >= 3) return Status::TargetHighConfidence;
    if (level == 2) return Status::TargetMediumConfidence;
    if (level == 1) return Status::TargetLowConfidence;
    if (capture.validFrames == 0) return Status::CapturingTarget;
    if (capture.updateCounter == 0) return Status::TargetAudioDetected;
    if (capture.validSeconds(sampleRate_) < 0.35) {
      return Status::TargetCollecting;
    }
    if (capture.stability > 0.18) return Status::TargetUnstable;
    return Status::TargetReady;
  }

  [[nodiscard]] bool executeCommand(int command) noexcept {
    if (command > 0) {
      setValue(tonetrace::ParameterId::LastCommand,
               static_cast<double>(command), true);
    }
    if (command != 0 && controlBusy_.load(std::memory_order_acquire)) {
      setStatus(Status::Analyzing);
      return false;
    }
    switch (command) {
      case 1:
        stopTones();
        captureBlocked_ = false;
        setupLockedNotice_ = false;
        importedReference_.reset();
        importedTarget_.reset();
        stagedReferenceForExport_.reset();
        reference_.reset();
        target_.reset();
        phase_.store(tonetrace::WorkflowPhase::CapturingReference,
                     std::memory_order_release);
        setStatus(Status::CapturingReference);
        setValue(tonetrace::ParameterId::Confidence, 0.0, true);
        setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);
        setValue(tonetrace::ParameterId::CaptureSeconds, 0.0, true);
        break;
      case 2:
        if (phase_.load(std::memory_order_acquire) !=
                tonetrace::WorkflowPhase::CapturingReference ||
            !reference_.readyForSave(sampleRate_)) {
          if (phase_.load(std::memory_order_acquire) !=
              tonetrace::WorkflowPhase::CapturingReference) {
            reference_.reset();
            target_.reset();
            phase_.store(tonetrace::WorkflowPhase::CapturingReference,
                         std::memory_order_release);
            setValue(tonetrace::ParameterId::Confidence, 0.0, true);
            setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);
            setValue(tonetrace::ParameterId::CaptureSeconds, 0.0, true);
          }
          captureBlocked_ = true;
          setWorkflowStep(1);
          startSweep(420.0, 180.0, 220.0);
          setStatus(Status::CannotSaveYet);
          break;
        }
        stopTones();
        captureBlocked_ = false;
        setupLockedNotice_ = false;
        importedTarget_.reset();
        target_.reset();
        phase_.store(tonetrace::WorkflowPhase::CapturingTarget,
                     std::memory_order_release);
        setStatus(Status::CapturingTarget);
        setValue(tonetrace::ParameterId::Confidence, 0.0, true);
        setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);
        setValue(tonetrace::ParameterId::CaptureSeconds, 0.0, true);
        break;
      case 3:
        // Low confidence remains the normal quality threshold, but a capture
        // that filled the entire accepted-audio buffer must be allowed to
        // continue. The public status for this condition is explicitly
        // "Capture full; continue workflow"; rejecting Correct Target here
        // would make that fallback impossible. Confidence remains truthful at
        // zero so the user can judge the result with appropriate caution.
        if (!tonetrace::targetCaptureCanCorrect(
                phase_.load(std::memory_order_acquire),
                target_.confidenceLevel, target_.overflowed,
                importedTarget_.has_value())) {
          captureBlocked_ = true;
          setWorkflowStep(2);
          startSweep(420.0, 180.0, 220.0);
          setStatus(Status::InvalidCapture);
          break;
        }
        stopTones();
        captureBlocked_ = false;
        phase_.store(tonetrace::WorkflowPhase::Preview,
                     std::memory_order_release);
        setStatus(Status::Analyzing);
        controlBusy_.store(true, std::memory_order_release);
        requestMainThread(WorkAnalyze);
        break;
      case 4:
        if (hasProfile_.load(std::memory_order_acquire)) {
          phase_.store(tonetrace::WorkflowPhase::Frozen,
                       std::memory_order_release);
          setStatus(Status::Frozen);
          stopTones();
        } else {
          setWorkflowStep(3);
          setStatus(Status::InvalidCapture);
        }
        break;
      case 5:
        phaseBeforeReset_.store(phase_.load(std::memory_order_acquire),
                                std::memory_order_release);
        phase_.store(tonetrace::WorkflowPhase::Ready,
                     std::memory_order_release);
        setStatus(Status::ResetArmed);
        resetArmed_.store(true, std::memory_order_release);
        break;
      case 6:
        if (resetArmed_.exchange(false, std::memory_order_acq_rel)) {
          stopTones();
          setStatus(Status::Analyzing);
          controlBusy_.store(true, std::memory_order_release);
          requestMainThread(WorkReset);
        }
        break;
      case 7:
        if (resetArmed_.exchange(false, std::memory_order_acq_rel)) {
          const auto previous = phaseBeforeReset_.load(std::memory_order_acquire);
          phase_.store(previous, std::memory_order_release);
          setStatus(previous == tonetrace::WorkflowPhase::Frozen
                        ? Status::Frozen
                        : (hasProfile_.load(std::memory_order_acquire)
                               ? Status::Preview
                               : Status::Ready));
        }
        break;
      default:
        break;
    }
    return true;
  }

  void consumePendingWorkflowCommand() noexcept {
    const int command = pendingWorkflowCommand_.exchange(
        0, std::memory_order_acq_rel);
    if (command == 0) return;
    if (!executeCommand(command)) {
      int empty = 0;
      (void)pendingWorkflowCommand_.compare_exchange_strong(
          empty, command, std::memory_order_release,
          std::memory_order_relaxed);
    }
  }

  // The editor routes every workflow button through editorSetParameter on the
  // UI thread. While the transport runs, processAudio consumes the queued
  // command within one block. When it is stopped the host never calls process()
  // and does not echo editor-initiated changes back through param_flush, so the
  // workflow would stall. Consume it here instead; the tone state is only
  // touched on this thread while the audio thread is idle.
  void consumeWorkflowCommandWhenIdle() noexcept {
    if (processing_.load(std::memory_order_acquire)) return;
    consumePendingWorkflowCommand();
  }

  bool applyParameter(clap_id id, double requested) noexcept {
    const auto& descriptors = tonetrace::parameterDescriptors();
    const std::size_t index = parameterIndex(id);
    if (index >= descriptors.size()) return false;
    if (descriptors[index].readOnly) {
      // A read-only value cannot be written; reflect the authoritative value
      // back to the host.
      markDirty(index);
      return false;
    }
    const auto parameterId = descriptors[index].id;
    const double previousValue = values_[index]->load(std::memory_order_acquire);
    const double newValue = clampedValue(descriptors[index], requested);
    const auto currentPhase = phase_.load(std::memory_order_acquire);
    if (parameterId == tonetrace::ParameterId::MatchMode &&
        (currentPhase == tonetrace::WorkflowPhase::CapturingReference ||
         currentPhase == tonetrace::WorkflowPhase::CapturingTarget)) {
      markDirty(index);
      setupLockedNotice_ = true;
      setStatus(Status::SetupLocked);
      return false;
    }
    values_[index]->store(newValue, std::memory_order_release);
    if (parameterId == tonetrace::ParameterId::Bypass) {
      if (core_ != nullptr) core_->setBypassed(newValue >= 0.5);
    } else if (parameterId == tonetrace::ParameterId::WorkflowAction) {
      // The workflow step stays where the user put it; it never auto-resets, so
      // a screen-reader user does not have to cross earlier/destructive commands
      // to reach the next phase. Commands fire only when the step changes.
      const int step = static_cast<int>(newValue);
      const int previous = lastWorkflowStep_.load(std::memory_order_acquire);
      if (step != previous) {
        lastWorkflowStep_.store(step, std::memory_order_release);
        if (step != 0) {
          pendingWorkflowCommand_.store(step, std::memory_order_release);
        }
      }
    } else if (parameterId == tonetrace::ParameterId::MatchMode) {
      if (hasProfile_.load(std::memory_order_acquire)) {
        if (newValue != previousValue) {
          // Match Mode is a post-capture interpretation control. The retained
          // Reference/Target spectra are rematched on the main thread; no audio
          // recapture is required and switching back reproduces the prior mode.
          setStatus(Status::Analyzing);
          requestMainThread(WorkRebuild);
        } else {
          setStatus(currentPhase == tonetrace::WorkflowPhase::Frozen
                        ? Status::Frozen
                        : Status::Preview);
        }
      }
    } else if (parameterId == tonetrace::ParameterId::ToneNotifications ||
               parameterId == tonetrace::ParameterId::ToneLevelDb ||
               parameterId == tonetrace::ParameterId::EmergencyClipGuardDb) {
      // Tones and the last-resort guard act directly and never rebuild the
      // learned correction.
    } else if (hasProfile_.load(std::memory_order_acquire)) {
      requestMainThread(WorkRebuild);
    }
    return true;
  }

  bool applyEvents(const clap_input_events_t* events,
                   uint32_t first,
                   uint32_t last) noexcept {
    if (events == nullptr || events->size == nullptr || events->get == nullptr) {
      return false;
    }
    const uint32_t count = events->size(events);
    last = std::min(last, count);
    bool allApplied = true;
    for (uint32_t index = first; index < last; ++index) {
      const clap_event_header_t* header = events->get(events, index);
      if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID ||
          header->type != CLAP_EVENT_PARAM_VALUE ||
          header->size < sizeof(clap_event_param_value_t)) {
        continue;
      }
      const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
      if (!applyParameter(event->param_id, event->value)) {
        allApplied = false;
      }
    }
    return allApplied;
  }

  bool pushParameterValue(const clap_output_events_t* output,
                          uint32_t time,
                          std::size_t index) const noexcept {
    const auto& descriptors = tonetrace::parameterDescriptors();
    if (output == nullptr || output->try_push == nullptr ||
        index >= descriptors.size()) {
      return false;
    }
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = static_cast<clap_id>(descriptors[index].id);
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = values_[index]->load(std::memory_order_acquire);
    return output->try_push(output, &event.header);
  }

  void pushDirtyValues(const clap_output_events_t* output,
                       uint32_t time) noexcept {
    const auto count = tonetrace::parameterDescriptors().size();
    for (std::size_t index = 0; index < count; ++index) {
      if (dirtyValues_[index]->exchange(false, std::memory_order_acq_rel) &&
          !pushParameterValue(output, time, index)) {
        dirtyValues_[index]->store(true, std::memory_order_release);
      }
    }
  }

  void captureSlice(const float* const* input,
                    std::size_t channels,
                    std::size_t count) noexcept {
    const auto phase = phase_.load(std::memory_order_acquire);
    CaptureBuffer* destination = nullptr;
    if (phase == tonetrace::WorkflowPhase::CapturingReference) {
      destination = &reference_;
    } else if (phase == tonetrace::WorkflowPhase::CapturingTarget) {
      destination = &target_;
    }
    if (destination == nullptr) return;
    const int oldConfidence = destination->confidenceLevel;
    destination->append(input, channels, count, sampleRate_,
                        matchMode(value(tonetrace::ParameterId::MatchMode)));
    const int newConfidence = destination->confidenceLevel;
    setValue(tonetrace::ParameterId::CaptureSeconds,
             destination->validSeconds(sampleRate_), true);
    setValue(tonetrace::ParameterId::Confidence,
             static_cast<double>(newConfidence) / 3.0, true);
    setValue(tonetrace::ParameterId::CurveDriftDb,
             std::clamp(destination->stability * 60.0, 0.0, 60.0), true);
    if (newConfidence != oldConfidence) {
      playConfidenceSweep(oldConfidence, newConfidence);
    }
    const bool nowReady = phase == tonetrace::WorkflowPhase::CapturingReference
                              ? destination->readyForSave(sampleRate_)
                              : destination->confidenceLevel >= 1;
    if (nowReady) captureBlocked_ = false;
    setStatus(setupLockedNotice_
                  ? Status::SetupLocked
                  : (captureBlocked_
                         ? (phase == tonetrace::WorkflowPhase::CapturingReference
                                ? Status::CannotSaveYet
                                : Status::InvalidCapture)
                                     : captureStatus(phase, newConfidence,
                                                     *destination)));
    if (destination->overflowed) setStatus(Status::CaptureFull);
  }

  void mixTone(float* const* output,
               std::size_t channels,
               std::size_t count) noexcept {
    if (stopToneRequested_.exchange(false, std::memory_order_acq_rel)) {
      stopTones();
    }
    if (traceToneRequested_.exchange(false, std::memory_order_acq_rel)) {
      const double frequency =
          traceToneFrequency_.load(std::memory_order_acquire);
      if (traceTonesAllowed() && std::isfinite(frequency) && frequency > 0.0) {
        tone_.startBandReference(frequency, frequency, 125.0, sampleRate_);
      }
    }
    if (traceSweepRequested_.exchange(false, std::memory_order_acq_rel)) {
      const double fromHz =
          traceSweepFromHz_.load(std::memory_order_acquire);
      const double toHz = traceSweepToHz_.load(std::memory_order_acquire);
      const double durationMs =
          traceSweepDurationMs_.load(std::memory_order_acquire);
      if (traceTonesAllowed() && std::isfinite(fromHz) &&
          std::isfinite(toHz) && fromHz > 0.0 && toHz > 0.0 &&
          std::isfinite(durationMs) && durationMs > 0.0) {
        tone_.startBandReference(fromHz, toHz, durationMs, sampleRate_);
      }
    }
    if (pendingToneRequest_.exchange(0, std::memory_order_acq_rel) == 1) {
      startSweep(420.0, 180.0, 220.0);
    }
    if (!tone_.active) return;
    // Band references stay audible while Frozen; capture/confidence tones do
    // not. Both are muted during an offline render via the *_allowed() gates.
    if (tone_.bandReference ? !traceTonesAllowed() : !tonesAllowed()) {
      stopTones();
      return;
    }
    const double gain = std::pow(
        10.0, value(tonetrace::ParameterId::ToneLevelDb) / 20.0);
    const std::uint64_t attackFrames = static_cast<std::uint64_t>(
        std::max(1.0, 0.020 * static_cast<double>(sampleRate_)));
    constexpr double twoPi = 6.283185307179586476925286766559;
    for (std::size_t frame = 0; frame < count; ++frame) {
      if (tone_.samplesLeft == 0) {
        stopTones();
        break;
      }
      const std::uint64_t elapsed = tone_.samplesTotal - tone_.samplesLeft;
      const double frequency = tone_.currentFrequency();
      const double attack = std::min(
          1.0, static_cast<double>(elapsed) / attackFrames);
      const double release = std::min(
          1.0, static_cast<double>(tone_.samplesLeft) / attackFrames);
      const float sample = static_cast<float>(
          std::sin(tone_.phase) * gain * std::min(attack, release));
      for (std::size_t channel = 0; channel < channels; ++channel) {
        output[channel][frame] += sample;
      }
      tone_.phase += twoPi * frequency / sampleRate_;
      if (tone_.phase >= twoPi) tone_.phase = std::fmod(tone_.phase, twoPi);
      --tone_.samplesLeft;
    }
  }

  void applyEmergencyClipGuard(float* const* output,
                               std::size_t channels,
                               std::size_t count) noexcept {
    if (value(tonetrace::ParameterId::Bypass) >= 0.5) return;
    const float limit = static_cast<float>(std::pow(
        10.0, value(tonetrace::ParameterId::EmergencyClipGuardDb) / 20.0));
    for (std::size_t channel = 0; channel < channels; ++channel) {
      for (std::size_t frame = 0; frame < count; ++frame) {
        output[channel][frame] = std::clamp(
            output[channel][frame], -limit, limit);
      }
    }
  }

  bool prepareWorkingAudio(const clap_process_t* process,
                           const float* (&input)[2],
                           float* (&output)[2],
                           std::size_t& channels,
                           bool& doublePrecision) noexcept {
    if (process == nullptr || process->audio_inputs_count != 1 ||
        process->audio_outputs_count != 1 || process->audio_inputs == nullptr ||
        process->audio_outputs == nullptr || process->frames_count > maxFrames_) {
      return false;
    }
    const auto& in = process->audio_inputs[0];
    auto& out = process->audio_outputs[0];
    channels = in.channel_count;
    if (channels == 0 || channels > 2 || out.channel_count != channels) return false;
    doublePrecision = in.data64 != nullptr && out.data64 != nullptr;
    if (doublePrecision) {
      for (std::size_t channel = 0; channel < channels; ++channel) {
        if (in.data64[channel] == nullptr || out.data64[channel] == nullptr) return false;
        for (uint32_t frame = 0; frame < process->frames_count; ++frame) {
          scratchInput_[channel][frame] = static_cast<float>(in.data64[channel][frame]);
        }
        input[channel] = scratchInput_[channel].data();
        output[channel] = scratchOutput_[channel].data();
      }
      return true;
    }
    if (in.data32 == nullptr || out.data32 == nullptr) return false;
    for (std::size_t channel = 0; channel < channels; ++channel) {
      if (in.data32[channel] == nullptr || out.data32[channel] == nullptr) return false;
      input[channel] = in.data32[channel];
      output[channel] = out.data32[channel];
    }
    return true;
  }

  void processSlice(const float* const input[2],
                    float* const output[2],
                    std::size_t channels,
                    uint32_t offset,
                    uint32_t count) noexcept {
    if (count == 0 || core_ == nullptr) return;
    const float* slicedInput[2]{};
    float* slicedOutput[2]{};
    for (std::size_t channel = 0; channel < channels; ++channel) {
      slicedInput[channel] = input[channel] + offset;
      slicedOutput[channel] = output[channel] + offset;
    }
    captureSlice(slicedInput, channels, count);
    core_->process(slicedInput, slicedOutput, channels, count);
    // A completed kernel crossfade leaves its old engine for the control
    // thread to reclaim. Schedule exactly one coalesced main-thread cleanup
    // once that retirement exists; this also lets an already queued latest
    // correction begin on the next audio block without waiting for some
    // unrelated future UI action.
    if (core_->hasRetiredKernel()) requestMainThread(WorkCleanup);
    mixTone(slicedOutput, channels, count);
    applyEmergencyClipGuard(slicedOutput, channels, count);
  }

  clap_process_status processAudio(const clap_process_t* process) noexcept {
    const float* input[2]{};
    float* output[2]{};
    std::size_t channels = 0;
    bool doublePrecision = false;
    if (!prepareWorkingAudio(process, input, output, channels, doublePrecision)) {
      return CLAP_PROCESS_ERROR;
    }

    consumePendingWorkflowCommand();

    uint32_t cursor = 0;
    uint32_t eventIndex = 0;
    const uint32_t eventCount =
        process->in_events != nullptr && process->in_events->size != nullptr
            ? process->in_events->size(process->in_events)
            : 0;
    while (eventIndex < eventCount) {
      const clap_event_header_t* header =
          process->in_events->get(process->in_events, eventIndex);
      const uint32_t eventTime = header == nullptr
                                     ? cursor
                                     : std::min(header->time, process->frames_count);
      if (eventTime > cursor) {
        processSlice(input, output, channels, cursor, eventTime - cursor);
        cursor = eventTime;
      }
      const uint32_t groupBegin = eventIndex;
      while (eventIndex < eventCount) {
        const clap_event_header_t* grouped =
            process->in_events->get(process->in_events, eventIndex);
        if (grouped == nullptr ||
            std::min(grouped->time, process->frames_count) != eventTime) {
          break;
        }
        ++eventIndex;
      }
      (void)applyEvents(process->in_events, groupBegin, eventIndex);
      consumePendingWorkflowCommand();
    }
    if (cursor < process->frames_count) {
      processSlice(input, output, channels, cursor, process->frames_count - cursor);
    }
    pushDirtyValues(process->out_events,
                    process->frames_count == 0 ? 0 : process->frames_count - 1);

    if (doublePrecision) {
      auto& out = process->audio_outputs[0];
      for (std::size_t channel = 0; channel < channels; ++channel) {
        for (uint32_t frame = 0; frame < process->frames_count; ++frame) {
          out.data64[channel][frame] = output[channel][frame];
        }
      }
    }
    process->audio_outputs[0].constant_mask = 0;
    return CLAP_PROCESS_CONTINUE;
  }

  void analyzeCapturedAudio() {
    const auto matchUncapped = currentMatchSettings(true);
    tonetrace::MatchEngine engine;
    const auto referenceCapture =
        importedReference_ ? *importedReference_
                           : engine.capture(reference_.audio(sampleRate_),
                                            matchUncapped);
    const auto targetCapture =
        importedTarget_
            ? *importedTarget_
            : engine.capture(target_.audio(sampleRate_), matchUncapped);
    auto model = engine.match(referenceCapture, targetCapture, matchUncapped);

    tonetrace::ProfileSnapshot candidate;
    candidate.reference = referenceCapture;
    candidate.target = targetCapture;
    candidate.uncappedModel = std::move(model);
    candidate.matchSettings = currentMatchSettings(false);
    candidate.renderSettings = currentRenderSettings();
    candidate.referenceDiagnostics = reference_.profileDiagnostics();
    candidate.targetDiagnostics = target_.profileDiagnostics();
    const auto validation = core_->commitCandidate(candidate);
    if (!validation.accepted) {
      setStatus(validation.issue == tonetrace::ProfileIssue::RendererBusy
                    ? Status::RendererBusy
                    : Status::InvalidCapture);
      if (validation.issue != tonetrace::ProfileIssue::RendererBusy) {
        requestWarningSweep();
      }
      return;
    }
    hasProfile_.store(true, std::memory_order_release);
    tailFrames_.store(static_cast<uint32_t>(std::clamp(
                          candidate.renderSettings.durationSeconds * sampleRate_,
                          0.0,
                          static_cast<double>(
                              std::numeric_limits<uint32_t>::max()))),
                      std::memory_order_release);
    phase_.store(tonetrace::WorkflowPhase::Preview,
                 std::memory_order_release);
    setValue(tonetrace::ParameterId::Confidence,
             std::min(referenceCapture.confidence, targetCapture.confidence), true);
    setValue(tonetrace::ParameterId::CurveDriftDb, 0.0, true);
    setStatus(Status::Preview);
  }

  void rebuildCorrection() {
    if (core_ == nullptr || core_->frozenSnapshot() == nullptr) return;
    auto candidate = *core_->frozenSnapshot();

    // Reference/Target analysis is intentionally retained at high internal
    // resolution. Post-match controls do not need to run MatchEngine again;
    // they only change how the stored learned model is rendered. Resolution
    // is metadata for the editable manual grid, so keep the model/snapshot
    // contract aligned without recomputing its spectral nodes.
    const auto requestedMatch = currentMatchSettings(false);
    const auto requestedUncapped = currentMatchSettings(true);
    if (candidate.uncappedModel.mode != requestedUncapped.mode) {
      // Reinterpret the retained capture spectra under the newly selected mode.
      // Capture itself is intentionally not repeated. This preserves the exact
      // original capture while allowing users to compare mode-specific smoothing,
      // point density and Voice safety behaviour after Correct/Freeze.
      tonetrace::MatchEngine engine;
      candidate.uncappedModel = engine.match(
          candidate.reference, candidate.target, requestedUncapped);
    }
    candidate.matchSettings.mode = requestedMatch.mode;
    candidate.matchSettings.maximumCorrectionDb =
        requestedMatch.maximumCorrectionDb;
    candidate.matchSettings.resolution = requestedMatch.resolution;
    candidate.uncappedModel.resolution = requestedMatch.resolution;
    candidate.renderSettings = currentRenderSettings();
    const auto validation = core_->commitCandidate(candidate);
    if (!validation.accepted) {
      setStatus(validation.issue == tonetrace::ProfileIssue::RendererBusy
                    ? Status::RendererBusy
                    : Status::AnalysisFailed);
      if (validation.issue != tonetrace::ProfileIssue::RendererBusy) {
        requestWarningSweep();
      }
      return;
    }
    tailFrames_.store(static_cast<uint32_t>(std::clamp(
                          candidate.renderSettings.durationSeconds * sampleRate_,
                          0.0,
                          static_cast<double>(
                              std::numeric_limits<uint32_t>::max()))),
                      std::memory_order_release);
    if (phase_.load(std::memory_order_acquire) ==
        tonetrace::WorkflowPhase::Frozen) {
      setStatus(setupMatchesFrozenProfile() ? Status::Frozen
                                            : Status::SetupChanged);
    } else {
      setStatus(Status::Preview);
    }
  }

  // Synthesizes a flat 0 dB spectrum at the model's node frequencies; the
  // matching target capture mirrors the model gains (0 - gain) so the match
  // engine reproduces the imported correction exactly.
  tonetrace::SpectrumCapture synthesizedReference(
      const tonetrace::CorrectionModel& model) const {
    tonetrace::SpectrumCapture capture;
    capture.sampleRate = sampleRate_;
    capture.fftSize = 1;
    capture.acceptedFrames = 3;
    capture.confidence = 1.0;
    capture.points.reserve(model.nodes.size());
    for (const auto& node : model.nodes) {
      tonetrace::SpectrumPoint point;
      point.frequencyHz = node.frequencyHz;
      point.levelDb = 0.0;
      point.confidence = 1.0;
      point.varianceDb2 = 0.0;
      capture.points.push_back(point);
    }
    return capture;
  }

  tonetrace::SpectrumCapture synthesizedTarget(
      const tonetrace::CorrectionModel& model) const {
    tonetrace::SpectrumCapture capture;
    capture.sampleRate = sampleRate_;
    capture.fftSize = 1;
    capture.acceptedFrames = 3;
    capture.confidence = 1.0;
    capture.points.reserve(model.nodes.size());
    for (const auto& node : model.nodes) {
      tonetrace::SpectrumPoint point;
      point.frequencyHz = node.frequencyHz;
      point.levelDb = -node.gainDb;
      point.confidence = 1.0;
      point.varianceDb2 = 0.0;
      capture.points.push_back(point);
    }
    return capture;
  }

  void applyImportedReference(const tonetrace::SpectrumCapture& capture) {
    stopTones();
    captureBlocked_ = false;
    setupLockedNotice_ = false;
    importedReference_ = capture;
    importedTarget_.reset();
    stagedReferenceForExport_.reset();
    reference_.reset();
    target_.reset();
    phase_.store(tonetrace::WorkflowPhase::CapturingTarget,
                 std::memory_order_release);
    setValue(tonetrace::ParameterId::Confidence, 0.0, true);
    setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);
    setValue(tonetrace::ParameterId::CaptureSeconds, 0.0, true);
    setStatus(Status::ImportedReference);
    setWorkflowStep(2);
  }

  void applyImportedTarget(const tonetrace::SpectrumCapture& capture) {
    const auto phase = phase_.load(std::memory_order_acquire);
    const bool hasReference =
        importedReference_.has_value() ||
        phase == tonetrace::WorkflowPhase::CapturingTarget ||
        (phase == tonetrace::WorkflowPhase::CapturingReference &&
         reference_.readyForSave(sampleRate_));
    if (!hasReference) {
      setStatus(Status::InvalidCapture);
      requestWarningSweep();
      return;
    }
    stopTones();
    captureBlocked_ = false;
    setupLockedNotice_ = false;
    importedTarget_ = capture;
    phase_.store(tonetrace::WorkflowPhase::Preview,
                 std::memory_order_release);
    setStatus(Status::ImportedTarget);
    controlBusy_.store(true, std::memory_order_release);
    setWorkflowStep(3);
    requestMainThread(WorkAnalyze);
  }

  void applyImportedModel(const tonetrace::CorrectionModel& model) {
    const auto referenceCapture = synthesizedReference(model);
    const auto targetCapture = synthesizedTarget(model);

    tonetrace::ProfileSnapshot candidate;
    candidate.reference = referenceCapture;
    candidate.target = targetCapture;
    candidate.uncappedModel = model;
    candidate.matchSettings = currentMatchSettings(false);
    candidate.matchSettings.mode = model.mode;
    candidate.matchSettings.resolution = model.resolution;
    candidate.matchSettings.rangeLowHz = model.analysisLowHz;
    candidate.matchSettings.rangeHighHz = model.analysisHighHz;
    candidate.renderSettings = currentRenderSettings();
    candidate.renderSettings.rangeLowHz = model.analysisLowHz;
    candidate.renderSettings.rangeHighHz = model.analysisHighHz;
    // A .ttm contains the learned model, not manual edits from whatever
    // profile happened to be open before the import. Start its editable grid
    // flat and at the model's own resolution; global render controls such as
    // Strength/Q/Gain still carry over as documented.
    candidate.renderSettings.manualGains.assign(
        static_cast<std::size_t>(std::max(1, model.resolution)), 0.0);
    candidate.referenceDiagnostics = tonetrace::CaptureDiagnostics{};
    candidate.targetDiagnostics = tonetrace::CaptureDiagnostics{};

    const auto validation = core_->commitCandidate(candidate);
    if (!validation.accepted) {
      setStatus(validation.issue == tonetrace::ProfileIssue::RendererBusy
                    ? Status::RendererBusy
                    : Status::InvalidCapture);
      if (validation.issue != tonetrace::ProfileIssue::RendererBusy) {
        requestWarningSweep();
      }
      return;
    }
    manualGains_ = candidate.renderSettings.manualGains;
    hasProfile_.store(true, std::memory_order_release);
    tailFrames_.store(static_cast<uint32_t>(std::clamp(
                          candidate.renderSettings.durationSeconds * sampleRate_,
                          0.0,
                          static_cast<double>(
                              std::numeric_limits<uint32_t>::max()))),
                      std::memory_order_release);
    importedReference_ = referenceCapture;
    importedTarget_ = targetCapture;
    stagedReferenceForExport_.reset();
    phase_.store(tonetrace::WorkflowPhase::Frozen,
                 std::memory_order_release);
    setValue(tonetrace::ParameterId::MatchMode,
             static_cast<double>(model.mode), true);
    setValue(tonetrace::ParameterId::Resolution,
             static_cast<double>(model.resolution), true);
    setValue(tonetrace::ParameterId::RangeLowHz, model.analysisLowHz, true);
    setValue(tonetrace::ParameterId::RangeHighHz, model.analysisHighHz, true);
    setValue(tonetrace::ParameterId::Confidence, 1.0, true);
    setValue(tonetrace::ParameterId::CurveDriftDb, 0.0, true);
    setStatus(Status::Frozen);
    setWorkflowStep(4);
  }

  void applyPendingImport() {
    std::unique_lock<std::mutex> lock(importMutex_);
    const int kind = pendingImportKind_.exchange(0, std::memory_order_acq_rel);
    if (kind == 0) return;
    bool reference = false;
    bool target = false;
    bool model = false;
    tonetrace::SpectrumCapture referenceCapture;
    tonetrace::SpectrumCapture targetCapture;
    tonetrace::CorrectionModel correctionModel;
    if (kind == 1) {
      reference = true;
      referenceCapture = std::move(pendingImportedReference_);
    } else if (kind == 2) {
      target = true;
      targetCapture = std::move(pendingImportedTarget_);
    } else if (kind == 3) {
      model = true;
      correctionModel = std::move(pendingImportedModel_);
    }
    lock.unlock();
    try {
      if (reference) {
        tonetrace::validateSpectrumCapture(referenceCapture);
        applyImportedReference(referenceCapture);
      } else if (target) {
        tonetrace::validateSpectrumCapture(targetCapture);
        applyImportedTarget(targetCapture);
      } else if (model) {
        tonetrace::validateCorrectionModel(correctionModel);
        applyImportedModel(correctionModel);
      }
    } catch (const std::exception&) {
      setStatus(Status::InvalidCapture);
      requestWarningSweep();
    }
  }

  void performMainThreadWork() noexcept {
    const std::uint32_t work = pendingWork_.exchange(WorkNone,
                                                     std::memory_order_acq_rel);
    try {
      if ((work & WorkReset) != 0U) {
        if (core_ != nullptr) core_->clearProfile();
        hasProfile_.store(false, std::memory_order_release);
        tailFrames_.store(0, std::memory_order_release);
        importedReference_.reset();
        importedTarget_.reset();
        stagedReferenceForExport_.reset();
        reference_.reset();
        target_.reset();
        phase_.store(tonetrace::WorkflowPhase::Ready,
                     std::memory_order_release);
        setValue(tonetrace::ParameterId::Confidence, 0.0, true);
        setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);
        setValue(tonetrace::ParameterId::CaptureSeconds, 0.0, true);
        setStatus(Status::Ready);
      }
      if ((work & WorkAnalyze) != 0U) {
        analyzeCapturedAudio();
      } else if ((work & WorkRebuild) != 0U) {
        rebuildCorrection();
      }
      if ((work & WorkImport) != 0U) {
        applyPendingImport();
      }
      if (core_ != nullptr) core_->collectRetiredKernels();
    } catch (const std::exception&) {
      setStatus(Status::AnalysisFailed);
      requestWarningSweep();
    }
    if ((work & (WorkAnalyze | WorkReset)) != 0U) {
      controlBusy_.store(false, std::memory_order_release);
    }
    if ((work & (WorkAnalyze | WorkRebuild | WorkReset | WorkImport)) != 0U) {
      notifyParameterValuesChanged();
    }
  }

  std::string coreState() const {
    if (core_ != nullptr) return core_->saveProjectState();
    return pendingCoreState_.empty()
               ? tonetrace::serializeProjectState(nullptr)
               : pendingCoreState_;
  }

  std::string saveWrapperState() const {
    std::ostringstream stream;
    stream.precision(std::numeric_limits<double>::max_digits10);
    stream << "ToneTraceClapState 2\n";
    const auto& descriptors = tonetrace::parameterDescriptors();
    std::size_t persisted = 0;
    for (const auto& descriptor : descriptors) {
      if (!descriptor.readOnly &&
          descriptor.id != tonetrace::ParameterId::WorkflowAction) {
        ++persisted;
      }
    }
    stream << "parameters " << persisted << '\n';
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
      if (descriptors[i].readOnly ||
          descriptors[i].id == tonetrace::ParameterId::WorkflowAction) {
        continue;
      }
      stream << "parameter " << static_cast<std::uint32_t>(descriptors[i].id)
             << ' ' << values_[i]->load(std::memory_order_acquire) << '\n';
    }
    const std::string state = coreState();
    stream << "tracedisplay " << (displayStatic_ ? 1 : 0) << '\n';
    stream << "core " << state.size() << '\n';
    stream.write(state.data(), static_cast<std::streamsize>(state.size()));
    return stream.str();
  }

  bool loadWrapperState(const std::string& bytes) {
    if (active_.load(std::memory_order_acquire) || bytes.size() > kMaximumStateBytes) {
      return false;
    }
    std::istringstream stream(bytes);
    std::string token;
    int version = 0;
    stream >> token >> version;
    if (!stream || token != "ToneTraceClapState" ||
        (version != 1 && version != 2)) {
      return false;
    }
    std::size_t count = 0;
    stream >> token >> count;
    if (!stream || token != "parameters" || count > values_.size()) return false;
    std::vector<std::pair<std::size_t, double>> restoredValues;
    restoredValues.reserve(count);
    const auto& descriptors = tonetrace::parameterDescriptors();
    for (std::size_t item = 0; item < count; ++item) {
      std::uint32_t id = 0;
      double restored = 0.0;
      stream >> token >> id >> restored;
      const std::size_t index = parameterIndex(static_cast<clap_id>(id));
      if (!stream || token != "parameter" || index >= descriptors.size() ||
          descriptors[index].readOnly ||
          descriptors[index].id == tonetrace::ParameterId::WorkflowAction ||
          !std::isfinite(restored)) {
        return false;
      }
      restoredValues.emplace_back(index,
          clampedValue(descriptors[index], restored));
    }
    bool restoredDisplayStatic = false;
    if (version >= 2) {
      int displayValue = 0;
      stream >> token >> displayValue;
      if (!stream || token != "tracedisplay" ||
          (displayValue != 0 && displayValue != 1)) {
        return false;
      }
      restoredDisplayStatic = displayValue != 0;
    }
    std::size_t coreBytes = 0;
    stream >> token >> coreBytes;
    if (!stream || token != "core" || coreBytes > kMaximumStateBytes) return false;
    if (stream.peek() == '\r') stream.get();
    if (stream.peek() == '\n') stream.get();
    std::string restoredCore(coreBytes, '\0');
    stream.read(restoredCore.data(), static_cast<std::streamsize>(coreBytes));
    if (stream.gcount() != static_cast<std::streamsize>(coreBytes)) return false;
    stream >> std::ws;
    if (!stream.eof()) return false;
    // Validate the nested state before mutating any live parameter.
    const auto restoredProject =
        tonetrace::deserializeProjectState(restoredCore);
    for (const auto& [index, restored] : restoredValues) {
      values_[index]->store(restored, std::memory_order_release);
    }
    displayStatic_ = restoredDisplayStatic;
    setValue(tonetrace::ParameterId::WorkflowAction, 0.0);
    lastWorkflowStep_.store(0, std::memory_order_release);
    if (restoredProject.phase == tonetrace::WorkflowPhase::Frozen &&
        restoredProject.snapshot) {
      setValue(tonetrace::ParameterId::Confidence,
               std::min(restoredProject.snapshot->reference.confidence,
                        restoredProject.snapshot->target.confidence), true);
      setValue(tonetrace::ParameterId::CurveDriftDb, 0.0, true);
      const bool setupMatches =
          restoredProject.snapshot->matchSettings.mode ==
              matchMode(value(tonetrace::ParameterId::MatchMode));
      setStatus(setupMatches ? Status::Frozen : Status::SetupChanged);
    } else {
      setValue(tonetrace::ParameterId::Confidence, 0.0, true);
      setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);
      setStatus(Status::Ready);
    }
    pendingCoreState_ = std::move(restoredCore);
    notifyParameterValuesChanged();
    return true;
  }

  static bool writeAll(const clap_ostream_t* stream,
                       const std::string& bytes) noexcept {
    if (stream == nullptr || stream->write == nullptr) return false;
    std::size_t written = 0;
    while (written < bytes.size()) {
      const int64_t result = stream->write(
          stream, bytes.data() + written, bytes.size() - written);
      if (result <= 0 || static_cast<std::uint64_t>(result) > bytes.size() - written) {
        return false;
      }
      written += static_cast<std::size_t>(result);
    }
    return true;
  }

  static bool readAll(const clap_istream_t* stream,
                      std::string& bytes) noexcept {
    if (stream == nullptr || stream->read == nullptr) return false;
    std::array<char, 4096> block{};
    bytes.clear();
    for (;;) {
      const int64_t result = stream->read(stream, block.data(), block.size());
      if (result < 0 || static_cast<std::uint64_t>(result) > block.size()) return false;
      if (result == 0) return true;
      if (bytes.size() + static_cast<std::size_t>(result) > kMaximumStateBytes) {
        return false;
      }
      bytes.append(block.data(), static_cast<std::size_t>(result));
    }
  }

  static bool CLAP_ABI pluginInit(const clap_plugin_t* plugin) noexcept {
    return self(plugin) != nullptr;
  }

  static void CLAP_ABI pluginDestroy(const clap_plugin_t* plugin) noexcept {
    delete self(plugin);
  }

  static bool CLAP_ABI pluginActivate(const clap_plugin_t* plugin,
                                      double sampleRate,
                                      uint32_t,
                                      uint32_t maxFrames) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || !std::isfinite(sampleRate) || sampleRate < 8000.0 ||
        sampleRate > 768000.0 || maxFrames == 0 || maxFrames > (1U << 20U)) {
      return false;
    }
    try {
      instance->sampleRate_ = static_cast<int>(std::lround(sampleRate));
      instance->maxFrames_ = maxFrames;
      instance->stopTones();
      instance->pendingWorkflowCommand_.store(0, std::memory_order_release);
      instance->setWorkflowStep(0);
      instance->reference_.prepare(instance->sampleRate_);
      instance->target_.prepare(instance->sampleRate_);
      for (auto& channel : instance->scratchInput_) channel.assign(maxFrames, 0.0F);
      for (auto& channel : instance->scratchOutput_) channel.assign(maxFrames, 0.0F);
      tonetrace::RealtimeConvolverConfig config;
      config.sampleRate = instance->sampleRate_;
      config.channels = 2;
      instance->core_ = std::make_unique<tonetrace::HeadlessPluginCore>(config);
      const auto restored = tonetrace::deserializeProjectState(
          instance->pendingCoreState_.empty()
              ? tonetrace::serializeProjectState(nullptr)
              : instance->pendingCoreState_);
      if (restored.phase == tonetrace::WorkflowPhase::Frozen && restored.snapshot) {
        restored.snapshot->renderSettings.sampleRate = instance->sampleRate_;
        const auto validation = instance->core_->commitCandidate(*restored.snapshot);
        if (!validation.accepted) return false;
        instance->phase_.store(tonetrace::WorkflowPhase::Frozen,
                               std::memory_order_release);
        instance->hasProfile_.store(true, std::memory_order_release);
        instance->tailFrames_.store(static_cast<uint32_t>(std::clamp(
            restored.snapshot->renderSettings.durationSeconds *
                instance->sampleRate_,
            0.0,
            static_cast<double>(std::numeric_limits<uint32_t>::max()))),
            std::memory_order_release);
        instance->setValue(tonetrace::ParameterId::Confidence,
            std::min(restored.snapshot->reference.confidence,
                     restored.snapshot->target.confidence), true);
        instance->setValue(tonetrace::ParameterId::CurveDriftDb, 0.0, true);
        instance->setStatus(instance->setupMatchesFrozenProfile()
                                ? Status::Frozen
                                : Status::SetupChanged);
      } else {
        instance->phase_.store(tonetrace::WorkflowPhase::Ready,
                               std::memory_order_release);
        instance->hasProfile_.store(false, std::memory_order_release);
        instance->tailFrames_.store(0, std::memory_order_release);
        instance->setValue(tonetrace::ParameterId::Confidence, 0.0, true);
        instance->setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);
        instance->setStatus(Status::Ready);
      }
      instance->core_->setBypassed(
          instance->value(tonetrace::ParameterId::Bypass) >= 0.5);
      instance->syncManualGainsFromSnapshot();
      instance->controlBusy_.store(false, std::memory_order_release);
      instance->active_.store(true, std::memory_order_release);
      return true;
    } catch (const std::exception&) {
      instance->core_.reset();
      return false;
    }
  }

  static void CLAP_ABI pluginDeactivate(const clap_plugin_t* plugin) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr) return;
    try {
      if (instance->core_ != nullptr) {
        instance->pendingCoreState_ = instance->core_->saveProjectState();
      }
    } catch (const std::exception&) {
    }
    instance->processing_.store(false, std::memory_order_release);
    instance->active_.store(false, std::memory_order_release);
    instance->stopTones();
    instance->pendingWorkflowCommand_.store(0, std::memory_order_release);
    // A host may deliver a previously requested main-thread callback after
    // deactivation. Drop work tied to the old renderer/captures so that a
    // later activation cannot accidentally run a stale analyze/rebuild/reset.
    instance->pendingWork_.store(WorkNone, std::memory_order_release);
    instance->pendingImportKind_.store(0, std::memory_order_release);
    instance->controlBusy_.store(false, std::memory_order_release);
    instance->core_.reset();
    // Captures are rebuilt on every activation and are never part of project
    // state. Release their potentially large 30-second stereo allocations as
    // soon as the host deactivates the instance instead of retaining roughly
    // 23 MB at 48 kHz (and proportionally more at high sample rates).
    instance->reference_.release();
    instance->target_.release();
    for (auto& channel : instance->scratchInput_) {
      std::vector<float>().swap(channel);
    }
    for (auto& channel : instance->scratchOutput_) {
      std::vector<float>().swap(channel);
    }
    instance->maxFrames_ = 0;
  }

  static bool CLAP_ABI pluginStartProcessing(const clap_plugin_t* plugin) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || !instance->active_.load(std::memory_order_acquire)) {
      return false;
    }
    instance->processing_.store(true, std::memory_order_release);
    return true;
  }

  static void CLAP_ABI pluginStopProcessing(const clap_plugin_t* plugin) noexcept {
    if (auto* instance = self(plugin)) {
      instance->processing_.store(false, std::memory_order_release);
    }
  }

  static void CLAP_ABI pluginReset(const clap_plugin_t* plugin) noexcept {
    auto* instance = self(plugin);
    if (instance != nullptr && instance->core_ != nullptr) {
      instance->core_->resetAudio();
      instance->core_->setBypassed(
          instance->value(tonetrace::ParameterId::Bypass) >= 0.5);
    }
  }

  static clap_process_status CLAP_ABI pluginProcess(
      const clap_plugin_t* plugin,
      const clap_process_t* process) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->core_ == nullptr ||
        !instance->active_.load(std::memory_order_acquire) ||
        !instance->processing_.load(std::memory_order_acquire)) {
      return CLAP_PROCESS_ERROR;
    }
    return instance->processAudio(process);
  }

  static const void* CLAP_ABI pluginGetExtension(const clap_plugin_t*,
                                                 const char* id) noexcept;

  static void CLAP_ABI pluginOnMainThread(const clap_plugin_t* plugin) noexcept {
    if (auto* instance = self(plugin)) instance->performMainThreadWork();
  }

#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
  static const tonetrace::ProfileSnapshot* editorGetSnapshot(
      void* context) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    if (instance == nullptr || instance->core_ == nullptr) return nullptr;
    return instance->core_->frozenSnapshot();
  }

  const tonetrace::SpectrumCapture* stagedSpectrumForEditor(int which) noexcept {
    if (which == 1 && importedReference_.has_value()) {
      return &*importedReference_;
    }
    if (which == 2 && importedTarget_.has_value()) {
      return &*importedTarget_;
    }
    if (which != 1 ||
        phase_.load(std::memory_order_acquire) !=
            tonetrace::WorkflowPhase::CapturingTarget ||
        !reference_.readyForSave(sampleRate_)) {
      return nullptr;
    }
    if (stagedReferenceForExport_.has_value()) {
      return &*stagedReferenceForExport_;
    }

    // Learn Target commits the Reference by ending Reference capture. Build the
    // portable .tts spectrum lazily only if the user actually asks to export it.
    // This keeps the normal audio/workflow path bit-for-bit unchanged and avoids
    // running FFT analysis on the audio thread. While this short main-thread copy
    // runs, workflow commands are held so the immutable Reference buffer cannot
    // be reset out from under the export.
    if (controlBusy_.exchange(true, std::memory_order_acq_rel)) return nullptr;
    try {
      tonetrace::MatchEngine engine;
      stagedReferenceForExport_ =
          engine.capture(reference_.audio(sampleRate_), currentMatchSettings(true));
    } catch (...) {
      stagedReferenceForExport_.reset();
    }
    controlBusy_.store(false, std::memory_order_release);
    return stagedReferenceForExport_.has_value()
               ? &*stagedReferenceForExport_
               : nullptr;
  }

  static const tonetrace::SpectrumCapture* editorGetStagedSpectrum(
      void* context, int which) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    return instance == nullptr ? nullptr
                               : instance->stagedSpectrumForEditor(which);
  }

  void applyEditorParameter(clap_id id, double requested) noexcept {
    const std::size_t index = parameterIndex(id);
    if (index >= tonetrace::parameterDescriptors().size()) return;
    (void)applyParameter(id, requested);
    consumeWorkflowCommandWhenIdle();

    // applyParameter() updates the plug-in immediately, but a native-editor
    // change is not a host input event. Queue the authoritative resulting
    // value (including any workflow fallback such as 2 -> 1) for the host and
    // ask for a parameter flush. This replaces the old accidental dependency
    // on correction rebuilds/rescans for keeping REAPER/OSARA in sync.
    markDirty(index);
    requestHostParameterFlush();
  }

  static void editorSetParameter(void* context, std::uint32_t paramId,
                                 double value) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    if (instance == nullptr) return;
    instance->applyEditorParameter(static_cast<clap_id>(paramId), value);
  }

  static void editorPlayTraceTone(void* context,
                                  double frequencyHz) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    if (instance != nullptr) instance->requestTraceTone(frequencyHz);
  }

  static void editorPlayBandSweep(void* context, double fromHz, double toHz,
                                  int bandCount, double durationMs) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    if (instance != nullptr) {
      instance->requestBandSweep(fromHz, toHz, bandCount, durationMs);
    }
  }

  static void editorSetBandGain(void* context, std::size_t index,
                                double gainDb) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    if (instance != nullptr) instance->setBandGain(index, gainDb);
  }

  static double editorGetBandGain(void* context, std::size_t index) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    return instance == nullptr ? 0.0 : instance->getBandGain(index);
  }

  static std::size_t editorGetBandCount(void* context) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    return instance == nullptr ? 0 : instance->bandCount();
  }

  static double editorGetSampleRate(void* context) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    return instance == nullptr ? 0.0 : instance->sampleRate_;
  }

  static void editorSetImportedSpectrum(
      void* context, int which,
      const tonetrace::SpectrumCapture& capture) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    if (instance == nullptr || (which != 1 && which != 2)) return;
    {
      std::lock_guard<std::mutex> lock(instance->importMutex_);
      if (which == 1) {
        instance->pendingImportedReference_ = capture;
      } else {
        instance->pendingImportedTarget_ = capture;
      }
    }
    instance->pendingImportKind_.store(which, std::memory_order_release);
    instance->requestMainThread(WorkImport);
  }

  static void editorSetImportedModel(
      void* context, const tonetrace::CorrectionModel& model) noexcept {
    auto* instance = static_cast<ToneTraceClap*>(context);
    if (instance == nullptr) return;
    {
      std::lock_guard<std::mutex> lock(instance->importMutex_);
      instance->pendingImportedModel_ = model;
    }
    instance->pendingImportKind_.store(3, std::memory_order_release);
    instance->requestMainThread(WorkImport);
  }

  static bool CLAP_ABI guiIsApiSupported(const clap_plugin_t*,
                                         const char* api,
                                         bool isFloating) noexcept {
    return !isFloating && api != nullptr &&
           std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
  }

  static bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t*,
                                          const char** api,
                                          bool* isFloating) noexcept {
    if (api == nullptr || isFloating == nullptr) return false;
    *api = CLAP_WINDOW_API_WIN32;
    *isFloating = false;
    return true;
  }

  static bool CLAP_ABI guiCreate(const clap_plugin_t* plugin,
                                 const char* api,
                                 bool isFloating) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->editor_ != nullptr ||
        !guiIsApiSupported(plugin, api, isFloating)) {
      return false;
    }
    try {
      static const clap_plugin_params_t params{
          paramCount,   paramGetInfo,   paramGetValue,
          paramValueToText, paramTextToValue, paramFlush,
      };
      auto editor = std::make_unique<ToneTraceWin32Editor>(
          plugin, &params, instance, editorGetSnapshot, editorGetStagedSpectrum,
          editorSetParameter, editorPlayTraceTone, editorPlayBandSweep,
          editorSetBandGain,
          editorGetBandGain, editorGetBandCount, editorSetImportedSpectrum,
          editorSetImportedModel, editorGetSampleRate);
      if (!editor->create()) return false;
      instance->editor_ = std::move(editor);
      return true;
    } catch (...) {
      return false;
    }
  }

  static void CLAP_ABI guiDestroy(const clap_plugin_t* plugin) noexcept {
    if (auto* instance = self(plugin)) instance->editor_.reset();
  }

  static bool CLAP_ABI guiSetScale(const clap_plugin_t* plugin,
                                   double scale) noexcept {
    auto* instance = self(plugin);
    return instance != nullptr && instance->editor_ != nullptr &&
           instance->editor_->setScale(scale);
  }

  static bool CLAP_ABI guiGetSize(const clap_plugin_t* plugin,
                                  std::uint32_t* width,
                                  std::uint32_t* height) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->editor_ == nullptr || width == nullptr ||
        height == nullptr) {
      return false;
    }
    return instance->editor_->getSize(*width, *height);
  }

  static bool CLAP_ABI guiCanResize(const clap_plugin_t*) noexcept {
    return true;
  }

  static bool CLAP_ABI guiGetResizeHints(
      const clap_plugin_t*,
      clap_gui_resize_hints_t* hints) noexcept {
    if (hints == nullptr) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically = true;
    hints->preserve_aspect_ratio = false;
    hints->aspect_ratio_width = 0;
    hints->aspect_ratio_height = 0;
    return true;
  }

  static bool CLAP_ABI guiAdjustSize(const clap_plugin_t* plugin,
                                     std::uint32_t* width,
                                     std::uint32_t* height) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->editor_ == nullptr || width == nullptr ||
        height == nullptr) {
      return false;
    }
    return instance->editor_->adjustSize(*width, *height);
  }

  static bool CLAP_ABI guiSetSize(const clap_plugin_t* plugin,
                                  std::uint32_t width,
                                  std::uint32_t height) noexcept {
    auto* instance = self(plugin);
    return instance != nullptr && instance->editor_ != nullptr &&
           instance->editor_->setSize(width, height);
  }

  static bool CLAP_ABI guiSetParent(const clap_plugin_t* plugin,
                                    const clap_window_t* window) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->editor_ == nullptr ||
        window == nullptr || window->api == nullptr ||
        std::strcmp(window->api, CLAP_WINDOW_API_WIN32) != 0 ||
        window->win32 == nullptr) {
      return false;
    }
    return instance->editor_->setParent(window->win32);
  }

  static bool CLAP_ABI guiSetTransient(const clap_plugin_t*,
                                       const clap_window_t*) noexcept {
    return false;
  }

  static void CLAP_ABI guiSuggestTitle(const clap_plugin_t*,
                                       const char*) noexcept {}

  static bool CLAP_ABI guiShow(const clap_plugin_t* plugin) noexcept {
    auto* instance = self(plugin);
    return instance != nullptr && instance->editor_ != nullptr &&
           instance->editor_->show();
  }

  static bool CLAP_ABI guiHide(const clap_plugin_t* plugin) noexcept {
    auto* instance = self(plugin);
    return instance != nullptr && instance->editor_ != nullptr &&
           instance->editor_->hide();
  }

  static void CLAP_ABI guiResize(const clap_plugin_t* plugin,
                                 std::uint32_t width,
                                 std::uint32_t height) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr || instance->editor_ == nullptr) return;
    (void)instance->editor_->setSize(width, height);
  }
#endif

  static bool CLAP_ABI renderHasHardRealtimeRequirement(
      const clap_plugin_t*) noexcept {
    // Tone Trace has no hard realtime hardware requirement; it is happy to be
    // told when the host switches to offline rendering.
    return false;
  }

  static bool CLAP_ABI renderSetMode(const clap_plugin_t* plugin,
                                     clap_plugin_render_mode mode) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr) return false;
    instance->setOfflineRendering(mode != CLAP_RENDER_REALTIME);
    return true;
  }

  static uint32_t CLAP_ABI audioPortCount(const clap_plugin_t*, bool) noexcept {
    return 1;
  }

  static bool CLAP_ABI audioPortGet(const clap_plugin_t*,
                                    uint32_t index,
                                    bool isInput,
                                    clap_audio_port_info_t* info) noexcept {
    if (index != 0 || info == nullptr) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s",
                  isInput ? "Main Input" : "Main Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN |
                  CLAP_AUDIO_PORT_SUPPORTS_64BITS |
                  CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = 0;
    return true;
  }

  static uint32_t CLAP_ABI paramCount(const clap_plugin_t*) noexcept {
    return static_cast<uint32_t>(tonetrace::parameterDescriptors().size());
  }

  static bool CLAP_ABI paramGetInfo(const clap_plugin_t*,
                                    uint32_t index,
                                    clap_param_info_t* info) noexcept {
    const auto& descriptors = tonetrace::parameterDescriptors();
    if (info == nullptr || index >= descriptors.size()) return false;
    const auto& descriptor = descriptors[index];
    std::memset(info, 0, sizeof(*info));
    info->id = static_cast<clap_id>(descriptor.id);
    if (descriptor.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (descriptor.readOnly) info->flags |= CLAP_PARAM_IS_READONLY;
    if (descriptor.automatable) info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
    if (descriptor.id == tonetrace::ParameterId::Bypass) {
      info->flags |= CLAP_PARAM_IS_BYPASS;
    }
    std::snprintf(info->name, sizeof(info->name), "%s", descriptor.name);
    std::snprintf(info->module, sizeof(info->module), "%s",
                  descriptor.readOnly ? "Status" : "Tone Trace");
    info->min_value = descriptor.minimum;
    info->max_value = descriptor.maximum;
    info->default_value = descriptor.defaultValue;
    return true;
  }

  static bool CLAP_ABI paramGetValue(const clap_plugin_t* plugin,
                                     clap_id id,
                                     double* output) noexcept {
    auto* instance = self(plugin);
    const std::size_t index = parameterIndex(id);
    if (instance == nullptr || output == nullptr || index >= instance->values_.size()) {
      return false;
    }
    *output = instance->values_[index]->load(std::memory_order_acquire);
    return true;
  }

  static bool CLAP_ABI paramValueToText(const clap_plugin_t* plugin,
                                        clap_id id,
                                        double value,
                                        char* output,
                                        uint32_t capacity) noexcept {
    const auto& descriptors = tonetrace::parameterDescriptors();
    const std::size_t index = parameterIndex(id);
    if (output == nullptr || capacity == 0 || index >= descriptors.size()) return false;
    const auto parameter = descriptors[index].id;
    if (parameter == tonetrace::ParameterId::WorkflowAction) {
      std::snprintf(output, capacity, "%s", workflowText(static_cast<int>(std::lround(value))));
    } else if (parameter == tonetrace::ParameterId::MatchMode) {
      std::snprintf(output, capacity, "%s", modeText(static_cast<int>(std::lround(value))));
    } else if (parameter == tonetrace::ParameterId::LastCommand) {
      std::snprintf(output, capacity, "%s", workflowText(static_cast<int>(std::lround(value))));
    } else if (parameter == tonetrace::ParameterId::Status) {
      std::snprintf(output, capacity, "%s", statusText(static_cast<int>(std::lround(value))));
    } else if (parameter == tonetrace::ParameterId::Confidence) {
      const int level = static_cast<int>(std::lround(value * 3.0));
      const auto* instance = self(plugin);
      const bool acceptedAudio = instance != nullptr &&
          instance->value(tonetrace::ParameterId::CaptureSeconds) > 0.0;
      std::snprintf(output, capacity, "%s",
                    level >= 3 ? "High" : level == 2 ? "Medium" :
                    level == 1 ? "Low; usable with caution" :
                    acceptedAudio ? "Not yet confident" : "No valid audio");
    } else if (parameter == tonetrace::ParameterId::ToneLevelDb &&
               value <= -59.5) {
      std::snprintf(output, capacity, "%s", "Off");
    } else if (parameter == tonetrace::ParameterId::CompleteMatch ||
               parameter == tonetrace::ParameterId::ToneNotifications ||
               parameter == tonetrace::ParameterId::Bypass) {
      std::snprintf(output, capacity, "%s", value >= 0.5 ? "On" : "Off");
    } else if (descriptors[index].stepped) {
      std::snprintf(output, capacity, "%.0f %s", value, descriptors[index].unit);
    } else {
      std::snprintf(output, capacity, "%.3f %s", value, descriptors[index].unit);
    }
    return true;
  }

  static bool CLAP_ABI paramTextToValue(const clap_plugin_t*,
                                        clap_id id,
                                        const char* text,
                                        double* output) noexcept {
    const auto& descriptors = tonetrace::parameterDescriptors();
    const std::size_t index = parameterIndex(id);
    if (text == nullptr || output == nullptr || index >= descriptors.size() ||
        descriptors[index].readOnly) {
      return false;
    }
    const auto parameter = descriptors[index].id;
    if (parameter == tonetrace::ParameterId::WorkflowAction) {
      for (int step = 0; step <= 7; ++step) {
        if (std::strcmp(text, workflowText(step)) == 0) {
          *output = static_cast<double>(step);
          return true;
        }
      }
    } else if (parameter == tonetrace::ParameterId::MatchMode) {
      for (int mode = 0; mode <= 4; ++mode) {
        if (std::strcmp(text, modeText(mode)) == 0) {
          *output = static_cast<double>(mode);
          return true;
        }
      }
    } else if (parameter == tonetrace::ParameterId::ToneLevelDb &&
               (std::strcmp(text, "Off") == 0 ||
                std::strcmp(text, "off") == 0)) {
      *output = -60.0;
      return true;
    }
    const bool toggle = parameter == tonetrace::ParameterId::CompleteMatch ||
                        parameter == tonetrace::ParameterId::ToneNotifications ||
                        parameter == tonetrace::ParameterId::Bypass;
    if (toggle &&
        (std::strcmp(text, "On") == 0 || std::strcmp(text, "on") == 0)) {
      *output = 1.0;
      return true;
    }
    if (toggle &&
        (std::strcmp(text, "Off") == 0 || std::strcmp(text, "off") == 0)) {
      *output = 0.0;
      return true;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (errno != 0 || end == text || !std::isfinite(parsed)) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0' && descriptors[index].unit != nullptr) {
      const std::size_t unitLength = std::strlen(descriptors[index].unit);
      if (std::strncmp(end, descriptors[index].unit, unitLength) != 0) return false;
      end += unitLength;
      while (*end == ' ' || *end == '\t') ++end;
    }
    if (*end != '\0') return false;
    *output = clampedValue(descriptors[index], parsed);
    return true;
  }

  static void CLAP_ABI paramFlush(const clap_plugin_t* plugin,
                                  const clap_input_events_t* input,
                                  const clap_output_events_t* output) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr) return;
    const uint32_t count = input != nullptr && input->size != nullptr
                               ? input->size(input)
                               : 0;
    (void)instance->applyEvents(input, 0, count);
    // REAPER delivers parameter changes through param_flush while the
    // transport is stopped and never calls process(). Consume a queued
    // workflow command here so every step advances without requiring play,
    // before pushing the resulting status/value updates to the host.
    instance->consumePendingWorkflowCommand();
    instance->pushDirtyValues(output, 0);
  }

  static bool CLAP_ABI stateSave(const clap_plugin_t* plugin,
                                 const clap_ostream_t* stream) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr) return false;
    try {
      // CLAP hosts may request state for undo, autosave, duplication, crash
      // recovery, or other bookkeeping; this callback is not proof that the
      // user explicitly saved the project. Never mutate or cancel a live
      // capture here. saveWrapperState() already serializes only a safe Ready
      // state or the validated last-known-good profile, so loading can never
      // resume unfinished learning.
      return writeAll(stream, instance->saveWrapperState());
    } catch (const std::exception&) {
      return false;
    }
  }

  static bool CLAP_ABI stateLoad(const clap_plugin_t* plugin,
                                 const clap_istream_t* stream) noexcept {
    auto* instance = self(plugin);
    if (instance == nullptr) return false;
    try {
      std::string bytes;
      return readAll(stream, bytes) && instance->loadWrapperState(bytes);
    } catch (const std::exception&) {
      return false;
    }
  }

  static uint32_t CLAP_ABI latencyGet(const clap_plugin_t*) noexcept { return 0; }

  static uint32_t CLAP_ABI tailGet(const clap_plugin_t* plugin) noexcept {
    auto* instance = self(plugin);
    return instance == nullptr
               ? 0
               : instance->tailFrames_.load(std::memory_order_acquire);
  }

  clap_plugin_t plugin_{};
  const clap_host_t* host_ = nullptr;
  std::vector<std::unique_ptr<std::atomic<double>>> values_;
  std::atomic<bool> active_{false};
  std::atomic<bool> processing_{false};
  std::atomic<bool> offlineRendering_{false};
  std::atomic<bool> resetArmed_{false};
  std::atomic<bool> controlBusy_{false};
  std::atomic<bool> hasProfile_{false};
  std::atomic<int> lastWorkflowStep_{0};
  std::atomic<int> pendingWorkflowCommand_{0};
  std::atomic<bool> stopToneRequested_{false};
  std::atomic<int> pendingToneRequest_{0};
  std::atomic<bool> traceToneRequested_{false};
  std::atomic<double> traceToneFrequency_{0.0};
  std::atomic<bool> traceSweepRequested_{false};
  std::atomic<double> traceSweepFromHz_{0.0};
  std::atomic<double> traceSweepToHz_{0.0};
  std::atomic<double> traceSweepDurationMs_{0.0};
  std::vector<double> manualGains_;
  // Editor preference: when true the band sliders display and set the band's
  // absolute final level (match + trim, 0 = flat); when false they display and
  // set the trim relative to the match (0 = as matched). Persisted with the
  // wrapper state so it survives a save/reload; it never affects audio.
  bool displayStatic_ = false;
  std::atomic<uint32_t> tailFrames_{0};
  std::atomic<std::uint32_t> pendingWork_{WorkNone};
  std::vector<std::unique_ptr<std::atomic<bool>>> dirtyValues_;
  std::atomic<tonetrace::WorkflowPhase> phase_{tonetrace::WorkflowPhase::Ready};
  std::atomic<tonetrace::WorkflowPhase> phaseBeforeReset_{
      tonetrace::WorkflowPhase::Ready};
  int sampleRate_ = 48000;
  uint32_t maxFrames_ = 0;
  CaptureBuffer reference_;
  CaptureBuffer target_;
  SweepTone tone_;
  bool captureBlocked_ = false;
  bool setupLockedNotice_ = false;
  std::array<std::vector<float>, 2> scratchInput_;
  std::array<std::vector<float>, 2> scratchOutput_;
  std::unique_ptr<tonetrace::HeadlessPluginCore> core_;
  std::string pendingCoreState_;
  // Import slots written by the editor (UI thread) and consumed on the main
  // thread via WorkImport; the mutex guards the payloads, the atomic guards
  // the hand-off flag.
  std::mutex importMutex_;
  std::atomic<int> pendingImportKind_{0};
  tonetrace::SpectrumCapture pendingImportedReference_;
  tonetrace::SpectrumCapture pendingImportedTarget_;
  tonetrace::CorrectionModel pendingImportedModel_;
  std::optional<tonetrace::SpectrumCapture> importedReference_;
  std::optional<tonetrace::SpectrumCapture> importedTarget_;
  // Lazily materialized only for Reference-only .tts export after Learn Target;
  // never participates in matching or project state.
  std::optional<tonetrace::SpectrumCapture> stagedReferenceForExport_;
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
  std::unique_ptr<ToneTraceWin32Editor> editor_;
  std::unique_ptr<StoppedTransportToneWorker> toneWorker_;
#endif
};

const void* CLAP_ABI ToneTraceClap::pluginGetExtension(const clap_plugin_t*,
                                                       const char* id) noexcept {
  static const clap_plugin_audio_ports_t audioPorts{
      audioPortCount,
      audioPortGet,
  };
  static const clap_plugin_params_t params{
      paramCount,
      paramGetInfo,
      paramGetValue,
      paramValueToText,
      paramTextToValue,
      paramFlush,
  };
  static const clap_plugin_state_t state{
      stateSave,
      stateLoad,
  };
  static const clap_plugin_latency_t latency{latencyGet};
  static const clap_plugin_tail_t tail{tailGet};
  static const clap_plugin_render_t render{
      renderHasHardRealtimeRequirement,
      renderSetMode,
  };
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
  static const clap_plugin_gui_t gui{
      guiIsApiSupported,
      guiGetPreferredApi,
      guiCreate,
      guiDestroy,
      guiSetScale,
      guiGetSize,
      guiCanResize,
      guiGetResizeHints,
      guiAdjustSize,
      guiSetSize,
      guiSetParent,
      guiSetTransient,
      guiSuggestTitle,
      guiShow,
      guiHide,
  };
#endif
  if (id == nullptr) return nullptr;
  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
  if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latency;
  if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tail;
  if (std::strcmp(id, CLAP_EXT_RENDER) == 0) return &render;
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_GUI)
  if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
  return nullptr;
}

uint32_t CLAP_ABI factoryCount(const clap_plugin_factory_t*) noexcept { return 1; }

const clap_plugin_descriptor_t* CLAP_ABI factoryDescriptor(
    const clap_plugin_factory_t*, uint32_t index) noexcept {
  return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* CLAP_ABI factoryCreate(const clap_plugin_factory_t*,
                                            const clap_host_t* host,
                                            const char* pluginId) noexcept {
  if (host == nullptr || pluginId == nullptr ||
      std::strcmp(pluginId, kPluginId) != 0 ||
      !clap_version_is_compatible(host->clap_version)) {
    return nullptr;
  }
  try {
    return (new ToneTraceClap(host))->clapPlugin();
  } catch (const std::exception&) {
    return nullptr;
  }
}

const clap_plugin_factory_t kFactory{
    factoryCount,
    factoryDescriptor,
    factoryCreate,
};

std::atomic<unsigned> gEntryReferences{0};

bool CLAP_ABI entryInit(const char*) noexcept {
  gEntryReferences.fetch_add(1, std::memory_order_acq_rel);
  return true;
}

void CLAP_ABI entryDeinit() noexcept {
  unsigned current = gEntryReferences.load(std::memory_order_acquire);
  while (current != 0 &&
         !gEntryReferences.compare_exchange_weak(current, current - 1U,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
  }
}

const void* CLAP_ABI entryFactory(const char* factoryId) noexcept {
  return factoryId != nullptr &&
                 std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
             ? &kFactory
             : nullptr;
}

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION,
    entryInit,
    entryDeinit,
    entryFactory,
};
