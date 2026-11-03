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
    {
        worker_ = std::thread([this] { workerLoop(); });
        startTimer(2000);
    }
}

ToneTraceCurveDescriptionField::~ToneTraceCurveDescriptionField()
{
    stopTimer();
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
    setContentImpl([&normalDescription] { return normalDescription; }, announce, priority);
}

void ToneTraceCurveDescriptionField::setContentImpl(
    const std::function<juce::String()>& normalDescription,
    bool announce,
    juce::AccessibilityHandler::AnnouncementPriority priority)
{
    if (!contextSensitive_)
    {
        ToneTraceReadOnlyField::setContent(normalDescription(), announce, priority);
        return;
    }

    auto* editor = findParentComponentOfClass<ToneTraceEditor>();
    if (editor == nullptr)
    {
        ToneTraceReadOnlyField::setContent(normalDescription(), announce, priority);
        return;
    }

    auto& processor = editor->processor_;
    int stage = 0;
    if (processor.analysisBusy() && processor.workflowStep() == 3)
        stage = 2;
    else if (processor.workflowStep() == 2
             || (processor.workflowStep() == 1
                 && processor.referenceReady()))
        stage = 1;

    const auto revision = processor.reportRevision();

    // Repeat the pre-match report only when the spectrum data actually changed.
    // Importing another Reference/Target in the same workflow stage bumps the
    // processor revision, while unrelated parameter updates leave it untouched.
    if (stage != stage_ || revision != reportRevision_)
    {
        stage_ = stage;
        reportRevision_ = revision;
        ++generation_;
        cachedDescription_.clear();
        if (stage == 1 || stage == 2)
            queueReport(processor, stage);
    }

    // Refresh a growing Reference in the background, retaining the readable
    // report while the next snapshot is prepared. Also catches the final short
    // capture when playback stops between refresh intervals.
    const double seconds = processor.captureSeconds();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (stage == 1 && processor.workflowStep() == 1
        && seconds != reportedCaptureSeconds_ && now - lastCaptureReportMs_ >= 2000.0)
    {
        reportedCaptureSeconds_ = seconds;
        lastCaptureReportMs_ = now;
        queueReport(processor, stage);
    }
    if (stage == 0)
    {
        ToneTraceReadOnlyField::setContent(normalDescription(), announce, priority);
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

void ToneTraceCurveDescriptionField::timerCallback()
{
    auto* editor = findParentComponentOfClass<ToneTraceEditor>();
    if (editor != nullptr && editor->processor_.workflowStep() == 1
        && editor->processor_.referenceReady())
        setContentImpl([] { return juce::String(); }, false,
                       juce::AccessibilityHandler::AnnouncementPriority::high);
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
                if (description.isEmpty())
                {
                    // Never leave the field stuck on "being prepared". If the
                    // spectrum could not be read, say so; a later stage or
                    // revision change still requests a fresh report.
                    const juce::String fallback = stage == 1
                        ? "Reference spectrum report is unavailable."
                        : "Target spectrum report is unavailable.";
                    safe->cachedDescription_ = fallback;
                    safe->ToneTraceReadOnlyField::setContent(fallback, false);
                    return;
                }
                safe->cachedDescription_ = description;
                safe->ToneTraceReadOnlyField::setContent(description, false);
            });
        }
        catch (...)
        {
            // Reporting is descriptive-only. A failed report must never take
            // down the editor or alter capture/correction state, but it should
            // not leave the placeholder on screen forever either.
            const auto safe = juce::Component::SafePointer<ToneTraceCurveDescriptionField>(this);
            juce::MessageManager::callAsync([safe, generation = job.generation, stage = job.stage]
            {
                if (safe == nullptr || safe->generation_ != generation || safe->stage_ != stage)
                    return;
                const juce::String fallback = stage == 1
                    ? "Reference spectrum report is unavailable."
                    : "Target spectrum report is unavailable.";
                safe->cachedDescription_ = fallback;
                safe->ToneTraceReadOnlyField::setContent(fallback, false);
            });
        }
    }
}
