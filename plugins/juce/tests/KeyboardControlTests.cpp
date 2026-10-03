#include <JuceHeader.h>

#include "ToneTraceProductCatalog.h"
#include "controls/ToneTraceAccessibleComboBox.h"
#include "controls/ToneTraceBandSlider.h"
#include "controls/ToneTraceParameterSlider.h"
#include "controls/ToneTracePageSelector.h"

#include <cmath>
#include <iostream>

namespace
{
bool near(double a, double b) { return std::abs(a - b) < 1.0e-6; }

int fail(const char* message)
{
    std::cerr << "FAIL: " << message << "\n";
    return 1;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace tonetrace::product;

    ToneTraceParameterSlider slider(kImplementedContinuousControls[0]);
    const auto& spec = kImplementedContinuousControls[0];
    slider.setFromHost(spec.parameter->defaultValue);
    if (!slider.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)) || !near(slider.getValue(), spec.parameter->minimum))
        return fail("parameter slider Home did not select minimum");
    if (!slider.keyPressed(juce::KeyPress(juce::KeyPress::endKey)) || !near(slider.getValue(), spec.parameter->maximum))
        return fail("parameter slider End did not select maximum");

    ToneTraceBandSlider band(0, 1000.0);
    band.setBandIdentity(0, 1000.0, 18.0);
    if (!band.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)) || !near(band.getValue(), 18.0))
        return fail("band Home did not select maximum");
    if (!band.keyPressed(juce::KeyPress(juce::KeyPress::endKey)) || !near(band.getValue(), -18.0))
        return fail("band End did not select minimum");
    int move = 0;
    bool traceToggled = false;
    bool exactRequested = false;
    band.onNavigate = [&](std::size_t, int delta) { move = delta; };
    band.onToggleTrace = [&] { traceToggled = !traceToggled; };
    band.onRequestExactEntry = [&](std::size_t) { exactRequested = true; };
    band.setFromProcessor(-6.234);
    band.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    if (move != 1 || !near(band.getValue(), -6.234)) return fail("Right changed gain instead of navigating bands");
    band.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    if (move != -1 || !near(band.getValue(), -6.234)) return fail("Left changed gain instead of navigating bands");
    band.keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    if (!near(band.getValue(), -5.234)) return fail("Up lost the fractional band value");
    band.keyPressed(juce::KeyPress(juce::KeyPress::F2Key));
    if (!traceToggled || exactRequested) return fail("F2 did not toggle Trace Curve");
    band.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    if (!exactRequested) return fail("Enter did not open exact band entry");
    if (band.formatValue(-6.2) != "-6.2 dB" || band.formatValue(-6.234) != "-6.234 dB")
        return fail("band text differs from released meaningful precision");
    band.setBounds(0, 0, 80, 200);
    band.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    auto* bandHandler = band.getAccessibilityHandler();
    if (bandHandler == nullptr || bandHandler->getValueInterface() == nullptr)
        return fail("band accessibility value missing");
    auto* bandValue = bandHandler->getValueInterface();
    if (bandValue->getRange().isValid() || bandValue->getCurrentValueAsString() != "-5.234 dB")
        return fail("band accessibility exposed a percentage range instead of dB text");
    bandValue->setValueAsString("-6.234 dB");
    if (!near(band.getValue(), -6.234)) return fail("accessible exact band write failed");
    bandValue->setValueAsString("invalid");
    if (!near(band.getValue(), -6.234)) return fail("invalid accessible band write changed value");
    band.removeFromDesktop();

    ToneTraceAccessibleComboBox combo;
    combo.addItem("First", 1);
    combo.addItem("Middle", 2);
    combo.addItem("Last", 3);
    combo.setSelectedItemIndex(1, juce::dontSendNotification);
    if (!combo.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)) || combo.getSelectedItemIndex() != 0)
        return fail("combo Home did not select first item");
    if (!combo.keyPressed(juce::KeyPress(juce::KeyPress::endKey)) || combo.getSelectedItemIndex() != 2)
        return fail("combo End did not select last item");

    juce::Component pageParent;
    pageParent.setBounds(0, 0, 600, 40);
    pageParent.setOpaque(true);

    // JUCE creates accessibility handlers only for components attached to a
    // native peer. Give the production page buttons the same prerequisite they
    // have inside a hosted plug-in editor before inspecting their real handlers.
    pageParent.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    if (pageParent.getWindowHandle() == nullptr)
        return fail("accessibility test harness did not create a native JUCE peer");

    ToneTracePageSelector pages(pageParent);
    pages.setBounds(pageParent.getLocalBounds());
    int pageCallbackCount = 0;
    int lastPage = -1;
    bool lastPageWasUser = true;
    pages.onPageChanged = [&](int page, bool userInitiated)
    {
        ++pageCallbackCount;
        lastPage = page;
        lastPageWasUser = userInitiated;
    };
    pages.setPages({ "Capture and transfer", "Correction controls", "Bands 1-10" }, 0);
    if (pages.pageCount() != 3 || pages.selectedPage() != 0)
        return fail("page selector did not build the requested pages");
    if (pageCallbackCount != 1 || lastPage != 0 || lastPageWasUser)
        return fail("page rebuild did not produce one programmatic selection callback");

    if (pageParent.getNumChildComponents() != 3)
        return fail("page selector did not create exactly one direct child per page");

    auto* firstPage = dynamic_cast<juce::TextButton*>(pageParent.getChildComponent(0));
    auto* secondPage = dynamic_cast<juce::TextButton*>(pageParent.getChildComponent(1));
    if (firstPage == nullptr || secondPage == nullptr)
        return fail("page selector buttons were not created as JUCE TextButtons");

    const int pageRadioGroup = firstPage->getRadioGroupId();
    int focusablePageButtons = 0;
    int checkedPageButtons = 0;
    for (int i = 0; i < pageParent.getNumChildComponents(); ++i)
    {
        auto* button = dynamic_cast<juce::TextButton*>(pageParent.getChildComponent(i));
        if (button == nullptr)
            return fail("page selector contains a non-TextButton child");
        if (pageRadioGroup == 0 || button->getRadioGroupId() != pageRadioGroup)
            return fail("page selector buttons do not share one nonzero JUCE radio group");
        if (!button->getClickingTogglesState())
            return fail("page selector buttons do not use ordinary JUCE toggle state");
        if (button->getAccessibilityHandler() == nullptr
            || button->getAccessibilityHandler()->getRole() != juce::AccessibilityRole::radioButton)
            return fail("page selector button is not exposed as a radio button to accessibility clients");
        if (button->getWantsKeyboardFocus())
            ++focusablePageButtons;
        if (button->getAccessibilityHandler()->getCurrentState().isChecked())
            ++checkedPageButtons;
    }
    if (focusablePageButtons != 1 || !firstPage->getWantsKeyboardFocus())
        return fail("page selector does not use one roving keyboard Tab stop");
    if (checkedPageButtons != 1 || !firstPage->getAccessibilityHandler()->getCurrentState().isChecked())
        return fail("selected page button is not exposed as the one checked radio button");

    pageCallbackCount = 0;
    // Exercise the public Component keyboard entry point; virtual dispatch reaches
    // the production page button exactly as host keyboard delivery does.
    juce::Component& firstPageComponent = *firstPage;
    if (!firstPageComponent.keyPressed(juce::KeyPress(juce::KeyPress::rightKey))
        || pages.selectedPage() != 1)
        return fail("page Right key did not select exactly one next page");
    if (pageCallbackCount != 1 || lastPage != 1 || !lastPageWasUser)
        return fail("page Right key did not use the one user-selection callback");
    if (firstPage->getWantsKeyboardFocus() || !secondPage->getWantsKeyboardFocus())
        return fail("roving page Tab stop did not follow selection");
    if (firstPage->getAccessibilityHandler()->getCurrentState().isChecked()
        || !secondPage->getAccessibilityHandler()->getCurrentState().isChecked())
        return fail("JUCE radio checked state did not follow page selection");

    pageCallbackCount = 0;
    juce::Component& secondPageComponent = *secondPage;
    if (!secondPageComponent.keyPressed(juce::KeyPress(juce::KeyPress::leftKey))
        || pages.selectedPage() != 0)
        return fail("page Left key did not select exactly one previous page");
    if (pageCallbackCount != 1 || lastPage != 0 || !lastPageWasUser)
        return fail("page Left key did not use the one user-selection callback");

    pageCallbackCount = 0;
    if (!secondPage->onClick)
        return fail("page selector button click route is missing");
    secondPage->onClick();
    if (pages.selectedPage() != 1 || pageCallbackCount != 1 || !lastPageWasUser)
        return fail("page button click did not synchronize the authoritative page state");
    pageCallbackCount = 0;
    if (!pages.selectRelativeFromUser(1) || pages.selectedPage() != 2
        || pageCallbackCount != 1 || !lastPageWasUser)
        return fail("relative page navigation did not continue from the clicked visible page");

    pageCallbackCount = 0;
    pages.selectPageProgrammatically(1);
    if (pages.selectedPage() != 1 || pageCallbackCount != 1 || lastPageWasUser)
        return fail("programmatic page selection was reported as user navigation");

    pageCallbackCount = 0;
    pages.selectPageProgrammatically(2);
    if (pages.selectedPage() != 2 || pageCallbackCount != 1 || lastPageWasUser)
        return fail("programmatic page selection did not reach the requested visible page");

    pageCallbackCount = 0;
    if (!pages.selectRelativeFromUser(1) || pages.selectedPage() != 0
        || pageCallbackCount != 1 || !lastPageWasUser)
        return fail("relative page navigation did not wrap exactly one page");

    std::cout << "PASS: slider/combo extremes and JUCE radio-group page-selector navigation contract\n";
    return 0;
}
