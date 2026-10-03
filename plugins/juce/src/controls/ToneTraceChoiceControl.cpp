#include "ToneTraceChoiceControl.h"
#include <algorithm>

ToneTraceChoiceControl::ToneTraceChoiceControl(const tonetrace::product::ChoiceControlSpec& spec)
    : spec_(spec)
{
    const auto& parameter = *spec_.parameter;
    setName(parameter.name);
    setTitle(parameter.name);
    setDescription(parameter.description);
    setTooltip(parameter.description);
    setHelpText("Arrow keys change the selection. Home selects the first choice and End selects the last choice.");
    setAccessible(true);
    setWantsKeyboardFocus(true);
    setEditableText(false);
    for (std::size_t i = 0; i < spec_.choices.size(); ++i)
        addItem(spec_.choices[i], static_cast<int>(i) + 1);
    setSelectedItemIndex(spec_.defaultIndex, juce::dontSendNotification);
    onChange = [this] { changedByUser(); };
}

void ToneTraceChoiceControl::setFromHostIndex(int index)
{
    const auto bounded = std::clamp(index, 0, static_cast<int>(spec_.choices.size()) - 1);
    syncingFromHost_ = true;
    setSelectedItemIndex(bounded, juce::dontSendNotification);
    syncingFromHost_ = false;
}

void ToneTraceChoiceControl::changedByUser()
{
    if (syncingFromHost_) return;
    const auto index = getSelectedItemIndex();
    if (index < 0) return;
    if (onUserGestureBegin) onUserGestureBegin();
    if (onUserIndexChanged) onUserIndexChanged(index);
    if (onUserGestureEnd) onUserGestureEnd();
}
