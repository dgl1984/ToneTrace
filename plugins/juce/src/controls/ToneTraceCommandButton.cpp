#include "ToneTraceCommandButton.h"

ToneTraceCommandButton::ToneTraceCommandButton(const juce::String& name,
                                               const juce::String& description)
    : juce::TextButton(name)
{
    setName(name);
    setTitle(name);
    setDescription(description);
    setHelpText(description);
    setTooltip(description);
    setWantsKeyboardFocus(true);
    onClick = [this] { if (onCommand) onCommand(); };
}
