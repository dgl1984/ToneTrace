#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace tonetrace::tone
{
struct SweepSpec
{
    double fromHz = 0.0;
    double toHz = 0.0;
    double durationMs = 0.0;
};

[[nodiscard]] constexpr SweepSpec warningSweep() noexcept
{
    return { 420.0, 180.0, 220.0 };
}

[[nodiscard]] std::optional<SweepSpec> confidenceTransition(int oldLevel, int newLevel) noexcept;

class SweepTone
{
public:
    void stop() noexcept;
    void start(const SweepSpec& spec, int sampleRate) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] double currentFrequency() const noexcept;
    [[nodiscard]] std::uint64_t samplesLeft() const noexcept { return samplesLeft_; }
    [[nodiscard]] std::uint64_t samplesTotal() const noexcept { return samplesTotal_; }

    // Adds the generated tone to an already-processed audio buffer. No
    // allocation, lock, host call, or UI work occurs here.
    void render(float* const* output,
                std::size_t channels,
                std::size_t count,
                int sampleRate,
                float linearGain) noexcept;

private:
    bool active_ = false;
    double phase_ = 0.0;
    double frequencyStart_ = 0.0;
    double frequencyEnd_ = 0.0;
    std::uint64_t samplesLeft_ = 0;
    std::uint64_t samplesTotal_ = 0;
};

} // namespace tonetrace::tone
