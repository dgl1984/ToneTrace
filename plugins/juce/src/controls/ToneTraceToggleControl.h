#pragma once

#include <JuceHeader.h>
#include "../ToneTraceProductCatalog.h"

class ToneTraceToggleControl final : public juce::ToggleButton
{
public:
    explicit ToneTraceToggleControl(const tonetrace::product::ReleasedParameterSpec&);
    std::function<void(bool)> onUserToggle;
    void setFromHost(bool);

private:
    bool syncingFromHost_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceToggleControl)
};
