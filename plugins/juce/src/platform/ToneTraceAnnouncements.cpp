#include "ToneTraceAnnouncements.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <UIAutomation.h>
 #include <wrl/client.h>
#endif

namespace tonetrace::platform
{
void announce(juce::Component& source, const juce::String& text,
              juce::AccessibilityHandler::AnnouncementPriority priority)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (text.isEmpty()) return;
   #if JUCE_WINDOWS
    // Use the peer's public HWND host provider, not JUCE's private COM layout.
    // JUCE 8.0.12 postAnnouncement uses SAPI on Windows, so do not call it here.
    if (!UiaClientsAreListening()) return;
    const auto* peer = source.getPeer();
    if (peer == nullptr) return;
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> provider;
    if (FAILED(UiaHostProviderFromHwnd(static_cast<HWND>(peer->getNativeHandle()),
                                     provider.GetAddressOf()))) return;
    const auto message = SysAllocString(text.toWideCharPointer());
    const auto activity = SysAllocString(L"ToneTrace.message");
    if (message != nullptr && activity != nullptr)
        UiaRaiseNotificationEvent(provider.Get(), NotificationKind_Other,
            priority == juce::AccessibilityHandler::AnnouncementPriority::high
                ? NotificationProcessing_ImportantAll : NotificationProcessing_MostRecent,
            message, activity);
    SysFreeString(activity);
    SysFreeString(message);
   #elif JUCE_MAC || JUCE_IOS
    juce::ignoreUnused(source);
    // JUCE uses the native accessibility announcement notification on Apple OSes.
    juce::AccessibilityHandler::postAnnouncement(text, priority);
   #else
    juce::ignoreUnused(priority);
    if (auto* handler = source.getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::textChanged);
   #endif
}
}
