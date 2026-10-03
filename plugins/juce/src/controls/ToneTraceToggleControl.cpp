#include "ToneTraceToggleControl.h"

ToneTraceToggleControl::ToneTraceToggleControl(const tonetrace::product::ReleasedParameterSpec& spec)
    : juce::ToggleButton(spec.name)
{
    jassert(spec.kind == tonetrace::product::ParameterKind::Toggle && !spec.readOnly);
    setName(spec.name);
    setTitle(spec.name);
    setDescription(spec.description);
    setTooltip(spec.description);
    setAccessible(true);
    setWantsKeyboardFocus(true);
    onClick = [this]
    {
        if (!syncingFromHost_ && onUserToggle) onUserToggle(getToggleState());
    };
}

void ToneTraceToggleControl::setFromHost(bool state)
{
    syncingFromHost_ = true;
    setToggleState(state, juce::dontSendNotification);
    syncingFromHost_ = false;
}
