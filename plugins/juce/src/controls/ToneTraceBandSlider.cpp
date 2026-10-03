#include "ToneTraceBandSlider.h"

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdlib>

ToneTraceBandSlider::ToneTraceBandSlider(std::size_t bandIndex, double frequencyHz)
    : bandIndex_(bandIndex), frequencyHz_(frequencyHz)
{
    setAccessible(true);
    setWantsKeyboardFocus(true);
    setSliderStyle(juce::Slider::LinearVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(-18.0, 18.0, 0.001);
    textFromValueFunction = [this](double value) { return formatValue(value); };
    valueFromTextFunction = [this](const juce::String& text)
    {
        double value = getValue();
        return parseExactText(text, value) ? value : getValue();
    };
    onValueChange = [this]
    {
        if (!syncing_ && onUserValueChanged) onUserValueChanged(bandIndex_, getValue());
    };
    updateAccessibilityText();
}

void ToneTraceBandSlider::focusGained(juce::Component::FocusChangeType cause)
{
    juce::Slider::focusGained(cause);
    if (onFocus) onFocus(bandIndex_);
}

void ToneTraceBandSlider::setBandIdentity(std::size_t bandIndex, double frequencyHz, double limitDb)
{
    bandIndex_ = bandIndex;
    frequencyHz_ = frequencyHz;
    limitDb_ = std::clamp(std::abs(limitDb), 1.0, 60.0);
    setRange(-limitDb_, limitDb_, 0.001);
    updateAccessibilityText();
}

void ToneTraceBandSlider::updateAccessibilityText()
{
    const auto frequency = frequencyText_.isNotEmpty() ? frequencyText_ : frequencyHz_ >= 1000.0
        ? juce::String(frequencyHz_ / 1000.0, frequencyHz_ < 10000.0 ? 2 : 1) + " kHz"
        : juce::String(frequencyHz_, frequencyHz_ < 100.0 ? 1 : 0) + " Hz";
    const auto name = "Band " + juce::String(static_cast<int>(bandIndex_ + 1)) + ", " + frequency;
    setName(name);
    setTitle(name);
    setDescription("Editable final correction at " + frequency + ".");
    setHelpText("Left and Right move between bands. Up and Down change by 1 dB. Page Up and Page Down change by 6 dB. Home selects the maximum and End selects the minimum. 0 or N neutralizes the band. Enter opens exact value entry. T or F2 toggles Trace Curve.");
}

bool ToneTraceBandSlider::keyPressed(const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::returnKey)
    {
        if (onRequestExactEntry) onRequestExactEntry(bandIndex_);
        return true;
    }
    if (code == 'T' || code == 't' || code == juce::KeyPress::F2Key)
    {
        if (onToggleTrace) onToggleTrace();
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        if (onNavigate) onNavigate(bandIndex_, code == juce::KeyPress::leftKey ? -1 : 1);
        return true;
    }
    if (code == juce::KeyPress::downKey) { setFromKeyboard(getValue() - 1.0); return true; }
    if (code == juce::KeyPress::upKey) { setFromKeyboard(getValue() + 1.0); return true; }
    if (code == juce::KeyPress::pageDownKey) { setFromKeyboard(getValue() - 6.0); return true; }
    if (code == juce::KeyPress::pageUpKey) { setFromKeyboard(getValue() + 6.0); return true; }
    if (code == juce::KeyPress::homeKey) { setFromKeyboard(limitDb_); return true; }
    if (code == juce::KeyPress::endKey) { setFromKeyboard(-limitDb_); return true; }
    if (code == '0' || key.getTextCharacter() == '0' || code == 'N' || code == 'n')
    {
        if (onNeutralize) onNeutralize(bandIndex_);
        return true;
    }
    return juce::Slider::keyPressed(key);
}

void ToneTraceBandSlider::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if (!isEnabled() || delta == 0.0f) return;
    setFromKeyboard(getValue() + (delta > 0.0f ? 1.0 : -1.0) * (event.mods.isShiftDown() ? 6.0 : 1.0));
}

void ToneTraceBandSlider::mouseDoubleClick(const juce::MouseEvent&)
{
    if (isEnabled() && onNeutralize) onNeutralize(bandIndex_);
}

void ToneTraceBandSlider::setFrequencyText(const juce::String& text)
{
    frequencyText_ = text;
    updateAccessibilityText();
}

std::unique_ptr<juce::AccessibilityHandler> ToneTraceBandSlider::createAccessibilityHandler()
{
    class BandValue final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit BandValue(ToneTraceBandSlider& owner) : owner_(owner) {}
        bool isReadOnly() const override { return false; }
        juce::String getCurrentValueAsString() const override { return owner_.formatValue(owner_.getValue()); }
        void setValueAsString(const juce::String& text) override
        {
            double value = 0.0;
            if (owner_.parseExactText(text, value)) owner_.setValue(value, juce::sendNotificationSync);
        }
    private:
        ToneTraceBandSlider& owner_;
    };
    // Released faders expose a unit-bearing string Value, not a percentage range.
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::slider,
        juce::AccessibilityActions{}, juce::AccessibilityHandler::Interfaces { std::make_unique<BandValue>(*this) });
}

void ToneTraceBandSlider::setFromKeyboard(double valueDb)
{
    setValue(std::clamp(valueDb, -limitDb_, limitDb_), juce::sendNotificationSync);
}

void ToneTraceBandSlider::setFromProcessor(double valueDb)
{
    syncing_ = true;
    setValue(std::clamp(valueDb, -limitDb_, limitDb_), juce::sendNotificationSync);
    syncing_ = false;
}

juce::String ToneTraceBandSlider::formatValue(double value) const
{
    if (std::abs(value) < 0.0005) value = 0.0;
    auto text = juce::String(value, 3);
    while (text.endsWithChar('0') && text.indexOfChar('.') < text.length() - 2)
        text = text.dropLastCharacters(1);
    return (value > 0.0 ? "+" : "") + text + " dB";
}

bool ToneTraceBandSlider::parseExactText(const juce::String& text, double& value) const
{
    auto cleaned = text.trim().toLowerCase().replace("db", "").trim();
    if (cleaned.isEmpty()) return false;
    const auto utf8 = cleaned.toRawUTF8();
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(utf8, &end);
    if (end == utf8 || errno == ERANGE || end == nullptr || *end != '\0'
        || !std::isfinite(parsed) || parsed < -limitDb_ || parsed > limitDb_)
        return false;
    value = parsed;
    return true;
}
