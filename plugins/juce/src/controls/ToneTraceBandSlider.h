#pragma once

#include <JuceHeader.h>

#include <cstddef>

class ToneTraceBandSlider final : public juce::Slider
{
public:
    ToneTraceBandSlider(std::size_t bandIndex, double frequencyHz);

    std::function<void(std::size_t)> onRequestExactEntry;
    std::function<void(std::size_t, double)> onUserValueChanged;
    std::function<void(std::size_t)> onNeutralize;
    std::function<void(std::size_t)> onFocus;
    std::function<void(std::size_t, int)> onNavigate;
    std::function<void()> onToggleTrace;

    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void setFrequencyText(const juce::String&);
    void setBandIdentity(std::size_t bandIndex, double frequencyHz, double limitDb);
    void setFromProcessor(double valueDb);
    [[nodiscard]] std::size_t bandIndex() const noexcept { return bandIndex_; }
    [[nodiscard]] double frequencyHz() const noexcept { return frequencyHz_; }
    [[nodiscard]] juce::String formatValue(double value) const;
    [[nodiscard]] bool parseExactText(const juce::String&, double&) const;

private:
    void setFromKeyboard(double valueDb);
    void updateAccessibilityText();

    std::size_t bandIndex_ = 0;
    double frequencyHz_ = 1000.0;
    double limitDb_ = 18.0;
    bool syncing_ = false;
    juce::String frequencyText_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceBandSlider)
};
