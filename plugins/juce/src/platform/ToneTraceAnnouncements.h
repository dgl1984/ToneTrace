#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace tonetrace::platform
{
// Call on the message thread, after updating the visible accessible message.
// Never synthesizes speech; the user's assistive technology controls delivery.
void announce(juce::Component& source, const juce::String& text,
              juce::AccessibilityHandler::AnnouncementPriority priority);
}
