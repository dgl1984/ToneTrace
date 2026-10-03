#pragma once

#include <JuceHeader.h>
#include "ToneTraceAccessibleComboBox.h"
#include "../ToneTraceProductCatalog.h"

class ToneTraceChoiceControl final : public ToneTraceAccessibleComboBox
{
public:
    explicit ToneTraceChoiceControl(const tonetrace::product::ChoiceControlSpec&);
    ~ToneTraceChoiceControl() override = default;

    std::function<void()> onUserGestureBegin;
    std::function<void(int)> onUserIndexChanged;
    std::function<void()> onUserGestureEnd;

    const tonetrace::product::ChoiceControlSpec& spec() const noexcept { return spec_; }
    void setFromHostIndex(int index);

private:
    void changedByUser();
    const tonetrace::product::ChoiceControlSpec& spec_;
    bool syncingFromHost_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceChoiceControl)
};
