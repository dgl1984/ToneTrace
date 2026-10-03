#include "ToneTraceAccessibleComboBox.h"

bool ToneTraceAccessibleComboBox::keyPressed(const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::homeKey)
    {
        selectBoundary(true);
        return true;
    }
    if (code == juce::KeyPress::endKey)
    {
        selectBoundary(false);
        return true;
    }
    return juce::ComboBox::keyPressed(key);
}

void ToneTraceAccessibleComboBox::selectBoundary(bool first)
{
    const int count = getNumItems();
    if (count <= 0) return;
    setSelectedItemIndex(first ? 0 : count - 1, juce::sendNotificationSync);
}
