#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

#include "controls/ToneTraceBandSlider.h"
#include "controls/ToneTraceChoiceControl.h"
#include "controls/ToneTraceCommandButton.h"
#include "controls/ToneTraceParameterSlider.h"
#include "controls/ToneTracePageSelector.h"
#include "controls/ToneTraceReadOnlyField.h"
#include "controls/ToneTraceToggleControl.h"
#include "ui/ToneTraceGraphSurface.h"

class ToneTraceProcessor;

class ToneTraceEditor final : public juce::AudioProcessorEditor,
                              private juce::AudioProcessorParameter::Listener,
                              private juce::AsyncUpdater,
                              private juce::KeyListener
{
public:
    explicit ToneTraceEditor(ToneTraceProcessor&);
    ~ToneTraceEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

   #if defined(TONETRACE_VISUAL_SNAPSHOT_BUILD)
    void setSnapshotPage(int pageId);
    int getSnapshotPageCount() const noexcept;
    std::unique_ptr<juce::Component> createOptionsSnapshotComponent();
   #endif

private:
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    void updateVisualValueLabels();
    void updateStatusField();
    void updateGraph();
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    void toggleTrace();
    void moveBandFocus(std::size_t band, int delta);
    void updateCurveReadout(double frequencyHz, bool bandReadout, bool announce = false);
    bool rebuildViewPages(bool preserveFocus);
    void handlePageChanged(int pageIndex, bool userInitiated);
    void updatePageVisibility();
    void requestBandPageSweep(int pageIndex);
    void updateBandControls();
    void beginExactEntry(ToneTraceParameterSlider& target);
    void beginBandExactEntry(std::size_t bandIndex);
    void queueExactFinish(bool apply, bool returnFocus);
    void finishExactEntry(bool apply, bool returnFocus);
    void clearExactEntryError();
    void showExactEntryError(const ToneTraceParameterSlider& target);
    void showBandExactEntryError(const ToneTraceBandSlider& target);
    void showExportMenu();
    void showImportMenu();
    void chooseExport(int kind);
    void chooseExportAfterConfirmation(int kind);
    void chooseImport(int kind);
    void showOptionsDialog();
    void announceTransferResult(const juce::String& text, bool error);
    static void configureDecorativeLabel(juce::Label&, bool strong = false);

    ToneTraceProcessor& processor_;

    juce::Label heading_;
    juce::Label subtitle_;
    ToneTracePageSelector pageSelector_;
    ToneTraceGraphSurface graphSurface_;
    ToneTraceReadOnlyField curveReadout_ { "Curve Readout", "Exact frequency and curve values.", true };
    ToneTraceReadOnlyField curveDescription_ { "Curve Description", "Reference, Target, and applied correction description.", true };
    juce::Label curveDescriptionLabel_;
    juce::ToggleButton traceButton_ { "Trace Curve" };
    juce::TextButton copyDescriptionButton_ { "Copy Curve Description" };
    int traceIndex_ = 0;
    juce::Label controlsHeading_;
    ToneTraceCommandButton captureReferenceButton_;
    ToneTraceCommandButton learnTargetButton_;
    ToneTraceCommandButton correctTargetButton_;
    ToneTraceCommandButton freezeCorrectionButton_;
    juce::TextButton optionsButton_ { "Options..." };
    juce::Label matchModeLabel_;
    std::unique_ptr<ToneTraceChoiceControl> matchModeControl_;
    std::vector<std::unique_ptr<ToneTraceParameterSlider>> continuousControls_;
    std::vector<std::unique_ptr<juce::Label>> continuousLabels_;
    std::vector<std::unique_ptr<juce::Label>> continuousValueLabels_;
    std::vector<std::unique_ptr<ToneTraceToggleControl>> toggleControls_;

    juce::Label transferHeading_;
    juce::TextButton exportButton_ { "Export..." };
    juce::TextButton importButton_ { "Import..." };
    std::unique_ptr<juce::FileChooser> fileChooser_;

    juce::Label bandHeading_;
    std::vector<std::unique_ptr<ToneTraceBandSlider>> bandControls_;
    std::vector<std::unique_ptr<juce::Label>> bandFrequencyLabels_;
    std::vector<std::unique_ptr<juce::Label>> bandValueLabels_;
    std::vector<int> bandPageSizes_;
    int bandPageCount_ = 0;
    int lastResolution_ = -1;

    juce::Label statusLabel_;
    ToneTraceReadOnlyField statusField_;
    juce::Label exactEntryError_;

    std::unique_ptr<juce::TextEditor> exactEditor_;
    ToneTraceParameterSlider* exactTarget_ = nullptr;
    ToneTraceBandSlider* bandExactTarget_ = nullptr;
    bool exactFinishQueued_ = false;
    int lastRenderedStatus_ = -1;
    int lastRenderedCommand_ = -1;

    juce::Rectangle<int> graphPanelBounds_;
    juce::Rectangle<int> controlsPanelBounds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceEditor)
};
