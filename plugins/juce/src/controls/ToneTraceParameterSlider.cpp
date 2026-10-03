#include "ToneTraceParameterSlider.h"

ToneTraceParameterSlider::ToneTraceParameterSlider(
    const tonetrace::product::ContinuousControlSpec& spec)
    : spec_(spec)
{
    using namespace tonetrace::product;
    const auto& parameter = *spec_.parameter;

    setName(parameter.name);
    setTitle(parameter.name);
    setDescription(parameter.description);

    auto stepSpec = spec_;
    stepSpec.showPlus = false;
    const auto arrowStep = juce::String::fromUTF8(formatContinuous(stepSpec, spec_.keyboardStep, true).c_str());
    const auto pageStep = juce::String::fromUTF8(formatContinuous(stepSpec, spec_.pageStep, true).c_str());
    const auto help = juce::String("Arrow keys change ") + parameter.name + " by " + arrowStep
        + ". Page Up and Page Down change it by " + pageStep
        + ". Home selects the minimum and End selects the maximum. Enter opens exact value entry. T or F2 toggles Trace Curve. Mouse behavior uses JUCE's standard slider path.";
    setHelpText(help);

    setAccessible(true);
    setWantsKeyboardFocus(true);
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(parameter.minimum, parameter.maximum, spec_.publicResolution);
    setDoubleClickReturnValue(true, parameter.defaultValue);

    textFromValueFunction = [this](double value) { return formatValue(value, true); };
    valueFromTextFunction = [this](const juce::String& text)
    {
        double parsed = spec_.parameter->defaultValue;
        return parseExactText(text, parsed) ? parsed : spec_.parameter->defaultValue;
    };

    onDragStart = [this] { beginContinuousGesture(); };
    onDragEnd = [this] { endContinuousGesture(); };
    onValueChange = [this] { valueChangedByUser(); };
    setValue(parameter.defaultValue, juce::dontSendNotification);
}

bool ToneTraceParameterSlider::keyPressed(const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::returnKey)
    {
        if (onRequestExactEntry) onRequestExactEntry(*this);
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey)
    {
        setFromKeyboard(getValue() - spec_.keyboardStep);
        return true;
    }
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)
    {
        setFromKeyboard(getValue() + spec_.keyboardStep);
        return true;
    }
    if (code == juce::KeyPress::pageDownKey)
    {
        setFromKeyboard(getValue() - spec_.pageStep);
        return true;
    }
    if (code == juce::KeyPress::pageUpKey)
    {
        setFromKeyboard(getValue() + spec_.pageStep);
        return true;
    }
    if (code == juce::KeyPress::homeKey)
    {
        setFromKeyboard(spec_.parameter->minimum);
        return true;
    }
    if (code == juce::KeyPress::endKey)
    {
        setFromKeyboard(spec_.parameter->maximum);
        return true;
    }
    return juce::Slider::keyPressed(key);
}

void ToneTraceParameterSlider::setFromHost(double value)
{
    syncingFromHost_ = true;
    setValue(tonetrace::product::canonicalise(spec_, value), juce::sendNotificationSync);
    syncingFromHost_ = false;
}

void ToneTraceParameterSlider::setFromExactEntry(double value)
{
    setValue(tonetrace::product::canonicalise(spec_, value), juce::sendNotificationSync);
}

bool ToneTraceParameterSlider::parseExactText(const juce::String& text, double& value) const
{
    return tonetrace::product::parseContinuous(spec_, text.toStdString(), value);
}

juce::String ToneTraceParameterSlider::formatValue(double value, bool includeUnit) const
{
    return juce::String::fromUTF8(tonetrace::product::formatContinuous(spec_, value, includeUnit).c_str());
}

void ToneTraceParameterSlider::setFromKeyboard(double value)
{
    setValue(tonetrace::product::canonicalise(spec_, value), juce::sendNotificationSync);
}

void ToneTraceParameterSlider::valueChangedByUser()
{
    if (syncingFromHost_) return;
    if (!continuousGesture_ && onUserGestureBegin) onUserGestureBegin();
    if (onUserValueChanged) onUserValueChanged(getValue());
    if (!continuousGesture_ && onUserGestureEnd) onUserGestureEnd();
}

void ToneTraceParameterSlider::beginContinuousGesture()
{
    if (continuousGesture_) return;
    continuousGesture_ = true;
    if (onUserGestureBegin) onUserGestureBegin();
}

void ToneTraceParameterSlider::endContinuousGesture()
{
    if (!continuousGesture_) return;
    continuousGesture_ = false;
    if (onUserGestureEnd) onUserGestureEnd();
}
