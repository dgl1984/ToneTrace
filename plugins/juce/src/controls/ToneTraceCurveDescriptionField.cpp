#include "ToneTraceCurveDescriptionField.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "tonetrace/tonetrace_describe.h"

#include <chrono>
#include <thread>

ToneTraceCurveDescriptionField::ToneTraceCurveDescriptionField(
    const juce::String& name,
    const juce::String& description,
    bool multiLine,
    bool contextSensitive)
    : ToneTraceReadOnlyField(name, description, multiLine),
      contextSensitive_(contextSensitive)
{
    if (contextSensitive_)
        worker_ = std::thread([this] { workerLoop(); });
}

ToneTraceCurveDescriptionField::~ToneTraceCurveDescriptionField()
{
    if (!contextSensitive_) return;
    {
        const std::lock_guard<std::mutex> lock(workerMutex_);
        workerExit_ = true;
        pendingJob_.reset();
    }
    workerCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ToneTraceCurveDescriptionField::setContent(
    const juce::String& normalDescription,
    bool announce,
    juce::AccessibilityHandler::AnnouncementPriority priority)
{
    if (!contextSensitive_)
    {
        ToneTraceReadOnlyField::setContent(normalDescription, announce, priority);
        return;
    }

    auto* editor = findParentComponentOfClass<ToneTraceEditor>();
    if (editor == nullptr)
    {
        ToneTraceReadOnlyField::setContent(normalDescription, announce, priority);
        return;
    }

    auto& processor = editor->processor_;
    int stage = 0;
    if (processor.analysisBusy() && processor.workflowStep() == 3)
        stage = 2;
    else if (processor.workflowStep() == 2)
        stage = 1;

    const int status = processor.statusCode();
    const bool importedReferenceRefresh = stage == 1
        && status == static_cast<int>(tonetrace::capture::Status::ImportedReference);
    const bool importedTargetRefresh = stage == 2
        && status == static_cast<int>(tonetrace::capture::Status::ImportedTarget);

    if (stage != stage_ || importedReferenceRefresh || importedTargetRefresh)
    {
        stage_ = stage;
        ++generation_;
        cachedDescription_.clear();
        if (stage == 1 || stage == 2)
            queueReport(processor, stage);
    }

    if (stage == 0)
    {
        ToneTraceReadOnlyField::setContent(normalDescription, announce, priority);
        return;
    }

    if (cachedDescription_.isNotEmpty())
    {
        ToneTraceReadOnlyField::setContent(cachedDescription_, announce, priority);
        return;
    }

    ToneTraceReadOnlyField::setContent(
        stage == 1 ? "Reference report is being prepared."
                   : "Target report is being prepared.",
        false,
        priority);
}

void ToneTraceCurveDescriptionField::queueReport(ToneTraceProcessor& processor, int stage)
{
    const std::lock_guard<std::mutex> lock(workerMutex_);
    pendingJob_ = ReportJob { &processor, stage, generation_ };
    workerCv_.notify_one();
}

void ToneTraceCurveDescriptionField::workerLoop()
{
    for (;;)
    {
        ReportJob job;
        {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerCv_.wait(lock, [this] { return workerExit_ || pendingJob_.has_value(); });
            if (workerExit_) return;
            job = *pendingJob_;
            pendingJob_.reset();
        }

        try
        {
            std::optional<tonetrace::SpectrumCapture> capture;
            if (job.processor != nullptr)
            {
                if (job.stage == 1)
                    capture = job.processor->exportableReference();
                else if (job.stage == 2)
                {
                    for (;;)
                    {
                        capture = job.processor->preMatchTargetCaptureCopy();
                        if (capture.has_value() || !job.processor->analysisBusy()) break;
                        {
                            const std::lock_guard<std::mutex> lock(workerMutex_);
                            if (workerExit_) return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    if (!capture.has_value())
                        capture = job.processor->preMatchTargetCaptureCopy();
                }
            }

            juce::String description;
            if (capture.has_value())
            {
                description = job.stage == 1 ? "Reference\n" : "Target\n";
                description += juce::String::fromUTF8(tonetrace::describeCapture(*capture).c_str());
            }

            const auto safe = juce::Component::SafePointer<ToneTraceCurveDescriptionField>(this);
            juce::MessageManager::callAsync([safe, generation = job.generation,
                                             stage = job.stage, description]
            {
                if (safe == nullptr || safe->generation_ != generation || safe->stage_ != stage)
                    return;
                if (description.isEmpty()) return;
                safe->cachedDescription_ = description;
                safe->ToneTraceReadOnlyField::setContent(description, false);
            });
        }
        catch (...)
        {
            // Reporting is descriptive-only. A failed report must never take
            // down the editor or alter capture/correction state.
        }
    }
}
