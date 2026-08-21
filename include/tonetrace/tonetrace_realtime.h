#pragma once

#include "tonetrace/tonetrace_engine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tonetrace {

// These numeric identifiers are part of the host-automation contract. They
// must never be recycled, even if a parameter is retired in a later release.
enum class ParameterId : std::uint32_t {
  WorkflowAction = 100,
  MatchMode = 110,
  CorrectionStrength = 120,
  Resolution = 130,
  RangeLowHz = 140,
  RangeHighHz = 150,
  MaximumCorrectionDb = 160,
  CompleteMatch = 170,
  CorrectionGainDb = 180,
  CorrectionSharpness = 190,
  LastCommand = 195,
  Confidence = 200,
  CurveDriftDb = 210,
  CaptureSeconds = 220,
  Status = 230,
  ToneNotifications = 240,
  ToneLevelDb = 250,
  Bypass = 260,
  EmergencyClipGuardDb = 270,
};

struct ParameterDescriptor {
  ParameterId id{};
  const char* name = "";
  double minimum = 0.0;
  double maximum = 1.0;
  double defaultValue = 0.0;
  const char* unit = "";
  bool automatable = false;
  bool readOnly = false;
  bool stepped = false;
};

[[nodiscard]] const std::vector<ParameterDescriptor>& parameterDescriptors();

enum class WorkflowPhase : std::uint32_t {
  Ready = 0,
  CapturingReference = 1,
  CapturingTarget = 2,
  Preview = 3,
  Frozen = 4,
};

// A normal live Target needs at least low confidence. If the accepted-audio
// buffer is completely full, the user-facing contract explicitly allows the
// workflow to continue while preserving a truthful zero-confidence reading.
[[nodiscard]] constexpr bool targetCaptureCanCorrect(WorkflowPhase phase,
                                                     int confidenceLevel,
                                                     bool captureFull,
                                                     bool importedTarget) noexcept {
  return importedTarget ||
         (phase == WorkflowPhase::CapturingTarget &&
          (confidenceLevel >= 1 || captureFull));
}

enum class ProfileIssue : std::uint32_t {
  None = 0,
  NoUsableProfile,
  NonFiniteAudio,
  SevereClipping,
  InsufficientAudio,
  InvalidCapture,
  InvalidModel,
  RendererBusy,
};

struct CaptureDiagnostics {
  std::uint64_t sampleCount = 0;
  std::uint64_t nonFiniteSamples = 0;
  std::uint64_t clippedSamples = 0;
  double peakAbsolute = 0.0;

  void observe(const float* const* channels,
               std::size_t channelCount,
               std::size_t frames) noexcept;
  [[nodiscard]] double clippedFraction() const noexcept;
};

struct ProfileSnapshot {
  SpectrumCapture reference;
  SpectrumCapture target;
  CorrectionModel uncappedModel;
  MatchSettings matchSettings;
  IrRenderSettings renderSettings;
  CaptureDiagnostics referenceDiagnostics;
  CaptureDiagnostics targetDiagnostics;
};

struct ProfileValidation {
  bool accepted = false;
  ProfileIssue issue = ProfileIssue::NoUsableProfile;
  std::string message;
};

[[nodiscard]] ProfileValidation validateProfileSnapshot(
    const ProfileSnapshot& snapshot);
[[nodiscard]] std::vector<double> renderProfileKernel(
    const ProfileSnapshot& snapshot);

// Project state intentionally has only two restorable phases. A live capture
// serializes its latest validated snapshot as Frozen; an unusable intermediate
// capture serializes as Ready. Loading can therefore never resume learning
// implicitly or create a workflow state the user did not select.
[[nodiscard]] std::string serializeProjectState(
    const ProfileSnapshot* latestValidatedSnapshot);

struct RestoredProjectState {
  WorkflowPhase phase = WorkflowPhase::Ready;
  std::unique_ptr<ProfileSnapshot> snapshot;
};

[[nodiscard]] RestoredProjectState deserializeProjectState(
    const std::string& bytes);

struct RealtimeConvolverConfig {
  int sampleRate = 48000;
  std::size_t channels = 2;
  std::size_t directHeadFrames = 128;
  std::size_t earlyTailEndFrames = 1024;
  double minimumCrossfadeSeconds = 0.02;
};

// A zero-reported-latency FIR renderer. The direct head produces the current
// sample immediately. Two partitioned FFT tiers calculate the remaining tail
// before its first coefficient is due. Kernel preparation and reclamation are
// control-thread operations; process() allocates, locks, and throws nothing.
class RealtimeConvolver {
 public:
  explicit RealtimeConvolver(const RealtimeConvolverConfig& config);
  ~RealtimeConvolver();

  RealtimeConvolver(const RealtimeConvolver&) = delete;
  RealtimeConvolver& operator=(const RealtimeConvolver&) = delete;
  RealtimeConvolver(RealtimeConvolver&&) noexcept;
  RealtimeConvolver& operator=(RealtimeConvolver&&) noexcept;

  // Call before processing starts. Replacing an initial kernel is permitted
  // only while no asynchronous change is pending.
  void installInitialKernel(const std::vector<double>& impulseResponse);

  // Control thread. If a prepared change has not yet started, a newer submit
  // replaces it so rapid edits coalesce to the latest requested correction.
  // A transition already running on the audio thread is never interrupted.
  [[nodiscard]] bool submitKernel(const std::vector<double>& impulseResponse);
  void collectRetiredKernels() noexcept;

  void setBypassed(bool bypassed) noexcept;
  void reset() noexcept;

  void process(const float* const* inputs,
               float* const* outputs,
               std::size_t channelCount,
               std::size_t frames) noexcept;

  [[nodiscard]] std::size_t latencyFrames() const noexcept;
  [[nodiscard]] bool hasPendingKernel() const noexcept;
  [[nodiscard]] bool hasRetiredKernel() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Format-neutral host adapter used by the CLAP entry point.
// It owns the last validated snapshot, project-state rules, and live renderer;
// wrappers should translate host events rather than duplicate this behavior.
class HeadlessPluginCore {
 public:
  explicit HeadlessPluginCore(const RealtimeConvolverConfig& config);

  [[nodiscard]] ProfileValidation commitCandidate(
      const ProfileSnapshot& candidate);
  void clearProfile();
  [[nodiscard]] std::string saveProjectState() const;
  void loadProjectState(const std::string& bytes);

  void setBypassed(bool bypassed) noexcept;
  void resetAudio() noexcept;
  void collectRetiredKernels() noexcept;
  [[nodiscard]] bool hasRetiredKernel() const noexcept;
  void process(const float* const* inputs,
               float* const* outputs,
               std::size_t channelCount,
               std::size_t frames) noexcept;

  [[nodiscard]] WorkflowPhase phase() const noexcept;
  [[nodiscard]] std::size_t latencyFrames() const noexcept;
  [[nodiscard]] const ProfileSnapshot* frozenSnapshot() const noexcept;

 private:
  RealtimeConvolver renderer_;
  std::unique_ptr<ProfileSnapshot> snapshot_;
  std::atomic<bool> processingStarted_{false};
};

}  // namespace tonetrace
