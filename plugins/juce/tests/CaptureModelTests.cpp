#include "capture/ToneTraceCaptureModel.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace tc = tonetrace::capture;

namespace
{
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<float> sine(int sampleRate, double seconds, double frequency = 440.0, float amplitude = 0.25F)
{
    const auto frames = static_cast<std::size_t>(std::ceil(sampleRate * seconds));
    std::vector<float> result(frames);
    constexpr double twoPi = 6.283185307179586476925286766559;
    for (std::size_t i = 0; i < frames; ++i)
        result[i] = amplitude * static_cast<float>(std::sin(twoPi * frequency * static_cast<double>(i) / sampleRate));
    return result;
}

void appendMono(tc::CaptureBuffer& capture,
                const std::vector<float>& audio,
                int sampleRate,
                tc::MatchMode mode,
                std::size_t block = 256)
{
    for (std::size_t offset = 0; offset < audio.size(); offset += block)
    {
        const auto count = std::min(block, audio.size() - offset);
        const float* input[1] { audio.data() + offset };
        capture.append(input, 1, count, sampleRate, mode);
    }
}
}

void testHostLifecycleCapturePreservationPolicy()
{
    using namespace tonetrace::capture;

    check(shouldPreserveCaptureAcrossReprepare(
                WorkflowPhase::CapturingReference, 48000, 48000, true),
            "same-rate Reference capture was not preserved across reprepare");
    check(shouldPreserveCaptureAcrossReprepare(
                WorkflowPhase::CapturingTarget, 44100, 44100, true),
            "same-rate Target capture was not preserved across reprepare");
    check(!shouldPreserveCaptureAcrossReprepare(
                WorkflowPhase::CapturingReference, 48000, 44100, true),
            "sample-rate change incorrectly preserved raw capture");
    check(!shouldPreserveCaptureAcrossReprepare(
                WorkflowPhase::Ready, 48000, 48000, true),
            "Ready state incorrectly treated as live capture");

    check(shouldPreserveCaptureForEquivalentStateEcho(
                WorkflowPhase::CapturingReference, true, true),
            "equivalent host state echo erased Reference capture");
    check(shouldPreserveCaptureForEquivalentStateEcho(
                WorkflowPhase::CapturingTarget, true, true),
            "equivalent host state echo erased Target capture");
    check(!shouldPreserveCaptureForEquivalentStateEcho(
                WorkflowPhase::CapturingReference, false, true),
            "different parameter state was incorrectly ignored");
    check(!shouldPreserveCaptureForEquivalentStateEcho(
                WorkflowPhase::CapturingReference, true, false),
            "different core state was incorrectly ignored");
}

int main()
{
    testHostLifecycleCapturePreservationPolicy();
    constexpr int sampleRate = 48000;

    check(tc::maximumCaptureSeconds(tc::MatchMode::Voice) == 30.0,
          "standard modes must retain 30-second accepted-audio capacity");
    check(tc::maximumCaptureSeconds(tc::MatchMode::CustomMaxCapture) == 60.0,
          "Custom Max Capture must retain 60-second accepted-audio capacity");

    check(tc::workflowStepFromHostValue(1.007, 1) == 2,
          "small positive REAPER/OSARA fractional enum move must mean next workflow step");
    check(tc::workflowStepFromHostValue(1.993, 2) == 1,
          "small negative REAPER/OSARA fractional enum move must mean previous workflow step");
    check(tc::workflowStepFromHostValue(2.0, 1) == 2,
          "exact workflow integer must remain exact");
    check(tc::workflowStepFromHostValue(std::numeric_limits<double>::quiet_NaN(), 3) == 3,
          "non-finite workflow request must preserve the previous step");

    tc::CaptureBuffer silence;
    silence.prepare(sampleRate, tc::kStandardCaptureSeconds);
    std::vector<float> silentAudio(static_cast<std::size_t>(sampleRate), 0.0F);
    appendMono(silence, silentAudio, sampleRate, tc::MatchMode::Voice);
    check(silence.validFrames == 0, "silence below the Voice gate must not count as accepted audio");
    check(silence.validSeconds(sampleRate) == 0.0,
          "Capture Time must measure accepted audio rather than wall-clock input time");
    check(tc::captureStatus(tc::WorkflowPhase::CapturingReference, 0, silence, sampleRate)
              == tc::Status::CapturingReference,
          "empty Reference capture must report no valid audio");

    tc::CaptureBuffer reference;
    reference.prepare(sampleRate, tc::kStandardCaptureSeconds);
    const auto referenceAudio = sine(sampleRate, 0.60, 440.0);
    appendMono(reference, referenceAudio, sampleRate, tc::MatchMode::Voice);
    check(reference.validSeconds(sampleRate) >= 0.35,
          "above-gate source must accumulate accepted Capture Time");
    check(reference.updateCounter >= 3,
          "Reference readiness must have at least three spectral monitor updates");
    check(reference.readyForSave(sampleRate),
          "Reference must become saveable at 0.35 seconds plus three monitor updates even before a confidence tier");
    check(reference.frames == reference.validFrames,
          "retained capture frames and accepted frame count must stay aligned");
    check(reference.profileDiagnostics().sampleCount == reference.acceptedSampleCount,
          "profile diagnostics must describe accepted samples, not rejected wall-clock input");

    tc::CaptureBuffer target;
    target.prepare(sampleRate, tc::kStandardCaptureSeconds);
    const auto stableTarget = sine(sampleRate, 3.2, 220.0);
    appendMono(target, stableTarget, sampleRate, tc::MatchMode::Drums);
    check(target.validSeconds(sampleRate) > 1.5,
          "Target test material must pass the Drums low-confidence time threshold");
    check(target.confidenceLevel >= 1,
          "stable accepted Target material must eventually reach at least low confidence");

    tc::CaptureBuffer tiny;
    tiny.prepare(sampleRate, 0.02);
    const auto tooLong = sine(sampleRate, 0.10, 1000.0, 0.4F);
    appendMono(tiny, tooLong, sampleRate, tc::MatchMode::FullMix, 64);
    check(tiny.overflowed, "capture must report full rather than writing beyond preallocated capacity");
    check(tiny.frames <= tiny.capacityFrames, "capture must never exceed its preallocated capacity");

    tc::CaptureDiagnostics diagnostics;
    std::array<float, 4> contaminated { 0.2F, 1.0F,
        std::numeric_limits<float>::quiet_NaN(), -0.5F };
    const float* contaminatedInput[1] { contaminated.data() };
    diagnostics.observe(contaminatedInput, 1, contaminated.size());
    check(diagnostics.sampleCount == 4, "diagnostics must count received samples");
    check(diagnostics.nonFiniteSamples == 1, "diagnostics must count non-finite samples");
    check(diagnostics.clippedSamples == 1, "diagnostics must count samples at or beyond the released 0.999 clip threshold");

    check(std::string(tc::statusText(tc::Status::CannotSaveYet)) ==
              "Cannot save yet; keep capturing",
          "released Cannot Save status wording must remain exact");
    check(std::string(tc::workflowText(2)) == "Save Reference and Learn Target",
          "released Workflow Step 2 wording must remain exact");

    if (failures != 0)
    {
        std::cerr << failures << " capture-model test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Tone Trace Capture Model Slice 02 tests passed\n";
    return EXIT_SUCCESS;
}
