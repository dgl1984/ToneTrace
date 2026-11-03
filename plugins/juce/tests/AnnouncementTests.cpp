#include <JuceHeader.h>
#include "platform/ToneTraceAnnouncements.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <UIAutomation.h>
 #include <wrl/client.h>
 #include <wrl/implements.h>

namespace
{
class NotificationHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IUIAutomationNotificationEventHandler>
{
public:
    std::atomic<bool> received { false };
    HRESULT STDMETHODCALLTYPE HandleNotificationEvent(IUIAutomationElement*,
        NotificationKind, NotificationProcessing processing, BSTR text, BSTR activity) override
    {
        if (text && activity && wcscmp(text, L"ToneTrace announcement regression") == 0
            && wcscmp(activity, L"ToneTrace.message") == 0
            && processing == NotificationProcessing_ImportantAll)
            received.store(true);
        return S_OK;
    }
};
}
#endif

void testScreenReaderAnnouncements()
{
   #if JUCE_WINDOWS
    juce::Component window;
    window.setName("ToneTrace announcement test");
    window.setBounds(0, 0, 100, 100);
    window.addToDesktop(0);
    window.setVisible(true);
    std::atomic<bool> ready { false }, done { false }, received { false };
    // UIA clients must run off the window thread, which stays free to dispatch.
    std::thread client([&]
    {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            Microsoft::WRL::ComPtr<IUIAutomation5> automation;
            Microsoft::WRL::ComPtr<IUIAutomationElement> desktop;
            auto handler = Microsoft::WRL::Make<NotificationHandler>();
            if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&automation)))
                && SUCCEEDED(automation->GetRootElement(&desktop))
                && SUCCEEDED(automation->AddNotificationEventHandler(desktop.Get(),
                    TreeScope_Subtree, nullptr, handler.Get())))
            {
                ready.store(true);
                while (!done.load())
                {
                    received.store(handler->received.load());
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                automation->RemoveNotificationEventHandler(desktop.Get(), handler.Get());
            }
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
    });
    for (int i = 0; i < 100 && !received.load(); ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        if (ready.load())
            tonetrace::platform::announce(window, "ToneTrace announcement regression",
                juce::AccessibilityHandler::AnnouncementPriority::high);
    }
    done.store(true);
    // UIA teardown may marshal back to this window thread.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    client.join();
    window.removeFromDesktop();
    if (!received.load())
    {
        std::cerr << "FAIL: Windows UIA client did not receive announcement\n";
        std::exit(1);
    }
   #endif
}
