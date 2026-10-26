#pragma once

#include "tonetrace/tonetrace_realtime.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
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

class CorrectionController
{
public:
    CorrectionController() = default;
    ~CorrectionController() = default;

    CorrectionController(const CorrectionController&) = delete;
    CorrectionController& operator=(const CorrectionController&) = delete;

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
    CorrectionResult restoreProjectState(const std::string& bytes);

    [[nodiscard]] std::string saveProjectState() const;
    [[nodiscard]] bool hasProfile() const;
    [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::unique_ptr<ProfileSnapshot> snapshotCopy() const;
    [[nodiscard]] std::optional<SpectrumCapture> inProgressTargetCopy() const;

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

    void refreshCachedStateLocked();
    void clearInProgressTarget();

    mutable std::mutex controlMutex_;
    mutable std::mutex stateCacheMutex_;
    mutable std::mutex analysisCaptureMutex_;
    std::string cachedProjectState_ { serializeProjectState(nullptr) };
    std::optional<SpectrumCapture> inProgressTarget_;
    std::unique_ptr<HeadlessPluginCore> core_;
    int sampleRate_ = 0;
    std::size_t channels_ = 0;
};

} // namespace tonetrace::jucebridge
