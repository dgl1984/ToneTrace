#pragma once

#include <JuceHeader.h>
#include "tonetrace/tonetrace_realtime.h"
#include <vector>

// One contract for the plotted curve and the exact pointer readout.
namespace tonetrace::ui
{
inline double displayedCorrectionDb(const ProfileSnapshot* snapshot,
                                    const IrRenderSettings& settings,
                                    double maximumCorrectionDb, double hz)
{
    if (snapshot != nullptr)
        return evaluateCorrectionAt(snapshot->uncappedModel, maximumCorrectionDb, settings, hz).outputDb;
    return settings.correctionGainDb
        + (hz >= settings.rangeLowHz && hz <= settings.rangeHighHz
            ? manualGainDbAt(hz, settings.manualGains, 20.0, 20000.0) : 0.0);
}
}

class ToneTraceGraphSurface final : public juce::Component
{
public:
    ToneTraceGraphSurface();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void setTraceFrequency(double frequencyHz);
    std::function<void(double)> onInspectFrequency;
    void updateFromState(const tonetrace::ProfileSnapshot*, const tonetrace::IrRenderSettings&, double);

private:
    struct Point { double frequencyHz = 0.0; double valueDb = 0.0; };
    juce::Rectangle<float> plotBounds() const;
    float xFor(double hz) const;
    void rebuildPaths();
    std::vector<Point> reference_, target_, correction_;
    juce::Path referencePath_, targetPath_, correctionPath_, dashedTargetPath_;
    double rangeDb_ = 12.0;
    double cursorHz_ = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceGraphSurface)
};
