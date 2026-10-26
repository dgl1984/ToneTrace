#include "PluginProcessor.h"

std::optional<tonetrace::SpectrumCapture> ToneTraceProcessor::preMatchTargetCaptureCopy() const
{
    if (phase_.load(std::memory_order_acquire) != tonetrace::capture::WorkflowPhase::Preview)
        return std::nullopt;

    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        if (analysisBusy_.load(std::memory_order_acquire) && importedTarget_.has_value())
            return importedTarget_;
    }

    if (analysisBusy_.load(std::memory_order_acquire))
        return correctionController_.inProgressTargetCopy();

    if (statusCode() == static_cast<int>(tonetrace::capture::Status::Preview))
        return exportableTarget();

    return std::nullopt;
}
