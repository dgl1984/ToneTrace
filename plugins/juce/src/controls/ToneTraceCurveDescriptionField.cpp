#include "ToneTraceCurveDescriptionField.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "tonetrace/tonetrace_describe.h"

void ToneTraceCurveDescriptionField::setContent(
    const juce::String& normalDescription,
    bool announce,
    juce::AccessibilityHandler::AnnouncementPriority priority)
{
    auto* editor = findParentComponentOfClass<ToneTraceEditor>();
    if (editor == nullptr)
    {
        ToneTraceReadOnlyField::setContent(normalDescription, announce, priority);
        return;
    }

    auto& processor = editor->processor_;

    // The normal completed-match report remains authoritative. These two
    // stages only replace its display while a newer capture is waiting to be
    // matched, so an existing last-known-good correction can stay active.
    int stage = 0;
    if (processor.analysisBusy() && processor.workflowStep() == 3)
        stage = 2; // Correct/import has finalized Target; replacement is analyzing.
    else if (processor.workflowStep() == 2)
        stage = 1; // Reference is finalized and Target capture is in progress.

    if (stage == 1)
    {
        const bool importedReference = processor.statusCode()
            == static_cast<int>(tonetrace::capture::Status::ImportedReference);
        if (stage_ != stage || importedReference)
        {
            const auto capture = processor.exportableReference();
            cachedDescription_ = capture.has_value()
                ? "Reference\n" + juce::String::fromUTF8(tonetrace::describeCapture(*capture).c_str())
                : normalDescription;
        }
        stage_ = stage;
        ToneTraceReadOnlyField::setContent(cachedDescription_, announce, priority);
        return;
    }

    if (stage == 2)
    {
        if (stage_ != stage)
        {
            const auto capture = processor.preMatchTargetCaptureCopy();
            cachedDescription_ = capture.has_value()
                ? "Target\n" + juce::String::fromUTF8(tonetrace::describeCapture(*capture).c_str())
                : normalDescription;
        }
        stage_ = stage;
        ToneTraceReadOnlyField::setContent(cachedDescription_, announce, priority);
        return;
    }

    stage_ = stage;
    cachedDescription_.clear();
    ToneTraceReadOnlyField::setContent(normalDescription, announce, priority);
}
