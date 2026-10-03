#include "tone/ToneTraceNotificationTone.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(double a, double b, double tolerance = 1.0e-9)
{
    return std::abs(a - b) <= tolerance;
}

void expectSweep(int oldLevel, int newLevel, double from, double to, double ms)
{
    const auto sweep = tonetrace::tone::confidenceTransition(oldLevel, newLevel);
    require(sweep.has_value(), "expected confidence sweep was absent");
    require(near(sweep->fromHz, from) && near(sweep->toHz, to)
            && near(sweep->durationMs, ms),
            "confidence sweep did not match released 1.0.5 contract");
}
}

int main()
{
    expectSweep(0, 1, 180.0, 400.0, 200.0);
    expectSweep(1, 2, 220.0, 750.0, 230.0);
    expectSweep(2, 3, 350.0, 1200.0, 340.0);
    expectSweep(3, 2, 1200.0, 350.0, 340.0);
    expectSweep(2, 1, 750.0, 220.0, 250.0);
    require(!tonetrace::tone::confidenceTransition(1, 0).has_value(),
            "drop to zero confidence should not emit a released confidence tone");
    require(!tonetrace::tone::confidenceTransition(2, 2).has_value(),
            "unchanged confidence emitted a tone");

    const auto warning = tonetrace::tone::warningSweep();
    require(near(warning.fromHz, 420.0) && near(warning.toHz, 180.0)
            && near(warning.durationMs, 220.0),
            "warning sweep differs from released 1.0.5");

    constexpr int sampleRate = 48000;
    tonetrace::tone::SweepTone tone;
    tone.start({ 180.0, 400.0, 200.0 }, sampleRate);
    require(tone.active(), "valid sweep did not start");
    require(tone.samplesTotal() == 9600, "200 ms sweep duration is sample-rate incorrect");
    require(near(tone.currentFrequency(), 180.0, 1.0e-6), "sweep start frequency is wrong");

    std::vector<float> left(tone.samplesTotal(), 0.0F);
    std::vector<float> right(tone.samplesTotal(), 0.0F);
    float* channels[] { left.data(), right.data() };
    tone.render(channels, 2, left.size(), sampleRate, 0.25F);
    require(tone.samplesLeft() == 0, "tone sample countdown did not reach zero");
    float tail = 0.0F;
    float* tailChannels[] { &tail };
    tone.render(tailChannels, 1, 1, sampleRate, 0.25F);
    require(!tone.active(), "tone did not retire on the block after its final sample");
    require(std::abs(left.front()) < 1.0e-8F, "attack did not begin at silence");
    require(std::abs(left.back()) < 0.02F, "release did not approach silence");
    require(left == right, "notification tone was not identical across stereo channels");
    const auto peak = *std::max_element(left.begin(), left.end(),
        [](float a, float b) { return std::abs(a) < std::abs(b); });
    require(std::abs(peak) <= 0.2501F, "tone exceeded requested linear gain");

    tone.start({ 400.0, 180.0, 220.0 }, sampleRate);
    tone.stop();
    require(!tone.active() && tone.samplesLeft() == 0, "stop did not clear tone state");

    tone.start({ 100.0, 2000.0, 1200.0 }, sampleRate);
    require(tone.samplesTotal() == 57600, "1.2 second navigation sweep duration is sample-rate incorrect");
    require(near(tone.currentFrequency(), 100.0, 1.0e-6),
            "navigation sweep did not begin at its requested low edge");
    tone.stop();

    std::cout << "PASS: released notification/navigation sweeps, sample-rate timing, stereo render, and fade envelope\n";
    return 0;
}
