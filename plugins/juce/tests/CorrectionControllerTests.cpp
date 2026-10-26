#include "correction/ToneTraceCorrectionController.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
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

tonetrace::AudioBuffer makeProgram(int sampleRate, double seconds,
                                   double lowGain, double highGain)
{
    const auto frames = static_cast<std::size_t>(std::lround(sampleRate * seconds));
    tonetrace::AudioBuffer audio;
    audio.sampleRate = sampleRate;
    audio.channels.assign(2, std::vector<double>(frames, 0.0));
    constexpr double pi = 3.141592653589793238462643383279502884;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double sample = lowGain * std::sin(2.0 * pi * 180.0 * t)
                            + highGain * std::sin(2.0 * pi * 4200.0 * t)
                            + 0.08 * std::sin(2.0 * pi * 900.0 * t);
        audio.channels[0][i] = sample;
        audio.channels[1][i] = sample * 0.97;
    }
    return audio;
}

tonetrace::CaptureDiagnostics diagnosticsFor(const tonetrace::AudioBuffer& audio)
{
    tonetrace::CaptureDiagnostics d;
    d.sampleCount = static_cast<std::uint64_t>(audio.frames() * audio.channels.size());
    double peak = 0.0;
    std::uint64_t clipped = 0;
    for (const auto& channel : audio.channels)
    {
        for (double sample : channel)
        {
            peak = std::max(peak, std::abs(sample));
            if (std::abs(sample) >= 0.999) ++clipped;
        }
    }
    d.peakAbsolute = peak;
    d.clippedSamples = clipped;
    return d;
}
}

int main()
{
    using namespace tonetrace;
    using namespace tonetrace::jucebridge;

    constexpr int rate = 48000;
    CorrectionController controller;
    require(controller.prepare(rate, 2).accepted, "controller prepare failed");
    require(!controller.hasProfile(), "fresh controller unexpectedly has a profile");

    // Manual-only EQ must be installable before any learned profile and must
    // remain distinct from hasProfile(), because project state persists its
    // editable grid in the JUCE wrapper rather than inventing a fake match.
    CorrectionSettings manualSettings;
    manualSettings.resolution = 3;
    manualSettings.manualGains = { 0.0, 6.0, 0.0 };
    require(controller.commitManualCorrection(manualSettings).accepted,
            "manual-only correction install failed");
    require(!controller.hasProfile(),
            "manual-only correction incorrectly created a learned profile");
    std::vector<float> manualLeft(512, 0.0F), manualRight(512, 0.0F);
    manualLeft[0] = manualRight[0] = 0.25F;
    const float* manualIn[2] { manualLeft.data(), manualRight.data() };
    float* manualOut[2] { manualLeft.data(), manualRight.data() };
    controller.process(manualIn, manualOut, 2, manualLeft.size());
    require(std::all_of(manualLeft.begin(), manualLeft.end(), [](float x) { return std::isfinite(x); }),
            "manual-only correction produced non-finite audio");

    const auto reference = makeProgram(rate, 3.0, 0.18, 0.05);
    const auto target = makeProgram(rate, 3.0, 0.07, 0.17);
    CorrectionSettings settings;
    settings.mode = MatchMode::FullMix;
    settings.resolution = 30;
    settings.maximumCorrectionDb = 18.0;
    settings.correctionSharpness = 1.0;

    const auto result = controller.analyze(reference, target,
                                           diagnosticsFor(reference),
                                           diagnosticsFor(target), settings);
    require(result.accepted, "released engine analysis was rejected through controller");
    require(controller.hasProfile(), "successful analysis did not install a profile");
    require(!controller.inProgressTargetCopy().has_value(),
            "completed analysis left its Target reporting window open");

    const auto saved = controller.saveProjectState();
    require(saved.find("phase frozen") != std::string::npos,
            "saved controller state did not contain a frozen profile");

    // Malformed restore is atomic: validation failure may not alter the live
    // profile or the independently cached last-known-good project state.
    const auto malformed = controller.restoreProjectState("not a Tone Trace project state");
    require(!malformed.accepted, "malformed project state was accepted");
    require(controller.hasProfile(), "malformed restore cleared the live profile");
    require(controller.saveProjectState() == saved,
            "malformed restore changed the cached last-known-good state");

    // Reset uses the same click-safe released core transition as Ready-state
    // restore, clears learned state, and refreshes the controller's cached state.
    require(controller.reset().accepted, "controller reset failed");
    require(!controller.hasProfile(), "controller reset left a learned profile active");
    require(controller.saveProjectState() == serializeProjectState(nullptr),
            "controller reset did not publish Ready state");
    require(!controller.inProgressTargetCopy().has_value(),
            "controller reset left a stale Target reporting spectrum");

    // Reinstall a learned profile so rebuild/restore tests below still exercise
    // the same post-analysis path after Reset.
    require(controller.analyze(reference, target,
                               diagnosticsFor(reference), diagnosticsFor(target),
                               settings).accepted,
            "analysis after controller reset failed");

    // Imported Target analysis uses the same worker/controller path as live
    // capture. Neither imported spectrum should require raw audio to exist.
    MatchEngine importedEngine;
    MatchSettings importedMatch;
    importedMatch.mode = MatchMode::FullMix;
    importedMatch.rangeLowHz = 10.0;
    importedMatch.rangeHighHz = 30000.0;
    importedMatch.resolution = settings.resolution;
    importedMatch.maximumCorrectionDb = 60.0;
    const auto importedReference = importedEngine.capture(reference, importedMatch);
    const auto importedTarget = importedEngine.capture(target, importedMatch);
    AudioBuffer noRawAudio;
    require(controller.analyze(noRawAudio, noRawAudio, {}, {}, settings,
                               &importedReference, &importedTarget).accepted,
            "imported Reference/Target analysis did not use the shared controller path");
    require(!controller.inProgressTargetCopy().has_value(),
            "completed imported analysis left its Target reporting window open");

    settings.correctionSharpness = 1.25;
    require(controller.rebuild(settings).accepted,
            "post-match sharpness rebuild failed");
    require(!controller.inProgressTargetCopy().has_value(),
            "profile rebuild left a stale in-progress Target report");
    settings.mode = MatchMode::Voice;
    require(controller.rebuild(settings).accepted,
            "retained-spectrum Match Mode reinterpretation failed");

    CorrectionController restored;
    require(restored.prepare(44100, 2).accepted, "restore controller prepare failed");
    const auto restoredResult = restored.restoreProjectState(saved);
    require(restoredResult.accepted && restored.hasProfile(),
            "frozen profile did not restore at a different sample rate");

    std::vector<float> left(257, 0.0F), right(257, 0.0F);
    left[0] = 0.25F;
    right[0] = -0.25F;
    const float* in[2] { left.data(), right.data() };
    float* out[2] { left.data(), right.data() };
    restored.process(in, out, 2, left.size());
    require(std::all_of(left.begin(), left.end(), [](float x) { return std::isfinite(x); })
            && std::all_of(right.begin(), right.end(), [](float x) { return std::isfinite(x); }),
            "restored controller produced non-finite audio");

    const auto beforeAudioReset = restored.saveProjectState();
    restored.resetAudio();
    std::fill(left.begin(), left.end(), 0.0F);
    std::fill(right.begin(), right.end(), 0.0F);
    restored.process(in, out, 2, left.size());
    require(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.0F; })
            && std::all_of(right.begin(), right.end(), [](float x) { return x == 0.0F; }),
            "host reset leaked old convolution audio");
    require(restored.hasProfile() && restored.saveProjectState() == beforeAudioReset,
            "host reset discarded the saved correction");

    const auto ready = serializeProjectState(nullptr);
    require(restored.restoreProjectState(ready).accepted,
            "Ready-state restore failed");
    require(!restored.hasProfile(), "Ready-state restore left a profile active");
    require(restored.saveProjectState() == ready,
            "Ready-state restore did not refresh the cached state");

    // Saving state must be independent of long analysis ownership. The cache is
    // allowed to return the previous complete state until the new profile commits,
    // but it must never expose a partial serialization or wait on MatchEngine.
    CorrectionController concurrent;
    require(concurrent.prepare(rate, 2).accepted, "concurrent controller prepare failed");
    const auto longReference = makeProgram(rate, 10.0, 0.18, 0.05);
    const auto longTarget = makeProgram(rate, 10.0, 0.07, 0.17);
    std::atomic<bool> analysisStarted { false };
    std::atomic<bool> analysisDone { false };
    CorrectionResult concurrentResult;
    std::thread analysis([&]
    {
        analysisStarted.store(true, std::memory_order_release);
        concurrentResult = concurrent.analyze(
            longReference, longTarget, diagnosticsFor(longReference),
            diagnosticsFor(longTarget), settings);
        analysisDone.store(true, std::memory_order_release);
    });

    while (!analysisStarted.load(std::memory_order_acquire)) std::this_thread::yield();
    std::size_t saveCount = 0;
    bool sawTargetReport = false;
    do
    {
        if (const auto report = concurrent.inProgressTargetCopy(); report.has_value())
        {
            require(!report->points.empty(),
                    "in-progress Target reporting spectrum contained no points");
            sawTargetReport = true;
        }

        const auto state = concurrent.saveProjectState();
        try
        {
            const auto parsed = deserializeProjectState(state);
            require(parsed.phase == WorkflowPhase::Ready
                        || parsed.phase == WorkflowPhase::Frozen,
                    "concurrent save returned an invalid workflow phase");
            if (parsed.phase == WorkflowPhase::Frozen)
                require(parsed.snapshot != nullptr,
                        "concurrent Frozen save returned no complete snapshot");
        }
        catch (...)
        {
            require(false, "concurrent save returned a partial/unparseable state");
        }
        ++saveCount;
    } while (!analysisDone.load(std::memory_order_acquire));

    analysis.join();
    require(concurrentResult.accepted, "concurrent analysis failed");
    require(saveCount > 0, "concurrent state-save test did not execute");
    require(sawTargetReport,
            "long Correct never exposed its Target spectrum during analysis");
    require(!concurrent.inProgressTargetCopy().has_value(),
            "completed Correct left the transient Target reporting window open");
    const auto finalConcurrentState = deserializeProjectState(concurrent.saveProjectState());
    require(finalConcurrentState.phase == WorkflowPhase::Frozen
                && finalConcurrentState.snapshot != nullptr,
            "completed concurrent analysis was not published to the state cache");

    std::cout << "Tone Trace Slice 03 correction-controller tests passed\n";
    return 0;
}
