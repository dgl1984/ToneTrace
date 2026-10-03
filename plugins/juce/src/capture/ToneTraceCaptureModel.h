#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tonetrace::capture
{
enum class MatchMode : int
{
    FullMix = 0,
    Voice = 1,
    Drums = 2,
    BassSynth = 3,
    CustomMaxCapture = 4,
};

enum class WorkflowPhase : int
{
    Ready = 0,
    CapturingReference = 1,
    CapturingTarget = 2,
    Preview = 3,
    Frozen = 4,
};


[[nodiscard]] constexpr bool isLiveCapturePhase(WorkflowPhase phase) noexcept
{
    return phase == WorkflowPhase::CapturingReference
        || phase == WorkflowPhase::CapturingTarget;
}

// Host lifecycle policy shared by prepare/reprepare and state-echo handling.
// A same-rate suspend/resume is not a Reset. Raw capture survives exactly as in
// released 1.0.5; a real sample-rate change still invalidates it.
[[nodiscard]] constexpr bool shouldPreserveCaptureAcrossReprepare(
    WorkflowPhase phase, int previousSampleRate, int requestedSampleRate,
    bool buffersReady) noexcept
{
    return isLiveCapturePhase(phase)
        && previousSampleRate > 0
        && previousSampleRate == requestedSampleRate
        && buffersReady;
}

// Some VST3 hosts round-trip the plug-in's own persisted state while stopping
// and restarting processing. Because raw capture is deliberately not project
// state, replaying an equivalent Ready/Frozen snapshot must not erase an active
// Reference/Target capture. A genuinely different preset/project state still
// replaces unfinished learning.
[[nodiscard]] constexpr bool shouldPreserveCaptureForEquivalentStateEcho(
    WorkflowPhase phase, bool parametersEquivalent, bool coreStateEquivalent) noexcept
{
    return isLiveCapturePhase(phase) && parametersEquivalent && coreStateEquivalent;
}

enum class Status : int
{
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
    PreparingCapture = 30,
};

inline constexpr double kStandardCaptureSeconds = 30.0;
inline constexpr double kCustomMaximumCaptureSeconds = 60.0;

[[nodiscard]] constexpr double maximumCaptureSeconds(MatchMode mode) noexcept
{
    return mode == MatchMode::CustomMaxCapture
        ? kCustomMaximumCaptureSeconds
        : kStandardCaptureSeconds;
}

[[nodiscard]] const char* statusText(Status status) noexcept;
[[nodiscard]] const char* workflowText(int step) noexcept;
[[nodiscard]] const char* confidenceText(int confidenceLevel, bool acceptedAudio) noexcept;
[[nodiscard]] MatchMode matchModeFromIndex(int index) noexcept;

struct CaptureDiagnostics
{
    std::uint64_t sampleCount = 0;
    std::uint64_t nonFiniteSamples = 0;
    std::uint64_t clippedSamples = 0;
    double peakAbsolute = 0.0;

    void observe(const float* const* channels,
                 std::size_t channelCount,
                 std::size_t frames) noexcept;
    [[nodiscard]] double clippedFraction() const noexcept;
};

struct CaptureBuffer
{
    std::array<std::vector<float>, 2> channels;
    std::size_t capacityFrames = 0;
    std::size_t frames = 0;
    std::size_t channelCount = 0;
    bool overflowed = false;
    CaptureDiagnostics diagnostics;
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
    int monitorConfigSampleRate = 0;
    MatchMode monitorConfigMode = MatchMode::FullMix;
    double monitorGatePower = 0.0;
    double monitorPowerAlpha = 0.0;
    std::uint64_t monitorUpdateFrames = 1;
    std::array<double, 6> monitorAlpha{};
    std::size_t monitorMaximumFrames = 0;
    std::array<double, 7> intervalBandPower{};
    std::array<double, 7> bandMomentSum{};
    std::array<double, 7> previousBandAggregate{};

    void prepare(int sampleRate, double maximumSeconds);
    void ensureCapacity(int sampleRate, double maximumSeconds);
    void reset() noexcept;
    void release() noexcept;
    void append(const float* const* input,
                std::size_t inputChannels,
                std::size_t count,
                int sampleRate,
                MatchMode mode) noexcept;

    [[nodiscard]] double validSeconds(int sampleRate) const noexcept;
    [[nodiscard]] bool readyForSave(int sampleRate) const noexcept;
    [[nodiscard]] CaptureDiagnostics profileDiagnostics() const noexcept;
    [[nodiscard]] int calculateConfidence(MatchMode mode, int sampleRate) const noexcept;
};

[[nodiscard]] Status captureStatus(WorkflowPhase phase,
                                   int confidenceLevel,
                                   const CaptureBuffer& capture,
                                   int sampleRate) noexcept;

// Preserves the released REAPER/OSARA enum behavior: a tiny fractional move
// within the adjacent interval is directional intent, while exact integers stay exact.
[[nodiscard]] int workflowStepFromHostValue(double requested, int previous) noexcept;

} // namespace tonetrace::capture
