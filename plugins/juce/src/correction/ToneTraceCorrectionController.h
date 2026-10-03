#pragma once

#include "tonetrace/tonetrace_realtime.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tonetrace::jucebridge
{
struct CorrectionSettings
{
    MatchMode mode = MatchMode::Voice;
    int resolution = 30;
    double maximumCorrectionDb = 18.0;
    bool fullCorrectionRange = false;
    double correctionStrength = 1.0;
    double correctionSharpness = 1.0;
    double rangeLowHz = 10.0;
    double rangeHighHz = 30000.0;
    std::vector<double> manualGains;
};

struct CorrectionResult
{
    bool accepted = false;
    ProfileIssue issue = ProfileIssue::NoUsableProfile;
    std::string message;
    double confidence = 0.0;
};

// Format-neutral ownership seam between the JUCE wrapper and the released
// Tone Trace 1.0.5 engine. The audio thread calls only process(). All methods
// that allocate, analyse, render, serialize, or mutate profiles are control-
// thread operations and are serialized by controlMutex_.
class CorrectionController
{
public:
    CorrectionController() = default;
    ~CorrectionController() = default;

    CorrectionController(const CorrectionController&) = delete;
    CorrectionController& operator=(const CorrectionController&) = delete;

    // Host lifecycle operation. prepareToPlay is expected not to race the host's
    // process callback. A same-rate re-prepare keeps the active renderer/profile.
    CorrectionResult prepare(int sampleRate, std::size_t channels);

    CorrectionResult analyze(const AudioBuffer& referenceAudio,
                             const AudioBuffer& targetAudio,
                             const CaptureDiagnostics& referenceDiagnostics,
                             const CaptureDiagnostics& targetDiagnostics,
                             const CorrectionSettings& settings,
                             const SpectrumCapture* importedReference = nullptr,
                             const SpectrumCapture* importedTarget = nullptr);

    CorrectionResult rebuild(const CorrectionSettings& settings);
    CorrectionResult commitManualCorrection(const CorrectionSettings& settings);
    CorrectionResult commitSnapshot(const ProfileSnapshot& snapshot);
    CorrectionResult reset();

    // Applies a previously validated ToneTraceProjectState to the live renderer.
    // Unlike HeadlessPluginCore::loadProjectState(), this path is safe after
    // processing has begun because it uses commitCandidate()/clearProfile(),
    // preserving the released click-free renderer transition.
    CorrectionResult restoreProjectState(const std::string& bytes);

    [[nodiscard]] std::string saveProjectState() const;
    [[nodiscard]] bool hasProfile() const;
    [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::unique_ptr<ProfileSnapshot> snapshotCopy() const;

    // Audio-thread operation. No lock, allocation, waiting, host call, or UI.
    void process(const float* const* inputs,
                 float* const* outputs,
                 std::size_t channelCount,
                 std::size_t frames) noexcept;

    void collectRetiredKernels() noexcept;
    void resetAudio() noexcept;

private:
    [[nodiscard]] static MatchSettings makeMatchSettings(const CorrectionSettings& settings,
                                                         bool uncapped);
    [[nodiscard]] IrRenderSettings makeRenderSettings(const CorrectionSettings& settings) const;
    [[nodiscard]] static CorrectionResult resultFromValidation(const ProfileValidation& validation,
                                                               double confidence = 0.0);

    void refreshCachedStateLocked(); // caller owns controlMutex_

    mutable std::mutex controlMutex_; // never acquired by process()
    mutable std::mutex stateCacheMutex_;
    std::string cachedProjectState_ { serializeProjectState(nullptr) };
    std::unique_ptr<HeadlessPluginCore> core_;
    int sampleRate_ = 0;
    std::size_t channels_ = 0;
};

} // namespace tonetrace::jucebridge
