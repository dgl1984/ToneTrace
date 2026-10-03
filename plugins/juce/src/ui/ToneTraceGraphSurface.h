#pragma once

#include <JuceHeader.h>
#include "tonetrace/tonetrace_realtime.h"

#include <vector>

class ToneTraceGraphSurface final : public juce::Component
{
public:
    ToneTraceGraphSurface();
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void setTraceFrequency(double frequencyHz);
    std::function<void(double)> onInspectFrequency;

    void updateFromState(const tonetrace::ProfileSnapshot* snapshot,
                         const tonetrace::IrRenderSettings& renderSettings,
                         double maximumCorrectionDb);

private:
    struct Point { double frequencyHz = 0.0; double valueDb = 0.0; };
    std::vector<Point> reference_;
    std::vector<Point> target_;
    std::vector<Point> correction_;
    double cursorHz_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceGraphSurface)
};
