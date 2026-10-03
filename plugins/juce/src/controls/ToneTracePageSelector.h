#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

// Owns the editor's page buttons and page-selection state. The buttons are
// direct children of the supplied parent component so normal JUCE focus and
// accessibility traversal sees the same controls that mouse users click.
class ToneTracePageSelector final
{
public:
    explicit ToneTracePageSelector(juce::Component& parent);

    // Called exactly once after an externally meaningful selection update.
    // userInitiated is false for structural rebuilds/snapshot selection and
    // true for keyboard or mouse navigation.
    std::function<void(int, bool userInitiated)> onPageChanged;

    void setPages(const std::vector<juce::String>& names, int selectedIndex);
    void selectPageProgrammatically(int index);
    bool selectRelativeFromUser(int delta);
    void setBounds(juce::Rectangle<int> bounds);

    [[nodiscard]] int selectedPage() const noexcept { return selectedIndex_; }
    [[nodiscard]] int pageCount() const noexcept { return static_cast<int>(buttons_.size()); }

private:
    class PageButton final : public juce::TextButton
    {
    public:
        static constexpr int kRadioGroupId = 0x54545243; // "TTRC"

        PageButton(ToneTracePageSelector& owner, juce::String text, int pageIndex);
        void setSelected(bool selected);
        bool keyPressed(const juce::KeyPress& key) override;
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;

    private:
        ToneTracePageSelector& owner_;
        int pageIndex_ = 0;
    };

    bool selectPage(int index, bool userInitiated, bool moveFocusToSelector);
    void updateButtonStates();
    void layoutButtons();
    [[nodiscard]] bool focusIsInSelector() const;

    juce::Component& parent_;
    std::vector<std::unique_ptr<PageButton>> buttons_;
    juce::Rectangle<int> bounds_;
    int selectedIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTracePageSelector)
};
