#pragma once

#include <JuceHeader.h>

class ToneTraceAccessibleComboBox : public juce::ComboBox
{
public:
    ToneTraceAccessibleComboBox() = default;
    ~ToneTraceAccessibleComboBox() override = default;

    bool keyPressed(const juce::KeyPress&) override;

protected:
    void selectBoundary(bool first);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceAccessibleComboBox)
};
