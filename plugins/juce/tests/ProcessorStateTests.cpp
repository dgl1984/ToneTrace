#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <sstream>

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

bool near(double a, double b, double tolerance = 1.0e-4)
{
    return std::abs(a - b) <= tolerance;
}

bool sameSpectrum(const tonetrace::SpectrumCapture& a, const tonetrace::SpectrumCapture& b)
{
    if (a.sampleRate != b.sampleRate || a.fftSize != b.fftSize
        || a.acceptedFrames != b.acceptedFrames || !near(a.confidence, b.confidence, 1.0e-12)
        || a.points.size() != b.points.size())
        return false;

    for (std::size_t index = 0; index < a.points.size(); ++index)
    {
        const auto& left = a.points[index];
        const auto& right = b.points[index];
        if (!near(left.frequencyHz, right.frequencyHz, 1.0e-12)
            || !near(left.levelDb, right.levelDb, 1.0e-12)
            || !near(left.confidence, right.confidence, 1.0e-12)
            || !near(left.varianceDb2, right.varianceDb2, 1.0e-12))
            return false;
    }
    return true;
}

bool sameModel(const tonetrace::CorrectionModel& a, const tonetrace::CorrectionModel& b)
{
    if (a.version != b.version || a.mode != b.mode
        || !near(a.analysisLowHz, b.analysisLowHz, 1.0e-12)
        || !near(a.analysisHighHz, b.analysisHighHz, 1.0e-12)
        || a.resolution != b.resolution || a.nodes.size() != b.nodes.size())
        return false;

    for (std::size_t index = 0; index < a.nodes.size(); ++index)
    {
        const auto& left = a.nodes[index];
        const auto& right = b.nodes[index];
        if (!near(left.frequencyHz, right.frequencyHz, 1.0e-12)
            || !near(left.gainDb, right.gainDb, 1.0e-12)
            || !near(left.confidence, right.confidence, 1.0e-12))
            return false;
    }
    return true;
}

bool sameDiagnostics(const tonetrace::CaptureDiagnostics& a,
                     const tonetrace::CaptureDiagnostics& b)
{
    return a.sampleCount == b.sampleCount
        && a.nonFiniteSamples == b.nonFiniteSamples
        && a.clippedSamples == b.clippedSamples
        && near(a.peakAbsolute, b.peakAbsolute, 1.0e-12);
}

bool sameFrozenCorrection(const tonetrace::ProfileSnapshot& a,
                          const tonetrace::ProfileSnapshot& b)
{
    if (!sameSpectrum(a.reference, b.reference) || !sameSpectrum(a.target, b.target)
        || !sameModel(a.uncappedModel, b.uncappedModel)
        || a.matchSettings.mode != b.matchSettings.mode
        || !near(a.matchSettings.rangeLowHz, b.matchSettings.rangeLowHz, 1.0e-12)
        || !near(a.matchSettings.rangeHighHz, b.matchSettings.rangeHighHz, 1.0e-12)
        || !near(a.matchSettings.maximumCorrectionDb, b.matchSettings.maximumCorrectionDb, 1.0e-12)
        || a.matchSettings.resolution != b.matchSettings.resolution
        || a.matchSettings.removeBroadLevelDifference != b.matchSettings.removeBroadLevelDifference
        || a.renderSettings.sampleRate != b.renderSettings.sampleRate
        || !near(a.renderSettings.durationSeconds, b.renderSettings.durationSeconds, 1.0e-12)
        || !near(a.renderSettings.correctionStrength, b.renderSettings.correctionStrength, 1.0e-12)
        || !near(a.renderSettings.correctionSharpness, b.renderSettings.correctionSharpness, 1.0e-12)
        || !near(a.renderSettings.correctionGainDb, b.renderSettings.correctionGainDb, 1.0e-12)
        || !near(a.renderSettings.rangeLowHz, b.renderSettings.rangeLowHz, 1.0e-12)
        || !near(a.renderSettings.rangeHighHz, b.renderSettings.rangeHighHz, 1.0e-12)
        || a.renderSettings.manualGains.size() != b.renderSettings.manualGains.size()
        || !sameDiagnostics(a.referenceDiagnostics, b.referenceDiagnostics)
        || !sameDiagnostics(a.targetDiagnostics, b.targetDiagnostics))
        return false;

    for (std::size_t index = 0; index < a.renderSettings.manualGains.size(); ++index)
        if (!near(a.renderSettings.manualGains[index], b.renderSettings.manualGains[index], 1.0e-12))
            return false;
    return true;
}

bool sameAudio(const tonetrace::AudioBuffer& a, const tonetrace::AudioBuffer& b)
{
    if (a.sampleRate != b.sampleRate || a.channels.size() != b.channels.size()) return false;
    for (std::size_t channel = 0; channel < a.channels.size(); ++channel)
    {
        if (a.channels[channel].size() != b.channels[channel].size()) return false;
        for (std::size_t frame = 0; frame < a.channels[channel].size(); ++frame)
            if (!near(a.channels[channel][frame], b.channels[channel][frame], 1.0e-12)) return false;
    }
    return true;
}

struct ExpectedState
{
    std::vector<double> continuous;
    int matchMode = 0;
    std::vector<bool> toggles;
    std::vector<double> manualGains;
};

ExpectedState captureState(const ToneTraceProcessor& processor)
{
    ExpectedState state;
    for (std::size_t index = 0; index < processor.continuousParameterCount(); ++index)
        state.continuous.push_back(processor.continuousValue(index));
    state.matchMode = processor.matchModeIndex();
    for (std::size_t index = 0; index < processor.toggleParameterCount(); ++index)
        state.toggles.push_back(processor.toggleValue(index));
    state.manualGains = processor.manualGainsCopy();
    return state;
}

void expectState(const ToneTraceProcessor& processor, const ExpectedState& expected, const char* stage)
{
    require(processor.continuousParameterCount() == expected.continuous.size(), "continuous parameter count changed");
    for (std::size_t index = 0; index < expected.continuous.size(); ++index)
    {
        if (!near(processor.continuousValue(index), expected.continuous[index]))
        {
            std::cerr << "FAIL: " << stage << " continuous parameter " << index
                      << " expected " << expected.continuous[index]
                      << " got " << processor.continuousValue(index) << '\n';
            std::exit(1);
        }
    }

    if (processor.matchModeIndex() != expected.matchMode)
    {
        std::cerr << "FAIL: " << stage << " Match Mode expected " << expected.matchMode
                  << " got " << processor.matchModeIndex() << '\n';
        std::exit(1);
    }

    require(processor.toggleParameterCount() == expected.toggles.size(), "toggle parameter count changed");
    for (std::size_t index = 0; index < expected.toggles.size(); ++index)
    {
        if (processor.toggleValue(index) != expected.toggles[index])
        {
            std::cerr << "FAIL: " << stage << " toggle " << index << " did not round-trip\n";
            std::exit(1);
        }
    }

    const auto actualManual = processor.manualGainsCopy();
    if (actualManual.size() != expected.manualGains.size())
    {
        std::cerr << "FAIL: " << stage << " manual band count expected " << expected.manualGains.size()
                  << " got " << actualManual.size() << '\n';
        std::exit(1);
    }
    for (std::size_t index = 0; index < expected.manualGains.size(); ++index)
    {
        if (!near(actualManual[index], expected.manualGains[index], 1.0e-6))
        {
            std::cerr << "FAIL: " << stage << " manual band " << index
                      << " expected " << expected.manualGains[index]
                      << " got " << actualManual[index] << '\n';
            std::exit(1);
        }
    }

}

double maximumAbsoluteSample(const juce::AudioBuffer<float>& buffer)
{
    double maximum = 0.0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            maximum = std::max(maximum, std::abs(static_cast<double>(buffer.getSample(channel, sample))));
    return maximum;
}

tonetrace::CorrectionModel flatFrozenModel()
{
    tonetrace::CorrectionModel model;
    model.mode = tonetrace::MatchMode::Voice;
    model.analysisLowHz = 20.0;
    model.analysisHighHz = 20000.0;
    model.resolution = 30;
    model.nodes = {
        { 20.0, 0.0, 1.0 },
        { 1000.0, 0.0, 1.0 },
        { 20000.0, 0.0, 1.0 },
    };
    return model;
}

tonetrace::SpectrumCapture makeSpectrum(double lowHz, double midHz, double highHz,
                                         double lowDb, double midDb, double highDb)
{
    tonetrace::SpectrumCapture capture;
    capture.sampleRate = 48000;
    capture.fftSize = 4096;
    capture.acceptedFrames = 42;
    capture.confidence = 0.9;
    capture.points = {
        { lowHz, lowDb, 0.9, 0.0 },
        { midHz, midDb, 0.9, 0.0 },
        { highHz, highDb, 0.9, 0.0 },
    };
    return capture;
}

tonetrace::CorrectionModel narrowModel(double lowHz, double midHz, double highHz)
{
    tonetrace::CorrectionModel model;
    model.mode = tonetrace::MatchMode::Voice;
    model.analysisLowHz = lowHz;
    model.analysisHighHz = highHz;
    model.resolution = 30;
    model.nodes = {
        { lowHz, 0.0, 1.0 },
        { midHz, 1.0, 1.0 },
        { highHz, 0.0, 1.0 },
    };
    return model;
}

void testImportedReferenceWithLiveTarget()
{
    ToneTraceProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    processor.setToggleFromUi(tonetrace::product::kToneNotificationsIndex, false);
    processor.setMatchModeFromUi(0);
    tonetrace::AudioBuffer referenceAudio;
    referenceAudio.sampleRate = 48000;
    referenceAudio.channels.assign(2, std::vector<double>(144000));
    for (std::size_t i = 0; i < referenceAudio.frames(); ++i)
    {
        const double t = static_cast<double>(i) / 48000.0;
        const double x = 0.18 * std::sin(2.0 * juce::MathConstants<double>::pi * 180.0 * t)
                       + 0.05 * std::sin(2.0 * juce::MathConstants<double>::pi * 4200.0 * t)
                       + 0.08 * std::sin(2.0 * juce::MathConstants<double>::pi * 900.0 * t);
        referenceAudio.channels[0][i] = referenceAudio.channels[1][i] = x;
    }
    tonetrace::MatchSettings settings;
    settings.mode = tonetrace::MatchMode::FullMix;
    tonetrace::MatchEngine engine;
    const auto reference = engine.capture(referenceAudio, settings);
    require(processor.importReference(reference).ok, "Reference import failed");
    juce::AudioBuffer<float> block(2, 512);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < 144000; frame += 512)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const double t = static_cast<double>(frame + i) / 48000.0;
            const auto x = static_cast<float>(0.07 * std::sin(2.0 * juce::MathConstants<double>::pi * 180.0 * t)
                         + 0.17 * std::sin(2.0 * juce::MathConstants<double>::pi * 4200.0 * t)
                         + 0.08 * std::sin(2.0 * juce::MathConstants<double>::pi * 900.0 * t));
            block.setSample(0, i, x);
            block.setSample(1, i, x);
        }
        processor.processBlock(block, midi);
    }
    processor.setWorkflowFromUi(3);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (processor.statusCode() == static_cast<int>(tonetrace::capture::Status::Analyzing)
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(processor.profileActive(), "imported Reference plus live Target did not create a correction");
    const auto snapshot = processor.profileSnapshotCopy();
    require(snapshot != nullptr && snapshot->reference.points.size() == reference.points.size(),
            "live Target analysis did not retain the imported Reference");
    processor.setWorkflowFromUi(4);
    require(processor.statusCode() == static_cast<int>(tonetrace::capture::Status::Frozen),
            "imported Reference workflow did not Freeze");

    // Exercise non-zero per-band edits inside a real learned profile, not only
    // the manual-only state test below. The rebuild is asynchronous, so wait
    // until the committed snapshot carries the same manual curve owned by the UI.
    const auto firstBandBefore = processor.bandValueDb(0);
    const auto middleBand = static_cast<std::size_t>(processor.bandCount() / 2);
    const auto middleBandBefore = processor.bandValueDb(middleBand);
    processor.setBandValueDb(0, firstBandBefore + 1.25);
    processor.setBandValueDb(middleBand, middleBandBefore - 0.75);
    const auto manualDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;)
    {
        const auto currentManual = processor.manualGainsCopy();
        const auto currentSnapshot = processor.profileSnapshotCopy();
        bool committed = currentSnapshot != nullptr
            && currentSnapshot->renderSettings.manualGains.size() == currentManual.size();
        for (std::size_t index = 0; committed && index < currentManual.size(); ++index)
            committed = near(currentSnapshot->renderSettings.manualGains[index],
                             currentManual[index], 1.0e-9);
        if (committed) break;
        require(std::chrono::steady_clock::now() < manualDeadline,
                "manual correction rebuild did not commit before project-state save");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Correction Gain belongs to the JUCE output stage rather than the stored
    // released kernel, but it is still part of the correction and therefore must
    // survive host project state alongside the full frozen profile payload.
    processor.setContinuousFromUi(tonetrace::product::kCorrectionGainIndex, 2.375);
    const auto savedControls = captureState(processor);
    const auto savedSnapshot = processor.profileSnapshotCopy();
    require(savedSnapshot != nullptr, "Frozen correction snapshot disappeared before save");
    const auto ir = processor.exportableImpulseResponse();
    require(ir.has_value() && ir->frames() > 0, "completed correction could not export an IR");

    juce::MemoryBlock state;
    processor.getStateInformation(state);

    // REAPER/VST3 state must be self-contained: a same-rate restore gets back
    // every datum needed to reproduce/export the correction without any .tts,
    // .ttm or IR sidecar file. Compare the complete released snapshot as well
    // as the JUCE-owned controls and the rendered impulse response.
    ToneTraceProcessor exactRestored;
    require(exactRestored.restoreState(state.getData(), static_cast<int>(state.getSize())),
            "valid Frozen state rejected before prepareToPlay");
    exactRestored.prepareToPlay(48000, 512);
    require(exactRestored.profileActive(), "same-rate Frozen profile did not restore");
    expectState(exactRestored, savedControls, "complete Frozen correction restore");
    const auto exactSnapshot = exactRestored.profileSnapshotCopy();
    require(exactSnapshot != nullptr && sameFrozenCorrection(*savedSnapshot, *exactSnapshot),
            "REAPER project state did not preserve the complete Frozen correction payload");
    const auto exactIr = exactRestored.exportableImpulseResponse();
    require(exactIr.has_value() && sameAudio(*ir, *exactIr),
            "REAPER project state changed the rendered correction after restore");
    exactRestored.releaseResources();

    ToneTraceProcessor restored;
    require(restored.restoreState(state.getData(), static_cast<int>(state.getSize())), "valid Frozen state rejected");
    restored.prepareToPlay(44100, 64);
    require(restored.profileActive(), "Frozen profile did not survive a sample-rate change on restore");
    const auto before = captureState(restored);
    require(!restored.restoreState("invalid state", 13), "invalid state accepted");
    expectState(restored, before, "rejected state");

    // A project restore owns a frozen profile, not a live transfer session.
    // Released 1.0.5 clears imported Reference/Target eligibility on state load,
    // so a standalone Target import must still require a fresh/imported Reference.
    const auto restoredTarget = makeSpectrum(100.0, 1000.0, 10000.0, -1.0, 2.0, -1.0);
    require(!restored.importTarget(restoredTarget).ok,
            "project restore incorrectly preserved imported Reference eligibility");

    // Genuine 1.0.5 wrapper format, including global gain in the old kernel.
    auto legacyProfile = *snapshot;
    legacyProfile.renderSettings.correctionGainDb = 2.375;
    const auto core = tonetrace::serializeProjectState(&legacyProfile);
    std::ostringstream legacy;
    legacy << "ToneTraceClapState 3\nparameters 2\nparameter 180 2.375\nparameter 110 0\n"
           << "tracedisplay 0\nmanualgains 0\ncore " << core.size() << '\n' << core;
    ToneTraceProcessor migrated;
    const auto legacyBytes = legacy.str();
    require(migrated.restoreState(legacyBytes.data(), static_cast<int>(legacyBytes.size())), "1.0.5 CLAP state rejected");
    migrated.prepareToPlay(48000, 512);
    require(migrated.profileActive() && near(migrated.continuousValue(tonetrace::product::kCorrectionGainIndex), 2.375),
            "legacy correction or gain was lost");
    require(near(migrated.profileSnapshotCopy()->renderSettings.correctionGainDb, 0.0),
            "legacy global gain would be applied twice");

    // Start a fresh live Reference after the import, then use it for a new match.
    processor.setWorkflowFromUi(1);
    for (int frame = 0; frame < 144000; frame += 512)
    {
        for (int i = 0; i < 512; ++i)
            for (int channel = 0; channel < 2; ++channel)
                block.setSample(channel, i, static_cast<float>(referenceAudio.channels[0][static_cast<std::size_t>(frame + i) % referenceAudio.frames()]));
        processor.processBlock(block, midi);
    }
    const auto acceptedSeconds = processor.captureSeconds();
    processor.reset();
    processor.releaseResources();
    processor.prepareToPlay(48000, 1024);
    require(near(processor.captureSeconds(), acceptedSeconds), "transport stop discarded a live Reference");
    processor.setWorkflowFromUi(2);
    require(processor.exportableReference().has_value(), "new live Reference was not exportable before Target");
    for (int frame = 0; frame < 144000; frame += 512)
    {
        for (int i = 0; i < 512; ++i)
            for (int channel = 0; channel < 2; ++channel)
                block.setSample(channel, i, static_cast<float>(referenceAudio.channels[0][static_cast<std::size_t>(frame + i) % referenceAudio.frames()]));
        processor.processBlock(block, midi);
    }
    processor.setWorkflowFromUi(3);
    const auto liveDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (processor.statusCode() == static_cast<int>(tonetrace::capture::Status::Analyzing)
           && std::chrono::steady_clock::now() < liveDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(processor.statusCode() == static_cast<int>(tonetrace::capture::Status::Preview), "live Reference/Target workflow failed");
    processor.setWorkflowFromUi(4);
    processor.setWorkflowFromUi(5);
    processor.setWorkflowFromUi(7);
    require(processor.statusCode() == static_cast<int>(tonetrace::capture::Status::Frozen), "Cancel Reset discarded Frozen correction");
    processor.setWorkflowFromUi(5);
    processor.setWorkflowFromUi(6);
    require(!processor.profileActive(), "confirmed Reset retained the learned correction");
    processor.releaseResources();
}

void testBandDisplaySnapshotConsistency()
{
    ToneTraceProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    processor.setBandValueDb(0, 3.25);
    processor.setBandValueDb(static_cast<std::size_t>(processor.bandCount() / 2), -2.5);

    const auto bands = processor.bandDisplayState();
    require(bands.frequenciesHz.size() == static_cast<std::size_t>(processor.bandCount()),
            "band display snapshot frequency count changed");
    require(bands.automaticDb.size() == bands.frequenciesHz.size()
                && bands.finalDb.size() == bands.frequenciesHz.size(),
            "band display snapshot vectors disagree");
    for (std::size_t index = 0; index < bands.frequenciesHz.size(); ++index)
    {
        require(near(bands.frequenciesHz[index], processor.bandFrequencyHz(index), 1.0e-8),
                "band display snapshot frequency disagrees with processor");
        require(near(bands.automaticDb[index], processor.bandAutomaticDb(index), 1.0e-8),
                "band display snapshot automatic value disagrees with processor");
        require(near(bands.finalDb[index], processor.bandValueDb(index), 1.0e-8),
                "band display snapshot final value disagrees with processor");
    }
    require(near(bands.limitDb, processor.bandLimitDb(), 1.0e-8),
            "band display snapshot limit disagrees with processor");
    processor.releaseResources();
}

void testTransferParity()
{
    using namespace tonetrace::product;

    // The released CLAP checks imports against the user's active correction
    // range, not a hard-coded 10 Hz..30 kHz window.
    ToneTraceProcessor rangeGuard;
    rangeGuard.prepareToPlay(48000.0, 512);
    rangeGuard.setContinuousFromUi(kCorrectionRangeLowIndex, 20.0);
    rangeGuard.setContinuousFromUi(kCorrectionRangeHighIndex, 500.0);
    const auto outOfRangeCurve = makeSpectrum(1000.0, 1500.0, 2000.0, 0.0, 1.0, 0.0);
    require(!rangeGuard.importReference(outOfRangeCurve).ok,
            "Reference import ignored the active correction range");
    require(!rangeGuard.importModel(narrowModel(1000.0, 1500.0, 2000.0)).ok,
            "model import ignored the active correction range");
    rangeGuard.releaseResources();

    // Importing a Reference is a new Target-capture transaction and must clear
    // stale live telemetry exactly as 1.0.5 does.
    ToneTraceProcessor telemetry;
    telemetry.prepareToPlay(48000.0, 512);
    telemetry.setToggleFromUi(kToneNotificationsIndex, false);
    telemetry.setWorkflowFromUi(1);
    juce::AudioBuffer<float> block(2, 512);
    juce::MidiBuffer midi;
    for (int frame = 0; frame < 48000; frame += 512)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const double t = static_cast<double>(frame + i) / 48000.0;
            const auto x = static_cast<float>(0.2 * std::sin(
                2.0 * juce::MathConstants<double>::pi * 440.0 * t));
            block.setSample(0, i, x);
            block.setSample(1, i, x);
        }
        telemetry.processBlock(block, midi);
    }
    require(telemetry.captureSeconds() > 0.0, "live Reference did not publish capture telemetry");
    const auto reference = makeSpectrum(100.0, 1000.0, 10000.0, 0.0, 0.0, 0.0);
    require(telemetry.importReference(reference).ok, "valid Reference import failed");
    require(near(telemetry.captureSeconds(), 0.0)
                && near(telemetry.captureConfidence(), 0.0)
                && near(telemetry.curveDriftDb(), 60.0),
            "Reference import left stale capture telemetry visible");
    telemetry.releaseResources();

    // Imported Reference + Target uses the normal background analysis path;
    // the file-dialog call itself must not become a second synchronous matcher.
    ToneTraceProcessor pair;
    pair.prepareToPlay(48000.0, 512);
    const auto target = makeSpectrum(100.0, 1000.0, 10000.0, -2.0, 3.0, -1.0);
    require(pair.importReference(reference).ok, "pair Reference import failed");
    require(pair.importTarget(target).ok, "pair Target import failed");
    const auto pairDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!pair.profileActive() && std::chrono::steady_clock::now() < pairDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(pair.profileActive(), "imported Target background analysis did not install a profile");
    require(pair.statusCode() == static_cast<int>(tonetrace::capture::Status::Preview),
            "imported Target did not finish in Preview");
    require(pair.getTailLengthSeconds() > 0.0, "imported Target did not publish correction tail length");
    pair.releaseResources();

    // A model import supplies an imported Reference. Learn Target must preserve
    // that Reference but discard the model's old synthetic Target so a new live
    // Target is really what Correct analyzes.
    ToneTraceProcessor recapture;
    recapture.prepareToPlay(48000.0, 512);
    recapture.setToggleFromUi(kToneNotificationsIndex, false);
    require(recapture.importModel(flatFrozenModel()).ok, "model import for Target recapture failed");
    require(recapture.getTailLengthSeconds() > 0.0, "model import did not publish correction tail length");
    recapture.setWorkflowFromUi(2);
    require(recapture.workflowStep() == 2
                && recapture.statusCode() == static_cast<int>(tonetrace::capture::Status::CapturingTarget),
            "Learn Target after model import discarded the imported Reference");
    for (int frame = 0; frame < 144000; frame += 512)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const double t = static_cast<double>(frame + i) / 48000.0;
            const auto x = static_cast<float>(0.12 * std::sin(
                    2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                + 0.08 * std::sin(2.0 * juce::MathConstants<double>::pi * 3300.0 * t));
            block.setSample(0, i, x);
            block.setSample(1, i, x);
        }
        recapture.processBlock(block, midi);
    }
    recapture.setWorkflowFromUi(3);
    const auto recaptureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (recapture.statusCode() == static_cast<int>(tonetrace::capture::Status::Analyzing)
           && std::chrono::steady_clock::now() < recaptureDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto recapturedSnapshot = recapture.profileSnapshotCopy();
    require(recapturedSnapshot != nullptr && recapturedSnapshot->target.fftSize > 1,
            "Target recapture reused the old imported Target instead of live audio");
    recapture.releaseResources();
}

void testRestoredEditorControls()
{
    ToneTraceProcessor processor;
    ToneTraceEditor editor(processor);
    juce::ToggleButton* trace = nullptr;
    juce::TextButton* exportButton = nullptr;
    juce::TextButton* importButton = nullptr;
    bool readout = false, description = false, copy = false;
    for (auto* child : editor.getChildren())
    {
        if (auto* button = dynamic_cast<juce::ToggleButton*>(child); button != nullptr && button->getButtonText() == "Trace Curve") trace = button;
        if (child->getName() == "Curve Readout") readout = true;
        if (child->getName() == "Curve Description" && dynamic_cast<juce::TextEditor*>(child) != nullptr) description = true;
        if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr)
        {
            if (button->getButtonText() == "Copy Curve Description") copy = true;
            if (button->getButtonText() == "Export...") exportButton = button;
            if (button->getButtonText() == "Import...") importButton = button;
        }
    }
    require(trace != nullptr && readout && description && copy, "released curve inspection controls missing");
    require(exportButton != nullptr && static_cast<bool>(exportButton->onClick),
            "Export button exists but has no activation callback");
    require(importButton != nullptr && static_cast<bool>(importButton->onClick),
            "Import button exists but has no activation callback");
    require(editor.keyPressed(juce::KeyPress(juce::KeyPress::F2Key)) && trace->getToggleState(), "editor F2 failed to enable Trace Curve");
    require(editor.keyPressed(juce::KeyPress('T')) && !trace->getToggleState(), "editor T failed to disable Trace Curve");
    editor.setSnapshotPage(3);
    ToneTraceParameterSlider* resolution = nullptr;
    for (auto* child : editor.getChildren())
        if (auto* slider = dynamic_cast<ToneTraceParameterSlider*>(child);
            slider != nullptr && slider->isVisible()) resolution = slider;
    require(resolution != nullptr, "band page resolution control missing");
    require(resolution->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)),
            "band page resolution exact entry failed");
    bool visibleExactEntry = false;
    for (auto* child : editor.getChildren())
        if (auto* field = dynamic_cast<juce::TextEditor*>(child);
            field != nullptr && !field->isReadOnly() && field->isVisible())
            visibleExactEntry = !field->getBounds().isEmpty()
                && editor.getLocalBounds().contains(field->getBounds());
    require(visibleExactEntry, "band page exact entry was invisible or outside editor");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace tonetrace::product;

    ToneTraceProcessor source;
    source.setContinuousFromUi(kMaximumCorrectionIndex, 23.125);
    source.setContinuousFromUi(kCorrectionStrengthIndex, 0.731);
    source.setContinuousFromUi(kCorrectionResolutionIndex, 37.0);
    source.setContinuousFromUi(kCorrectionRangeLowIndex, 73.0);
    source.setContinuousFromUi(kCorrectionRangeHighIndex, 17321.0);
    source.setContinuousFromUi(kCorrectionSharpnessIndex, 1.237);
    source.setContinuousFromUi(kCorrectionGainIndex, 2.375);
    source.setContinuousFromUi(kEmergencyClipGuardIndex, 4.125);
    source.setContinuousFromUi(kConfidenceToneVolumeIndex, -17.0);
    source.setMatchModeFromUi(3);
    source.setToggleFromUi(kFullCorrectionRangeIndex, true);
    source.setToggleFromUi(kToneNotificationsIndex, false);
    source.setToggleFromUi(kBypassIndex, true);

    // Manual-band persistence is part of project/preset state. These calls update
    // the owned manual state synchronously; any renderer rebuild is background work.
    source.setBandValueDb(0, 1.250);
    source.setBandValueDb(18, -2.500);
    source.setBandValueDb(36, 3.750);

    const auto expected = captureState(source);
    require(expected.manualGains.size() == 37, "resolution change did not resize manual band state");

    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    require(saved.getSize() > 0, "getStateInformation returned an empty block");

    // This specifically covers the host lifecycle where state arrives before
    // prepareToPlay. Tone Trace intentionally retains a validated pending core state.
    ToneTraceProcessor restoreBeforePrepare;
    restoreBeforePrepare.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expectState(restoreBeforePrepare, expected, "restore before prepareToPlay");
    restoreBeforePrepare.prepareToPlay(48000.0, 512);
    expectState(restoreBeforePrepare, expected, "after prepareToPlay of pending restore");
    restoreBeforePrepare.releaseResources();

    // Also cover an already-prepared processor, which exercises the immediate
    // controller restore path used by hosts that replace state on a live instance.
    ToneTraceProcessor restoreAfterPrepare;
    restoreAfterPrepare.prepareToPlay(48000.0, 512);
    restoreAfterPrepare.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expectState(restoreAfterPrepare, expected, "restore after prepareToPlay");

    juce::MemoryBlock resaved;
    restoreAfterPrepare.getStateInformation(resaved);
    require(resaved.getSize() > 0, "restored processor could not save state again");
    restoreAfterPrepare.releaseResources();

    ToneTraceProcessor secondGeneration;
    secondGeneration.setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
    expectState(secondGeneration, expected, "second-generation state round-trip");

    // Released trace tones are navigation aids, so they remain audible after a
    // model import puts the workflow into Frozen. Exercise the real processor
    // path rather than a test-only policy shim.
    ToneTraceProcessor navigation;
    navigation.prepareToPlay(48000.0, 512);
    const auto imported = navigation.importModel(flatFrozenModel());
    require(imported.ok, "flat model import did not create a Frozen test profile");
    require(navigation.statusCode() == static_cast<int>(tonetrace::capture::Status::Frozen),
            "model import did not enter Frozen state");

    juce::AudioBuffer<float> toneBuffer(2, 512);
    juce::MidiBuffer midi;
    // Simulate the host entering active processing before testing the normal
    // audio-path navigation behavior.
    toneBuffer.clear();
    navigation.processBlock(toneBuffer, midi);

    toneBuffer.clear();
    navigation.requestNavigationTone(1000.0);
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) > 1.0e-5,
            "band navigation tone was muted while Frozen");

    toneBuffer.clear();
    navigation.requestNavigationSweep(100.0, 2000.0, 10, 1200.0);
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) > 1.0e-5,
            "page navigation sweep was muted while Frozen");

    // An active sweep must be stopped even without another navigation request.
    navigation.reset();
    toneBuffer.clear();
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) < 1.0e-8,
            "active page sweep survived reset");

    // A queued, not-yet-rendered sweep must also be discarded at reset.
    navigation.requestNavigationSweep(100.0, 2000.0, 10, 1200.0);
    navigation.reset();
    toneBuffer.clear();
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) < 1.0e-8,
            "queued page sweep survived reset");

    // VST3 setProcessing(false) reaches AudioProcessor::reset() in JUCE 8.0.12.
    // Released CLAP used its Beep worker for a stopped page sweep and returned
    // without queueing that sweep for later audio processing. Keep that exact
    // no-stale-sweep behavior. The validation target disables actual OS Beeps.
    navigation.reset();
    toneBuffer.clear();
    navigation.requestNavigationSweep(100.0, 2000.0, 10, 1200.0);
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) < 1.0e-8,
            "stopped-transport page sweep leaked into resumed audio");

    // Released CLAP did queue a stopped single-band tone for the audio path as
    // well as asking the Beep worker to sound it immediately. Preserve that
    // released asymmetry rather than normalizing the two request types.
    navigation.reset();
    toneBuffer.clear();
    navigation.requestNavigationTone(1000.0);
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) > 1.0e-5,
            "stopped-transport band tone lost released resume parity");

    navigation.setToggleFromUi(kToneNotificationsIndex, false);
    toneBuffer.clear();
    navigation.requestNavigationTone(1000.0);
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) < 1.0e-8,
            "Tone Notifications Off did not suppress navigation tone");

    navigation.setToggleFromUi(kToneNotificationsIndex, true);
    navigation.reset();
    navigation.setToggleFromUi(kToneNotificationsIndex, false);
    navigation.requestNavigationSweep(100.0, 2000.0, 10, 1200.0);
    navigation.requestNavigationTone(1000.0);
    navigation.setToggleFromUi(kToneNotificationsIndex, true);
    toneBuffer.clear();
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) < 1.0e-8,
            "navigation requested while muted was replayed after unmuting");

    navigation.setToggleFromUi(kBypassIndex, true);
    toneBuffer.clear();
    navigation.requestNavigationTone(1000.0);
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) < 1.0e-8,
            "Bypass did not suppress navigation tone");

    navigation.setToggleFromUi(kBypassIndex, false);
    navigation.setNonRealtime(true);
    toneBuffer.clear();
    navigation.requestNavigationTone(1000.0);
    navigation.processBlock(toneBuffer, midi);
    require(maximumAbsoluteSample(toneBuffer) < 1.0e-8,
            "offline rendering did not suppress navigation tone");
    navigation.setNonRealtime(false);
    navigation.releaseResources();

    testImportedReferenceWithLiveTarget();
    testTransferParity();
    testBandDisplaySnapshotConsistency();
    testRestoredEditorControls();

    std::cout << "PASS: processor state round-trip plus Frozen navigation-tone policy\n";
    return 0;
}
