#include "ToneTraceCaptureModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tonetrace::capture
{
const char* statusText(Status status) noexcept
{
    switch (status)
    {
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
        case Status::PreparingCapture:
            return "Preparing 60-second Custom Max Capture buffer";
        case Status::Ready:
        default:
            return "Ready";
    }
}

const char* workflowText(int step) noexcept
{
    switch (step)
    {
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

const char* confidenceText(int confidenceLevel, bool acceptedAudio) noexcept
{
    if (confidenceLevel >= 3) return "High";
    if (confidenceLevel == 2) return "Medium";
    if (confidenceLevel == 1) return "Low; usable with caution";
    return acceptedAudio ? "Not yet confident" : "No valid audio";
}

MatchMode matchModeFromIndex(int index) noexcept
{
    switch (index)
    {
        case 1: return MatchMode::Voice;
        case 2: return MatchMode::Drums;
        case 3: return MatchMode::BassSynth;
        case 4: return MatchMode::CustomMaxCapture;
        default: return MatchMode::FullMix;
    }
}

void CaptureDiagnostics::observe(const float* const* input,
                                 std::size_t inputChannels,
                                 std::size_t count) noexcept
{
    if (input == nullptr) return;
    for (std::size_t channel = 0; channel < inputChannels; ++channel)
    {
        if (input[channel] == nullptr) continue;
        for (std::size_t frame = 0; frame < count; ++frame)
        {
            if (sampleCount != std::numeric_limits<std::uint64_t>::max()) ++sampleCount;
            const double sample = input[channel][frame];
            if (!std::isfinite(sample))
            {
                if (nonFiniteSamples != std::numeric_limits<std::uint64_t>::max())
                    ++nonFiniteSamples;
                continue;
            }
            const double absolute = std::abs(sample);
            peakAbsolute = std::max(peakAbsolute, absolute);
            if (absolute >= 0.999 && clippedSamples != std::numeric_limits<std::uint64_t>::max())
                ++clippedSamples;
        }
    }
}

double CaptureDiagnostics::clippedFraction() const noexcept
{
    return sampleCount == 0
        ? 0.0
        : static_cast<double>(clippedSamples) / static_cast<double>(sampleCount);
}

void CaptureBuffer::prepare(int sampleRate, double maximumSeconds)
{
    monitorConfigSampleRate = 0;
    capacityFrames = static_cast<std::size_t>(
        std::ceil(static_cast<double>(sampleRate) * maximumSeconds));
    for (auto& channel : channels) channel.assign(capacityFrames, 0.0F);
    reset();
}

void CaptureBuffer::ensureCapacity(int sampleRate, double maximumSeconds)
{
    monitorConfigSampleRate = 0;
    const std::size_t requiredFrames = static_cast<std::size_t>(
        std::ceil(static_cast<double>(sampleRate) * maximumSeconds));
    if (requiredFrames <= capacityFrames) return;
    for (auto& channel : channels) channel.resize(requiredFrames, 0.0F);
    capacityFrames = requiredFrames;
}

void CaptureBuffer::reset() noexcept
{
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

void CaptureBuffer::release() noexcept
{
    for (auto& channel : channels) std::vector<float>().swap(channel);
    capacityFrames = 0;
    reset();
}

void CaptureBuffer::append(const float* const* input,
                           std::size_t inputChannels,
                           std::size_t count,
                           int sampleRate,
                           MatchMode mode) noexcept
{
    if (input == nullptr || inputChannels == 0 || sampleRate <= 0) return;
    inputChannels = std::min<std::size_t>(2, inputChannels);
    if (channelCount == 0) channelCount = inputChannels;
    if (channelCount != inputChannels)
    {
        diagnostics.nonFiniteSamples = 1;
        return;
    }

    diagnostics.observe(input, inputChannels, count);
    receivedFrames += count;

    constexpr std::array<double, 6> monitorCutoffs{
        100.0, 300.0, 1000.0, 3000.0, 8000.0, 16000.0};
    if (monitorConfigSampleRate != sampleRate || monitorConfigMode != mode)
    {
        monitorConfigSampleRate = sampleRate;
        monitorConfigMode = mode;
        const double gateDb = mode == MatchMode::Voice ? -55.0 : -65.0;
        monitorGatePower = std::pow(10.0, gateDb / 10.0);
        monitorPowerAlpha = 1.0 - std::exp(
            -1.0 / std::max(1.0, 0.030 * static_cast<double>(sampleRate)));
        monitorUpdateFrames = static_cast<std::uint64_t>(
            std::max(1.0, 0.050 * static_cast<double>(sampleRate)));
        for (std::size_t band = 0; band < monitorCutoffs.size(); ++band)
        {
            const double cutoff = std::min(monitorCutoffs[band], sampleRate * 0.45);
            monitorAlpha[band] = 1.0 - std::exp(
                -6.2831853071795864769 * cutoff / sampleRate);
        }
        monitorMaximumFrames = std::min(
            capacityFrames,
            static_cast<std::size_t>(std::ceil(
                sampleRate * maximumCaptureSeconds(mode))));
    }

    for (std::size_t frame = 0; frame < count; ++frame)
    {
        double power = 0.0;
        std::array<double, 7> bandPower{};
        bool finite = true;
        for (std::size_t channel = 0; channel < inputChannels; ++channel)
        {
            const double sample = input[channel][frame];
            finite = finite && std::isfinite(sample);
            power += sample * sample;
        }
        if (!finite) continue;

        for (std::size_t channel = 0; channel < inputChannels; ++channel)
        {
            const double sample = input[channel][frame];
            double previousLowpass = 0.0;
            for (std::size_t band = 0; band < monitorCutoffs.size(); ++band)
            {
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
        for (auto& band : bandPower) band /= static_cast<double>(inputChannels);
        smoothedPower += (power - smoothedPower) * monitorPowerAlpha;
        if (smoothedPower <= monitorGatePower) continue;

        if (frames >= monitorMaximumFrames)
        {
            overflowed = true;
            continue;
        }

        for (std::size_t channel = 0; channel < inputChannels; ++channel)
        {
            channels[channel][frames] = input[channel][frame];
            ++acceptedSampleCount;
            if (std::abs(input[channel][frame]) >= 0.999F) ++acceptedClippedSamples;
        }
        ++frames;
        ++validFrames;
        ++intervalFrames;
        for (std::size_t band = 0; band < intervalBandPower.size(); ++band)
            intervalBandPower[band] += bandPower[band];

        if (intervalFrames >= monitorUpdateFrames)
        {
            const double completedIntervalFrames = static_cast<double>(intervalFrames);
            intervalFrames = 0;
            double totalMovement = 0.0;
            std::size_t comparedBands = 0;
            double momentPower = 4.0;
            if (mode == MatchMode::Voice) momentPower = 3.0;
            else if (mode == MatchMode::Drums) momentPower = 5.0;
            else if (mode == MatchMode::BassSynth) momentPower = 3.5;
            else if (mode == MatchMode::CustomMaxCapture) momentPower = 6.0;

            for (std::size_t band = 0; band < intervalBandPower.size(); ++band)
            {
                const double intervalMean = intervalBandPower[band] / completedIntervalFrames;
                bandMomentSum[band] += std::pow(std::max(0.0, intervalMean), momentPower);
                const double aggregate = std::pow(
                    bandMomentSum[band] / static_cast<double>(updateCounter + 1U),
                    1.0 / momentPower);
                if (aggregate > 1.0e-20 && previousBandAggregate[band] > 1.0e-20)
                {
                    totalMovement += std::abs(std::log(
                        aggregate / previousBandAggregate[band]));
                    ++comparedBands;
                }
                previousBandAggregate[band] = aggregate;
                intervalBandPower[band] = 0.0;
            }
            const double movement = comparedBands > 0 ? totalMovement / comparedBands : 1.0;
            stability += (movement - stability) * 0.1;
            ++updateCounter;
        }
    }

    confidenceLevel = calculateConfidence(mode, sampleRate);
}

double CaptureBuffer::validSeconds(int sampleRate) const noexcept
{
    return sampleRate > 0 ? static_cast<double>(validFrames) / sampleRate : 0.0;
}

bool CaptureBuffer::readyForSave(int sampleRate) const noexcept
{
    return validSeconds(sampleRate) >= 0.35 && updateCounter >= 3;
}

CaptureDiagnostics CaptureBuffer::profileDiagnostics() const noexcept
{
    auto result = diagnostics;
    result.sampleCount = acceptedSampleCount;
    result.clippedSamples = acceptedClippedSamples;
    return result;
}

int CaptureBuffer::calculateConfidence(MatchMode mode, int sampleRate) const noexcept
{
    const double validTime = validSeconds(sampleRate);
    double lowTime = 1.8;
    double mediumTime = 7.0;
    double highTime = 16.0;
    double lowStability = 0.100;
    double mediumStability = 0.035;
    double highStability = 0.016;

    if (mode == MatchMode::Voice)
    {
        lowTime = 2.5;
        mediumTime = 8.0;
        highTime = 18.0;
        lowStability = 0.080;
        mediumStability = 0.028;
        highStability = 0.012;
    }
    else if (mode == MatchMode::Drums)
    {
        lowTime = 1.5;
        mediumTime = 5.0;
        highTime = 12.0;
        lowStability = 0.120;
        mediumStability = 0.050;
        highStability = 0.025;
    }
    else if (mode == MatchMode::BassSynth)
    {
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

Status captureStatus(WorkflowPhase phase,
                     int confidenceLevel,
                     const CaptureBuffer& capture,
                     int sampleRate) noexcept
{
    if (phase == WorkflowPhase::CapturingReference)
    {
        if (confidenceLevel >= 3) return Status::ReferenceHighConfidence;
        if (confidenceLevel == 2) return Status::ReferenceMediumConfidence;
        if (confidenceLevel == 1) return Status::ReferenceLowConfidence;
        if (capture.validFrames == 0) return Status::CapturingReference;
        if (capture.updateCounter == 0) return Status::ReferenceAudioDetected;
        if (capture.validSeconds(sampleRate) < 0.35) return Status::ReferenceCollecting;
        if (capture.stability > 0.18) return Status::ReferenceUnstable;
        return Status::ReferenceReady;
    }

    if (confidenceLevel >= 3) return Status::TargetHighConfidence;
    if (confidenceLevel == 2) return Status::TargetMediumConfidence;
    if (confidenceLevel == 1) return Status::TargetLowConfidence;
    if (capture.validFrames == 0) return Status::CapturingTarget;
    if (capture.updateCounter == 0) return Status::TargetAudioDetected;
    if (capture.validSeconds(sampleRate) < 0.35) return Status::TargetCollecting;
    if (capture.stability > 0.18) return Status::TargetUnstable;
    return Status::TargetReady;
}

int workflowStepFromHostValue(double requested, int previous) noexcept
{
    if (!std::isfinite(requested)) return previous;
    requested = std::clamp(requested, 0.0, 7.0);
    const double nearest = std::round(requested);
    const double tolerance = 1.0e-7 * std::max(1.0, std::abs(nearest));
    if (std::abs(requested - nearest) <= tolerance)
        return static_cast<int>(nearest);

    if (requested > static_cast<double>(previous) &&
        requested < static_cast<double>(previous + 1))
        return std::min(7, previous + 1);

    if (requested < static_cast<double>(previous) &&
        requested > static_cast<double>(previous - 1))
        return std::max(0, previous - 1);

    return std::clamp(static_cast<int>(std::lround(requested)), 0, 7);
}

} // namespace tonetrace::capture
