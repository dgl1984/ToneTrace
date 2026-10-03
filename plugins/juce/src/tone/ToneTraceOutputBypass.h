#pragma once

#include <cstddef>

namespace tonetrace::tone
{
class OutputBypassMixer
{
public:
    void prepare(int sampleRate, bool bypassed) noexcept;
    void process(float* const* wetOutput,
                 const float* const* dryInput,
                 std::size_t channels,
                 std::size_t frames,
                 bool bypassed) noexcept;

    [[nodiscard]] double wetMix() const noexcept { return wetMix_; }

private:
    double wetMix_ = 1.0;
    double step_ = 1.0;
};
} // namespace tonetrace::tone
