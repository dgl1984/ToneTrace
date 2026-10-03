#pragma once

#include <JuceHeader.h>

class ToneTraceCommandButton final : public juce::TextButton
{
public:
    ToneTraceCommandButton(const juce::String& name, const juce::String& description);
    std::function<void()> onCommand;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceCommandButton)
};
