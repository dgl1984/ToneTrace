#include "PluginProcessor.h"

#include <chrono>
#include <thread>

std::optional<tonetrace::SpectrumCapture> ToneTraceProcessor::preMatchTargetCaptureCopy() const
{
    if (!analysisBusy_.load(std::memory_order_acquire)
        || phase_.load(std::memory_order_acquire) != tonetrace::capture::WorkflowPhase::Preview)
        return std::nullopt;

    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        if (importedTarget_.has_value()) return importedTarget_;
    }

    while (analysisBusy_.load(std::memory_order_acquire))
    {
        if (const auto capture = correctionController_.inProgressTargetCopy(); capture.has_value())
            return capture;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return correctionController_.inProgressTargetCopy();
}
