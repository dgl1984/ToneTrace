#include "PluginProcessor.h"

#include <thread>

std::optional<tonetrace::SpectrumCapture> ToneTraceProcessor::preMatchTargetCaptureCopy() const
{
    // A live Target is owned by the audio thread until Correct changes phase to
    // Preview. Only inspect it during that AnalyzeCaptured transaction, after
    // any block that entered capture before the phase change has left. This
    // keeps reporting read-only and completely outside the realtime path.
    if (!analysisBusy_.load(std::memory_order_acquire)
        || phase_.load(std::memory_order_acquire) != tonetrace::capture::WorkflowPhase::Preview
        || !targetCorrectable_.load(std::memory_order_acquire))
        return std::nullopt;

    while (captureWriterCount_.load(std::memory_order_acquire) != 0)
    {
        if (!analysisBusy_.load(std::memory_order_acquire)) return std::nullopt;
        std::this_thread::yield();
    }

    tonetrace::AudioBuffer targetAudio;
    try
    {
        const std::lock_guard<std::mutex> lock(captureControlMutex_);
        if (!analysisBusy_.load(std::memory_order_acquire)
            || phase_.load(std::memory_order_acquire) != tonetrace::capture::WorkflowPhase::Preview)
            return std::nullopt;
        targetAudio = copyCaptureAudio(targetCapture_, captureSampleRate_.load(std::memory_order_acquire));
    }
    catch (...)
    {
        return std::nullopt;
    }

    try
    {
        const auto settings = currentCorrectionSettings();
        tonetrace::MatchSettings match;
        match.mode = settings.mode;
        match.rangeLowHz = 10.0;
        match.rangeHighHz = 30000.0;
        match.resolution = settings.resolution;
        match.maximumCorrectionDb = 60.0;
        tonetrace::MatchEngine engine;
        return engine.capture(targetAudio, match);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
