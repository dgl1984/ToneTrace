#include "ToneTraceReadOnlyField.h"

ToneTraceReadOnlyField::ToneTraceReadOnlyField(const juce::String& name,
                                               const juce::String& description,
                                               bool multiLine)
{
    setName(name);
    setTitle(name);
    setDescription(description);
    setAccessible(true);
    setWantsKeyboardFocus(true);
    setReadOnly(true);
    setMultiLine(multiLine, true);
    setReturnKeyStartsNewLine(false);
    // Keep Tab/Shift+Tab as focus navigation even if JUCE defaults change.
    // This field is a readable telemetry stop, not an editable text surface.
    setTabKeyUsedAsCharacter(false);
    setScrollbarsShown(multiLine);
}

void ToneTraceReadOnlyField::setContent(
    const juce::String& text,
    bool announce,
    juce::AccessibilityHandler::AnnouncementPriority priority)
{
    if (getText() != text) setText(text, false);
    if (announce && text.isNotEmpty() && text != lastAnnouncement_)
    {
        lastAnnouncement_ = text;
        juce::AccessibilityHandler::postAnnouncement(text, priority);
    }
}
