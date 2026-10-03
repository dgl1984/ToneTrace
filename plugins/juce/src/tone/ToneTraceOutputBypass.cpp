#include "tone/ToneTraceOutputBypass.h"

#include <algorithm>

namespace tonetrace::tone
{
void OutputBypassMixer::prepare(int sampleRate, bool bypassed) noexcept
{
    wetMix_ = bypassed ? 0.0 : 1.0;
    step_ = 1.0 / std::max(1.0, static_cast<double>(sampleRate) * 0.010);
}

void OutputBypassMixer::process(float* const* wetOutput,
                                const float* const* dryInput,
                                std::size_t channels,
                                std::size_t frames,
                                bool bypassed) noexcept
{
    if (wetOutput == nullptr || dryInput == nullptr || channels == 0 || frames == 0) return;
    const double target = bypassed ? 0.0 : 1.0;

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        if (wetMix_ < target) wetMix_ = std::min(target, wetMix_ + step_);
        else if (wetMix_ > target) wetMix_ = std::max(target, wetMix_ - step_);

        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            if (wetOutput[channel] == nullptr || dryInput[channel] == nullptr) continue;
            const double dry = dryInput[channel][frame];
            const double wet = wetOutput[channel][frame];
            wetOutput[channel][frame] = static_cast<float>(dry + (wet - dry) * wetMix_);
        }
    }
}
} // namespace tonetrace::tone
