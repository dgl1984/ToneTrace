#include "tone/ToneTraceNotificationTone.h"

#include <algorithm>
#include <cmath>

namespace tonetrace::tone
{
std::optional<SweepSpec> confidenceTransition(int oldLevel, int newLevel) noexcept
{
    oldLevel = std::clamp(oldLevel, 0, 3);
    newLevel = std::clamp(newLevel, 0, 3);
    if (newLevel <= 0 || newLevel == oldLevel) return std::nullopt;

    if (newLevel > oldLevel)
    {
        if (newLevel == 1) return SweepSpec { 180.0, 400.0, 200.0 };
        if (newLevel == 2) return SweepSpec { 220.0, 750.0, 230.0 };
        return SweepSpec { 350.0, 1200.0, 340.0 };
    }

    if (oldLevel >= 3) return SweepSpec { 1200.0, 350.0, 340.0 };
    if (oldLevel == 2) return SweepSpec { 750.0, 220.0, 250.0 };
    return SweepSpec { 400.0, 180.0, 220.0 };
}

void SweepTone::stop() noexcept
{
    active_ = false;
    phase_ = 0.0;
    samplesLeft_ = 0;
    samplesTotal_ = 0;
}

void SweepTone::start(const SweepSpec& spec, int sampleRate) noexcept
{
    if (!std::isfinite(spec.fromHz) || !std::isfinite(spec.toHz)
        || !std::isfinite(spec.durationMs) || spec.fromHz <= 0.0
        || spec.toHz <= 0.0 || spec.durationMs <= 0.0 || sampleRate <= 0)
    {
        stop();
        return;
    }

    frequencyStart_ = spec.fromHz;
    frequencyEnd_ = spec.toHz;
    samplesTotal_ = static_cast<std::uint64_t>(std::max(
        1.0, spec.durationMs * 0.001 * static_cast<double>(sampleRate)));
    samplesLeft_ = samplesTotal_;
    phase_ = 0.0;
    active_ = true;
}

double SweepTone::currentFrequency() const noexcept
{
    if (!active_ || frequencyStart_ <= 0.0 || frequencyEnd_ <= 0.0) return 0.0;
    const double fraction = samplesTotal_ > 1
        ? static_cast<double>(samplesTotal_ - samplesLeft_)
            / static_cast<double>(samplesTotal_ - 1)
        : 0.0;
    return frequencyStart_ * std::pow(frequencyEnd_ / frequencyStart_, fraction);
}

void SweepTone::render(float* const* output,
                       std::size_t channels,
                       std::size_t count,
                       int sampleRate,
                       float linearGain) noexcept
{
    if (!active_ || output == nullptr || channels == 0 || count == 0 || sampleRate <= 0)
        return;

    linearGain = std::max(0.0F, linearGain);
    const auto attackFrames = static_cast<std::uint64_t>(
        std::max(1.0, 0.020 * static_cast<double>(sampleRate)));
    constexpr double twoPi = 6.283185307179586476925286766559;

    for (std::size_t frame = 0; frame < count; ++frame)
    {
        if (samplesLeft_ == 0)
        {
            stop();
            break;
        }

        const std::uint64_t elapsed = samplesTotal_ - samplesLeft_;
        const double frequency = currentFrequency();
        const double attack = std::min(1.0, static_cast<double>(elapsed) / attackFrames);
        const double release = std::min(1.0, static_cast<double>(samplesLeft_) / attackFrames);
        const float sample = static_cast<float>(
            std::sin(phase_) * static_cast<double>(linearGain) * std::min(attack, release));

        for (std::size_t channel = 0; channel < channels; ++channel)
            if (output[channel] != nullptr) output[channel][frame] += sample;

        phase_ += twoPi * frequency / static_cast<double>(sampleRate);
        if (phase_ >= twoPi) phase_ = std::fmod(phase_, twoPi);
        --samplesLeft_;
    }
}

} // namespace tonetrace::tone
