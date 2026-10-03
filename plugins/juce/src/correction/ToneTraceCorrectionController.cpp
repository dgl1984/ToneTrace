#include "correction/ToneTraceCorrectionController.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace tonetrace::jucebridge
{
namespace
{
CorrectionResult failure(ProfileIssue issue, const std::string& message)
{
    return { false, issue, message, 0.0 };
}
}

CorrectionResult CorrectionController::resultFromValidation(const ProfileValidation& validation,
                                                            double confidence)
{
    return { validation.accepted, validation.issue, validation.message, confidence };
}

MatchSettings CorrectionController::makeMatchSettings(const CorrectionSettings& settings,
                                                       bool uncapped)
{
    MatchSettings match;
    match.mode = settings.mode;
    // Released 1.0.5 captures/retains the full analysis range. Low/High are a
    // render mask and therefore live in IrRenderSettings below.
    match.rangeLowHz = 10.0;
    match.rangeHighHz = 30000.0;
    match.resolution = std::clamp(settings.resolution, 1, 120);
    match.maximumCorrectionDb = (uncapped || settings.fullCorrectionRange)
        ? 60.0
        : std::clamp(settings.maximumCorrectionDb, 1.0, 60.0);
    return match;
}

IrRenderSettings CorrectionController::makeRenderSettings(const CorrectionSettings& settings) const
{
    IrRenderSettings render;
    render.sampleRate = sampleRate_;
    render.correctionStrength = settings.correctionStrength;
    render.correctionSharpness = settings.correctionSharpness;
    // Product Foundation 01a/Candidate C already proved Correction Gain as one
    // separate 20 ms JUCE output-trim stage. Keep that single ownership path;
    // the learned profile is rendered at 0 dB global gain and the JUCE stage is
    // applied afterwards. Static transfer therefore matches released 1.0.5
    // without creating two owners for the same parameter.
    render.correctionGainDb = 0.0;
    render.rangeLowHz = settings.rangeLowHz;
    render.rangeHighHz = settings.rangeHighHz;
    render.manualGains = settings.manualGains;
    return render;
}

CorrectionResult CorrectionController::prepare(int sampleRate, std::size_t channels)
{
    if (sampleRate < 8000 || sampleRate > 768000 || channels == 0 || channels > 2)
        return failure(ProfileIssue::InvalidModel, "Invalid realtime configuration");

    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ != nullptr && sampleRate_ == sampleRate && channels_ == channels)
        return { true, ProfileIssue::None, "Realtime core already prepared", 0.0 };

    std::unique_ptr<ProfileSnapshot> preserved;
    if (core_ != nullptr)
    {
        if (const auto* snapshot = core_->frozenSnapshot(); snapshot != nullptr)
            preserved = std::make_unique<ProfileSnapshot>(*snapshot);
    }

    try
    {
        RealtimeConvolverConfig config;
        config.sampleRate = sampleRate;
        config.channels = channels;
        auto replacement = std::make_unique<HeadlessPluginCore>(config);

        if (preserved != nullptr)
        {
            preserved->renderSettings.sampleRate = sampleRate;
            const auto validation = replacement->commitCandidate(*preserved);
            if (!validation.accepted)
                return resultFromValidation(validation);
        }

        core_ = std::move(replacement);
        sampleRate_ = sampleRate;
        channels_ = channels;
        refreshCachedStateLocked();
        return { true, ProfileIssue::None, "Realtime core prepared", 0.0 };
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidModel, error.what());
    }
}

CorrectionResult CorrectionController::analyze(const AudioBuffer& referenceAudio,
                                                const AudioBuffer& targetAudio,
                                                const CaptureDiagnostics& referenceDiagnostics,
                                                const CaptureDiagnostics& targetDiagnostics,
                                                const CorrectionSettings& settings,
                                                const SpectrumCapture* importedReference,
                                                const SpectrumCapture* importedTarget)
{
    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ == nullptr || sampleRate_ <= 0)
        return failure(ProfileIssue::RendererBusy, "Realtime core is not prepared");

    try
    {
        MatchEngine engine;
        const auto uncapped = makeMatchSettings(settings, true);
        const auto reference = importedReference != nullptr
            ? *importedReference : engine.capture(referenceAudio, uncapped);
        const auto target = importedTarget != nullptr
            ? *importedTarget : engine.capture(targetAudio, uncapped);
        auto model = engine.match(reference, target, uncapped);

        ProfileSnapshot candidate;
        candidate.reference = reference;
        candidate.target = target;
        candidate.uncappedModel = std::move(model);
        candidate.matchSettings = makeMatchSettings(settings, false);
        candidate.renderSettings = makeRenderSettings(settings);
        candidate.referenceDiagnostics = referenceDiagnostics;
        candidate.targetDiagnostics = targetDiagnostics;

        const auto validation = core_->commitCandidate(candidate);
        if (validation.accepted) refreshCachedStateLocked();
        const auto confidence = std::min(reference.confidence, target.confidence);
        return resultFromValidation(validation, confidence);
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidCapture, error.what());
    }
}

CorrectionResult CorrectionController::rebuild(const CorrectionSettings& settings)
{
    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ == nullptr)
        return failure(ProfileIssue::RendererBusy, "Realtime core is not prepared");
    const auto* current = core_->frozenSnapshot();
    if (current == nullptr)
        return failure(ProfileIssue::NoUsableProfile, "No learned profile is available");

    try
    {
        auto candidate = *current;
        const auto requestedAudible = makeMatchSettings(settings, false);
        const auto requestedUncapped = makeMatchSettings(settings, true);

        if (candidate.uncappedModel.mode != requestedUncapped.mode)
        {
            MatchEngine engine;
            candidate.uncappedModel = engine.match(
                candidate.reference, candidate.target, requestedUncapped);
        }

        candidate.matchSettings.mode = requestedAudible.mode;
        candidate.matchSettings.maximumCorrectionDb = requestedAudible.maximumCorrectionDb;
        candidate.matchSettings.resolution = requestedAudible.resolution;
        candidate.uncappedModel.resolution = requestedAudible.resolution;
        candidate.renderSettings = makeRenderSettings(settings);

        const auto validation = core_->commitCandidate(candidate);
        if (validation.accepted) refreshCachedStateLocked();
        return resultFromValidation(validation,
            std::min(candidate.reference.confidence, candidate.target.confidence));
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidModel, error.what());
    }
}

CorrectionResult CorrectionController::commitManualCorrection(const CorrectionSettings& settings)
{
    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ == nullptr)
        return failure(ProfileIssue::RendererBusy, "Realtime core is not prepared");

    try
    {
        auto render = makeRenderSettings(settings);
        const auto validation = core_->commitManualCorrection(render);
        if (validation.accepted) refreshCachedStateLocked();
        return resultFromValidation(validation);
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidModel, error.what());
    }
}

CorrectionResult CorrectionController::commitSnapshot(const ProfileSnapshot& snapshot)
{
    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ == nullptr)
        return failure(ProfileIssue::RendererBusy, "Realtime core is not prepared");

    try
    {
        auto candidate = snapshot;
        candidate.renderSettings.sampleRate = sampleRate_;
        const auto validation = core_->commitCandidate(candidate);
        if (validation.accepted) refreshCachedStateLocked();
        return resultFromValidation(validation,
            std::min(candidate.reference.confidence, candidate.target.confidence));
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidModel, error.what());
    }
}

CorrectionResult CorrectionController::reset()
{
    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ == nullptr)
    {
        refreshCachedStateLocked();
        return { true, ProfileIssue::None, "Tone Trace reset", 0.0 };
    }

    try
    {
        core_->clearProfile();
        refreshCachedStateLocked();
        return { true, ProfileIssue::None, "Tone Trace reset", 0.0 };
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidModel, error.what());
    }
}

CorrectionResult CorrectionController::restoreProjectState(const std::string& bytes)
{
    RestoredProjectState restored;
    try
    {
        restored = deserializeProjectState(bytes);
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidModel, error.what());
    }

    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ == nullptr)
        return failure(ProfileIssue::RendererBusy, "Realtime core is not prepared");

    try
    {
        if (restored.phase == WorkflowPhase::Frozen && restored.snapshot != nullptr)
        {
            restored.snapshot->renderSettings.sampleRate = sampleRate_;
            const auto validation = core_->commitCandidate(*restored.snapshot);
            if (validation.accepted) refreshCachedStateLocked();
            return resultFromValidation(validation,
                std::min(restored.snapshot->reference.confidence,
                         restored.snapshot->target.confidence));
        }

        core_->clearProfile();
        refreshCachedStateLocked();
        return { true, ProfileIssue::None, "Ready state restored", 0.0 };
    }
    catch (const std::exception& error)
    {
        return failure(ProfileIssue::InvalidModel, error.what());
    }
}

std::string CorrectionController::saveProjectState() const
{
    const std::lock_guard<std::mutex> lock(stateCacheMutex_);
    return cachedProjectState_;
}

void CorrectionController::refreshCachedStateLocked()
{
    const auto state = core_ != nullptr ? core_->saveProjectState()
                                        : serializeProjectState(nullptr);
    const std::lock_guard<std::mutex> cacheLock(stateCacheMutex_);
    cachedProjectState_ = state;
}

bool CorrectionController::hasProfile() const
{
    const std::lock_guard<std::mutex> lock(controlMutex_);
    return core_ != nullptr && core_->frozenSnapshot() != nullptr;
}

std::unique_ptr<ProfileSnapshot> CorrectionController::snapshotCopy() const
{
    const std::lock_guard<std::mutex> lock(controlMutex_);
    if (core_ == nullptr || core_->frozenSnapshot() == nullptr) return {};
    return std::make_unique<ProfileSnapshot>(*core_->frozenSnapshot());
}

void CorrectionController::resetAudio() noexcept
{
    if (core_ != nullptr) core_->resetAudio();
}

void CorrectionController::process(const float* const* inputs,
                                   float* const* outputs,
                                   std::size_t channelCount,
                                   std::size_t frames) noexcept
{
    auto* core = core_.get();
    if (core == nullptr) return;
    core->process(inputs, outputs, channelCount, frames);
}

void CorrectionController::collectRetiredKernels() noexcept
{
    if (!controlMutex_.try_lock()) return;
    if (core_ != nullptr) core_->collectRetiredKernels();
    controlMutex_.unlock();
}

} // namespace tonetrace::jucebridge
