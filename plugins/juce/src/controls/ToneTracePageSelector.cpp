#include "ToneTracePageSelector.h"

#include "ui/ToneTraceDesignSystem.h"

ToneTracePageSelector::ToneTracePageSelector(juce::Component& parent)
    : parent_(parent)
{
}

void ToneTracePageSelector::setPages(const std::vector<juce::String>& names, int selectedIndex)
{
    const bool restoreSelectorFocus = focusIsInSelector();

    overflowButton_.reset();
    buttons_.clear();
    selectedIndex_ = -1;

    buttons_.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto button = std::make_unique<PageButton>(*this, names[i], static_cast<int>(i));
        parent_.addAndMakeVisible(*button);
        buttons_.push_back(std::move(button));
    }

    layoutButtons();

    if (buttons_.empty())
    {
        if (onPageChanged)
            onPageChanged(-1, false);
        return;
    }

    selectPage(juce::jlimit(0, pageCount() - 1, selectedIndex),
               false, restoreSelectorFocus);
}

void ToneTracePageSelector::selectPageProgrammatically(int index)
{
    selectPage(index, false, false);
}

bool ToneTracePageSelector::selectRelativeFromUser(int delta)
{
    const int count = pageCount();
    if (count <= 0 || delta == 0)
        return false;

    const int current = juce::jmax(0, selectedIndex_);
    const int normalizedDelta = delta % count;
    const int next = (current + normalizedDelta + count) % count;
    return selectPage(next, true, true);
}

void ToneTracePageSelector::setBounds(juce::Rectangle<int> bounds)
{
    bounds_ = bounds;
    layoutButtons();
}

ToneTracePageSelector::PageButton::PageButton(ToneTracePageSelector& owner,
                                               juce::String text,
                                               int pageIndex)
    : juce::TextButton(text), owner_(owner), pageIndex_(pageIndex)
{
    setName(text);
    setTitle(text + " page selector");
    setDescription("Changes the visible Tone Trace editor page.");
    setHelpText("This page selector group is one Tab stop. Left and Right change the selected page. Home selects the first page and End selects the last page. Tab moves into the selected page. Control+Tab and Control+Shift+Tab switch one page from the page currently visible. Down opens the complete page menu.");
    setTooltip(getHelpText());
    setClickingTogglesState(true);
    setRadioGroupId(kRadioGroupId, juce::dontSendNotification);
    setWantsKeyboardFocus(false);
    setHasFocusOutline(false);

    onClick = [this]
    {
        owner_.selectPage(pageIndex_, true, true);
    };
}

void ToneTracePageSelector::PageButton::setSelected(bool selected)
{
    if (getToggleState() == selected && getWantsKeyboardFocus() == selected)
        return;

    setToggleState(selected, juce::dontSendNotification);
    setWantsKeyboardFocus(selected);
    setExplicitFocusOrder(selected ? 1 : 0);
    repaint();

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

bool ToneTracePageSelector::PageButton::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    if (modifiers.isCtrlDown() || modifiers.isAltDown() || modifiers.isCommandDown())
        return juce::TextButton::keyPressed(key);

    if (key.getKeyCode() == juce::KeyPress::downKey)
    {
        owner_.showPageMenu();
        return true;
    }
    int target = owner_.selectedPage();
    const int count = owner_.pageCount();
    if (count <= 0)
        return juce::TextButton::keyPressed(key);

    if (key.getKeyCode() == juce::KeyPress::leftKey)
        target = (target + count - 1) % count;
    else if (key.getKeyCode() == juce::KeyPress::rightKey)
        target = (target + 1) % count;
    else if (key.getKeyCode() == juce::KeyPress::homeKey)
        target = 0;
    else if (key.getKeyCode() == juce::KeyPress::endKey)
        target = count - 1;
    else
        return juce::TextButton::keyPressed(key);

    owner_.selectPage(target, true, true);
    return true;
}

void ToneTracePageSelector::PageButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    using namespace tonetrace::ui;

    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    auto background = getToggleState() ? colour::grid : colour::panelRaised;
    if (highlighted)
        background = background.brighter(0.08f);
    if (down)
        background = background.darker(0.08f);

    g.setColour(background);
    g.fillRoundedRectangle(bounds, 5.0f);

    if (getToggleState())
    {
        auto indicator = bounds;
        indicator = indicator.removeFromBottom(3.0f);
        g.setColour(colour::accentStrong);
        g.fillRoundedRectangle(indicator, 1.5f);
    }

    if (hasKeyboardFocus(false))
    {
        g.setColour(colour::focus);
        g.drawRoundedRectangle(bounds.reduced(1.0f), 5.0f, 1.5f);
    }

    g.setColour(getToggleState() ? colour::text : colour::mutedText);
    g.setFont(type::small());
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(7, 3),
                     juce::Justification::centred, 1, 0.72f);
}

bool ToneTracePageSelector::selectPage(int index, bool userInitiated, bool moveFocusToSelector)
{
    const int count = pageCount();
    if (count <= 0)
        return false;

    index = juce::jlimit(0, count - 1, index);
    const bool changed = selectedIndex_ != index;
    selectedIndex_ = index;
    updateButtonStates();
    layoutButtons();

    if (moveFocusToSelector)
        buttons_[static_cast<std::size_t>(selectedIndex_)]->grabKeyboardFocus();

    if (changed && onPageChanged)
        onPageChanged(selectedIndex_, userInitiated);

    return changed;
}

void ToneTracePageSelector::updateButtonStates()
{
    for (int i = 0; i < pageCount(); ++i)
        buttons_[static_cast<std::size_t>(i)]->setSelected(i == selectedIndex_);
}

void ToneTracePageSelector::layoutButtons()
{
    if (buttons_.empty()) return;
    const int capacity = std::max(4, bounds_.getWidth() / 110);
    const bool overflow = pageCount() > capacity;
    if (overflow && overflowButton_ == nullptr)
    {
        overflowButton_ = std::make_unique<juce::TextButton>("All pages...");
        overflowButton_->setName("All pages");
        overflowButton_->setDescription("Choose any page. Keyboard: Down on the selected page opens this menu.");
        // The radio group remains one roving Tab stop. Its Down key opens the
        // same accessible menu as this pointer shortcut.
        overflowButton_->setWantsKeyboardFocus(false);
        overflowButton_->onClick = [this] { showPageMenu(); };
        parent_.addAndMakeVisible(*overflowButton_);
    }
    if (overflowButton_ != nullptr) overflowButton_->setVisible(overflow);
    const int bandSlots = capacity - 3; // Capture, Correction and menu are persistent.
    const int firstBandPage = std::clamp(selectedIndex_ - bandSlots / 2, 2,
                                        std::max(2, pageCount() - bandSlots));
    std::vector<int> visible;
    for (int i = 0; i < pageCount(); ++i)
    {
        const bool show = !overflow || i < 2 || (i >= firstBandPage && i < firstBandPage + bandSlots);
        buttons_[static_cast<std::size_t>(i)]->setVisible(show);
        if (show) visible.push_back(i);
    }
    auto area = bounds_;
    int remaining = static_cast<int>(visible.size()) + (overflow ? 1 : 0);
    for (int index : visible)
    {
        buttons_[static_cast<std::size_t>(index)]->setBounds(area.removeFromLeft(area.getWidth() / remaining));
        --remaining;
    }
    if (overflow) overflowButton_->setBounds(area.reduced(2));
}

void ToneTracePageSelector::showPageMenu()
{
    if (buttons_.empty()) return;
    juce::PopupMenu menu;
    for (int i = 0; i < pageCount(); ++i)
        menu.addItem(i + 1, buttons_[static_cast<std::size_t>(i)]->getButtonText(), true, i == selectedIndex_);
    auto* anchor = buttons_[static_cast<std::size_t>(std::max(0, selectedIndex_))].get();
    const auto safe = juce::Component::SafePointer<juce::TextButton>(anchor);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor), [this, safe](int result)
    {
        if (safe != nullptr && result > 0) selectPage(result - 1, true, true);
    });
}

bool ToneTracePageSelector::focusIsInSelector() const
{
    for (const auto& button : buttons_)
        if (button->hasKeyboardFocus(true))
            return true;
    return false;
}
