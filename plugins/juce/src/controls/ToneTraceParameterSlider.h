#pragma once

#include <JuceHeader.h>
#include "../ToneTraceProductCatalog.h"

class ToneTraceParameterSlider final : public juce::Slider
{
public:
    explicit ToneTraceParameterSlider(const tonetrace::product::ContinuousControlSpec&);
    ~ToneTraceParameterSlider() override = default;

    std::function<void(ToneTraceParameterSlider&)> onRequestExactEntry;
    std::function<void()> onUserGestureBegin;
    std::function<void(double)> onUserValueChanged;
    std::function<void()> onUserGestureEnd;

    bool keyPressed(const juce::KeyPress&) override;

    const tonetrace::product::ContinuousControlSpec& spec() const noexcept { return spec_; }
    void setFromHost(double value);
    void setFromExactEntry(double value);
    bool parseExactText(const juce::String&, double&) const;
    juce::String formatValue(double value, bool includeUnit = true) const;

private:
    void setFromKeyboard(double value);
    void valueChangedByUser();
    void beginContinuousGesture();
    void endContinuousGesture();

    const tonetrace::product::ContinuousControlSpec& spec_;
    bool syncingFromHost_ = false;
    bool continuousGesture_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceParameterSlider)
};
