#pragma once

#include <JuceHeader.h>

class ToneTraceReadOnlyField : public juce::TextEditor
{
public:
    ToneTraceReadOnlyField(const juce::String& name,
                           const juce::String& description,
                           bool multiLine = true);
    void setContent(const juce::String&, bool announce = false,
                    juce::AccessibilityHandler::AnnouncementPriority priority =
                        juce::AccessibilityHandler::AnnouncementPriority::high);

private:
    juce::String lastAnnouncement_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceReadOnlyField)
};
