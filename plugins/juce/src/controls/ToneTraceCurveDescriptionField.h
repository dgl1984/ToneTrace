#pragma once

#include "ToneTraceReadOnlyField.h"

class ToneTraceCurveDescriptionField final : public ToneTraceReadOnlyField
{
public:
    ToneTraceCurveDescriptionField(const juce::String& name,
                                   const juce::String& description,
                                   bool multiLine = true)
        : ToneTraceReadOnlyField(name, description, multiLine)
    {
    }

    void setContent(const juce::String&, bool announce = false,
                    juce::AccessibilityHandler::AnnouncementPriority priority =
                        juce::AccessibilityHandler::AnnouncementPriority::high);

private:
    int stage_ = -1;
    juce::String cachedDescription_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceCurveDescriptionField)
};
