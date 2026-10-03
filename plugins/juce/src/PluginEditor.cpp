#include "PluginEditor.h"
#include "tonetrace/tonetrace_describe.h"
#include "PluginProcessor.h"
#include "ui/ToneTraceDesignSystem.h"
#include "ui/ToneTraceBandPageLayout.h"

#include <filesystem>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>


namespace
{
struct ToneTraceOptionsValues
{
    bool fullCorrectionRange = false;
    bool toneNotifications = true;
    double confidenceToneVolumeDb = -12.0;
    bool bypass = false;
};

std::filesystem::path toneTraceFilesystemPath(const juce::File& file)
{
    #if JUCE_WINDOWS
    return std::filesystem::path(file.getFullPathName().toWideCharPointer());
   #else
    return std::filesystem::path(file.getFullPathName().toStdString());
   #endif
}


class ToneTraceOptionsComponent final : public juce::Component
{
public:
    ToneTraceOptionsComponent(ToneTraceOptionsValues initial,
                              std::function<void(const ToneTraceOptionsValues&)> apply,
                              std::function<void()> reset)
        : onApply_(std::move(apply)), onReset_(std::move(reset))
    {
        using namespace tonetrace::product;
        using namespace tonetrace::ui;

        setAccessible(true);
        setTitle("Tone Trace EQ Options");
        setDescription("Tone Trace options for Full Correction Range, tone notifications, confidence tone volume, bypass, and reset.");
        setSize(470, 330);

        fullRange_.setButtonText("Full Correction Range");
        fullRange_.setToggleState(initial.fullCorrectionRange, juce::dontSendNotification);
        fullRange_.setDescription("Allow the learned correction to use the full internal correction range.");

        toneNotifications_.setButtonText("Tone Notifications");
        toneNotifications_.setToggleState(initial.toneNotifications, juce::dontSendNotification);
        toneNotifications_.setDescription("Enable confidence, warning, page, and trace notification tones.");

        toneLabel_.setText("Confidence Tone Volume", juce::dontSendNotification);
        toneLabel_.setAccessible(false);
        toneLabel_.setInterceptsMouseClicks(false, false);
        toneEditor_.setName("Confidence Tone Volume");
        toneEditor_.setTitle("Confidence Tone Volume");
        toneEditor_.setDescription("Enter a level from -60 dB through -12 dB. Enter Off for -60 dB.");
        toneEditor_.setText(juce::String::fromUTF8(formatContinuous(
            kImplementedContinuousControls[kConfidenceToneVolumeIndex],
            initial.confidenceToneVolumeDb).c_str()), false);
        toneEditor_.setSelectAllWhenFocused(true);

        bypass_.setButtonText("Bypass");
        bypass_.setToggleState(initial.bypass, juce::dontSendNotification);
        bypass_.setDescription("Crossfade Tone Trace to dry input and suppress correction, generated tones, and clip guard on output.");

        resetButton_.setButtonText("Reset Tone Trace...");
        resetButton_.setDescription("Reset clears the learned Reference/Target match, manual band edits, Correction Gain, and current capture state after confirmation.");
        resetExplanation_.setText("Reset clears the learned Reference/Target match, manual band edits, Correction Gain, and current capture state.", juce::dontSendNotification);
        resetExplanation_.setAccessible(false);
        resetExplanation_.setInterceptsMouseClicks(false, false);

        error_.setColour(juce::Label::textColourId, colour::warning);
        error_.setAccessible(true);
        error_.setVisible(false);
        error_.setInterceptsMouseClicks(false, false);

        okButton_.setButtonText("OK");
        cancelButton_.setButtonText("Cancel");

        for (auto* c : { static_cast<juce::Component*>(&fullRange_), static_cast<juce::Component*>(&toneNotifications_),
                         static_cast<juce::Component*>(&toneLabel_), static_cast<juce::Component*>(&toneEditor_),
                         static_cast<juce::Component*>(&bypass_), static_cast<juce::Component*>(&resetButton_),
                         static_cast<juce::Component*>(&resetExplanation_), static_cast<juce::Component*>(&error_),
                         static_cast<juce::Component*>(&okButton_), static_cast<juce::Component*>(&cancelButton_) })
            addAndMakeVisible(*c);

        int order = 1;
        fullRange_.setExplicitFocusOrder(order++);
        toneNotifications_.setExplicitFocusOrder(order++);
        toneEditor_.setExplicitFocusOrder(order++);
        bypass_.setExplicitFocusOrder(order++);
        resetButton_.setExplicitFocusOrder(order++);
        okButton_.setExplicitFocusOrder(order++);
        cancelButton_.setExplicitFocusOrder(order++);

        okButton_.onClick = [this] { applyAndClose(); };
        cancelButton_.onClick = [this] { closeDialog(); };
        resetButton_.onClick = [this] { confirmReset(); };
    }

    void focusInitialControl()
    {
        // A JUCE DialogWindow receives native focus as it enters modal state,
        // but it does not automatically move keyboard/accessibility focus to
        // the first child control. The released Win32 dialog manager did.
        // Restore that behavior explicitly so keyboard and screen-reader users
        // land on a real option without first clicking the dialog.
        fullRange_.grabKeyboardFocus();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        fullRange_.setBounds(area.removeFromTop(32));
        area.removeFromTop(8);
        toneNotifications_.setBounds(area.removeFromTop(32));
        area.removeFromTop(8);

        auto toneRow = area.removeFromTop(36);
        toneLabel_.setBounds(toneRow.removeFromLeft(205));
        toneRow.removeFromLeft(8);
        toneEditor_.setBounds(toneRow);
        area.removeFromTop(8);

        bypass_.setBounds(area.removeFromTop(32));
        area.removeFromTop(10);

        auto resetRow = area.removeFromTop(46);
        resetButton_.setBounds(resetRow.removeFromLeft(165));
        resetRow.removeFromLeft(10);
        resetExplanation_.setBounds(resetRow);
        area.removeFromTop(6);
        error_.setBounds(area.removeFromTop(32));

        auto buttons = area.removeFromBottom(34);
        cancelButton_.setBounds(buttons.removeFromRight(100));
        buttons.removeFromRight(8);
        okButton_.setBounds(buttons.removeFromRight(100));
    }

private:
    void closeDialog()
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState(0);
    }

    void applyAndClose()
    {
        using namespace tonetrace::product;
        double toneLevel = 0.0;
        const auto text = toneEditor_.getText().toStdString();
        if (!parseContinuous(kImplementedContinuousControls[kConfidenceToneVolumeIndex], text, toneLevel))
        {
            error_.setText("Enter a valid Confidence Tone Volume, for example -12 dB, or Off.", juce::dontSendNotification);
            error_.setVisible(true);
            juce::AccessibilityHandler::postAnnouncement(error_.getText(), juce::AccessibilityHandler::AnnouncementPriority::high);
            toneEditor_.grabKeyboardFocus();
            toneEditor_.selectAll();
            return;
        }

        ToneTraceOptionsValues values;
        values.fullCorrectionRange = fullRange_.getToggleState();
        values.toneNotifications = toneNotifications_.getToggleState();
        values.confidenceToneVolumeDb = toneLevel;
        values.bypass = bypass_.getToggleState();
        if (onApply_) onApply_(values);
        closeDialog();
    }

    void confirmReset()
    {
        const auto safe = juce::Component::SafePointer<ToneTraceOptionsComponent>(this);
        juce::NativeMessageBox::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon,
            "Reset Tone Trace EQ?",
            "Reset Tone Trace? This clears the learned Reference/Target match, manual band edits, Correction Gain, and current capture state.\n\nThis cannot be undone. Continue?",
            this,
            juce::ModalCallbackFunction::create([safe](int result)
            {
                if (safe == nullptr || result != 1) return;
                if (safe->onReset_) safe->onReset_();
                safe->closeDialog();
            }));
    }

    juce::ToggleButton fullRange_;
    juce::ToggleButton toneNotifications_;
    juce::Label toneLabel_;
    juce::TextEditor toneEditor_;
    juce::ToggleButton bypass_;
    juce::TextButton resetButton_;
    juce::Label resetExplanation_;
    juce::Label error_;
    juce::TextButton okButton_;
    juce::TextButton cancelButton_;
    std::function<void(const ToneTraceOptionsValues&)> onApply_;
    std::function<void()> onReset_;
};
} // namespace

void ToneTraceEditor::configureDecorativeLabel(juce::Label& label, bool strong)
{
    using namespace tonetrace::ui;
    label.setFont(strong ? type::section() : type::control());
    label.setColour(juce::Label::textColourId, strong ? colour::text : colour::mutedText);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setAccessible(false);
    label.setInterceptsMouseClicks(false, false);
}

ToneTraceEditor::ToneTraceEditor(ToneTraceProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor),
      pageSelector_(*this),
      captureReferenceButton_(
          "Capture Reference",
          "Start or restart Reference capture. Only accepted audio counts toward Capture Time."),
      learnTargetButton_(
          "Learn Target",
          "Save a usable Reference and begin learning the Target. If the Reference is not ready, Tone Trace keeps capturing it and reports that more audio is needed."),
      correctTargetButton_(
          "Correct Target",
          "Analyze the captured Reference and Target with the released Tone Trace 1.0.5 match engine and begin Preview correction."),
      freezeCorrectionButton_(
          "Freeze Correction",
          "Freeze the current validated correction. Capture is stopped; retained spectra remain available for later Match Mode reinterpretation."),
      statusField_(
          "Workflow status and progress",
          "Read-only workflow status, last action, accepted-audio time, confidence, and curve drift.",
          true)
{
    using namespace tonetrace::product;
    using namespace tonetrace::ui;

    // Keep the human-passed JUCE/VST3 boundary: accessible editor root, no
    // nested focus-container experiment, no startup focus grab, no REAPER wrapper
    // manipulation, and direct interactive children.
    setAccessible(true);
    setTitle("Tone Trace 1.1");
    setDescription("Tone Trace EQ. Capture, correction, band editing, curve inspection, import and export.");

    heading_.setText("Tone Trace 1.1", juce::dontSendNotification);
    heading_.setFont(type::title());
    heading_.setColour(juce::Label::textColourId, colour::text);
    heading_.setJustificationType(juce::Justification::centredLeft);
    heading_.setAccessible(false);
    heading_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(heading_);

    subtitle_.setText("Capture a Reference. Learn a Target. Refine the correction.",
                      juce::dontSendNotification);
    subtitle_.setFont(type::subtitle());
    subtitle_.setColour(juce::Label::textColourId, colour::mutedText);
    subtitle_.setJustificationType(juce::Justification::centredLeft);
    subtitle_.setAccessible(false);
    subtitle_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(subtitle_);

    pageSelector_.onPageChanged = [this](int pageIndex, bool userInitiated)
    {
        handlePageChanged(pageIndex, userInitiated);
    };
    addAndMakeVisible(graphSurface_);
    for (auto* field : { &curveReadout_, &curveDescription_ })
    {
        field->setFont(type::small());
        field->setColour(juce::TextEditor::backgroundColourId, colour::panelRaised);
        field->setColour(juce::TextEditor::textColourId, colour::text);
        addAndMakeVisible(*field);
    }
    curveReadout_.setContent("Curve Readout: move over the graph or use Trace Curve to inspect exact values.", false);
    curveDescriptionLabel_.setText("Curve Description", juce::dontSendNotification);
    configureDecorativeLabel(curveDescriptionLabel_, true);
    addAndMakeVisible(curveDescriptionLabel_);
    traceButton_.setWantsKeyboardFocus(true);
    traceButton_.setHelpText("T or F2 toggles Trace Curve. Left and Right inspect bands; Up and Down adjust the selected band.");
    traceButton_.onClick = [this]
    {
        traceButton_.setToggleState(!traceButton_.getToggleState(), juce::dontSendNotification);
        toggleTrace();
    };
    addAndMakeVisible(traceButton_);
    copyDescriptionButton_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(curveDescription_.getText()); };
    addAndMakeVisible(copyDescriptionButton_);
    graphSurface_.onInspectFrequency = [this](double frequency)
    {
        if (!traceButton_.getToggleState()) updateCurveReadout(frequency, false);
    };

    controlsHeading_.setText("Capture and match controls", juce::dontSendNotification);
    configureDecorativeLabel(controlsHeading_, true);
    addAndMakeVisible(controlsHeading_);

    for (auto* button : { &captureReferenceButton_, &learnTargetButton_,
                          &correctTargetButton_, &freezeCorrectionButton_ })
    {
        button->setColour(juce::TextButton::buttonColourId, colour::panelRaised);
        button->setColour(juce::TextButton::buttonOnColourId, colour::accent);
        button->setColour(juce::TextButton::textColourOffId, colour::text);
        button->setColour(juce::TextButton::textColourOnId, colour::background);
        addAndMakeVisible(*button);
    }
    captureReferenceButton_.onCommand = [this] { processor_.setWorkflowFromUi(1); };
    learnTargetButton_.onCommand = [this] { processor_.setWorkflowFromUi(2); };
    correctTargetButton_.onCommand = [this] { processor_.setWorkflowFromUi(3); };
    freezeCorrectionButton_.onCommand = [this] { processor_.setWorkflowFromUi(4); };

    optionsButton_.setAccessible(true);
    optionsButton_.setWantsKeyboardFocus(true);
    optionsButton_.setDescription("Open Full Correction Range, tone, bypass, and reset options.");
    optionsButton_.setColour(juce::TextButton::buttonColourId, colour::panelRaised);
    optionsButton_.setColour(juce::TextButton::textColourOffId, colour::text);
    optionsButton_.onClick = [this] { showOptionsDialog(); };
    addAndMakeVisible(optionsButton_);

    matchModeLabel_.setText(kImplementedMatchMode.parameter->name, juce::dontSendNotification);
    configureDecorativeLabel(matchModeLabel_);
    addAndMakeVisible(matchModeLabel_);

    matchModeControl_ = std::make_unique<ToneTraceChoiceControl>(kImplementedMatchMode);
    matchModeControl_->setColour(juce::ComboBox::backgroundColourId, colour::panelRaised);
    matchModeControl_->setColour(juce::ComboBox::textColourId, colour::text);
    matchModeControl_->setColour(juce::ComboBox::outlineColourId, colour::border);
    matchModeControl_->setColour(juce::ComboBox::arrowColourId, colour::accentStrong);
    matchModeControl_->onUserGestureBegin = [this] { processor_.beginMatchModeGesture(); };
    matchModeControl_->onUserIndexChanged = [this](int index)
    {
        processor_.setMatchModeFromUi(index);
        // A capture locks Match Mode. If a custom-editor change is rejected,
        // restore the ComboBox immediately instead of waiting for a host echo.
        if (processor_.captureActive())
            matchModeControl_->setFromHostIndex(processor_.matchModeIndex());
    };
    matchModeControl_->onUserGestureEnd = [this] { processor_.endMatchModeGesture(); };
    processor_.matchModeParameter().addListener(this);
    matchModeControl_->setFromHostIndex(processor_.matchModeIndex());
    addAndMakeVisible(*matchModeControl_);

    continuousControls_.reserve(kImplementedContinuousControls.size());
    continuousLabels_.reserve(kImplementedContinuousControls.size());
    continuousValueLabels_.reserve(kImplementedContinuousControls.size());

    for (std::size_t index = 0; index < kImplementedContinuousControls.size(); ++index)
    {
        const auto& spec = kImplementedContinuousControls[index];
        auto label = std::make_unique<juce::Label>();
        label->setText(spec.parameter->name, juce::dontSendNotification);
        configureDecorativeLabel(*label);
        addAndMakeVisible(*label);
        continuousLabels_.push_back(std::move(label));

        auto valueLabel = std::make_unique<juce::Label>();
        configureDecorativeLabel(*valueLabel, true);
        valueLabel->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*valueLabel);
        continuousValueLabels_.push_back(std::move(valueLabel));

        auto control = std::make_unique<ToneTraceParameterSlider>(spec);
        auto* controlPtr = control.get();
        control->setColour(juce::Slider::backgroundColourId, colour::grid);
        control->setColour(juce::Slider::trackColourId, colour::accent);
        control->setColour(juce::Slider::thumbColourId, colour::accentStrong);
        control->onRequestExactEntry = [this](ToneTraceParameterSlider& target) { beginExactEntry(target); };
        control->onUserGestureBegin = [this, index] { processor_.beginContinuousGesture(index); };
        control->onUserValueChanged = [this, index](double value) { processor_.setContinuousFromUi(index, value); };
        control->onUserGestureEnd = [this, index] { processor_.endContinuousGesture(index); };

        processor_.continuousParameter(index).addListener(this);
        control->setFromHost(processor_.continuousValue(index));
        addAndMakeVisible(*controlPtr);
        continuousControls_.push_back(std::move(control));
    }

    toggleControls_.reserve(kImplementedToggleControls.size());
    for (std::size_t index = 0; index < kImplementedToggleControls.size(); ++index)
    {
        auto control = std::make_unique<ToneTraceToggleControl>(*kImplementedToggleControls[index]);
        control->setColour(juce::ToggleButton::textColourId, colour::text);
        control->setColour(juce::ToggleButton::tickColourId, colour::accentStrong);
        control->setColour(juce::ToggleButton::tickDisabledColourId, colour::mutedText);
        control->onUserToggle = [this, index](bool value)
        {
            processor_.beginToggleGesture(index);
            processor_.setToggleFromUi(index, value);
            processor_.endToggleGesture(index);
        };
        processor_.toggleParameter(index).addListener(this);
        control->setFromHost(processor_.toggleValue(index));
        addAndMakeVisible(*control);
        toggleControls_.push_back(std::move(control));
    }

    transferHeading_.setText("Import and export", juce::dontSendNotification);
    configureDecorativeLabel(transferHeading_, true);
    addAndMakeVisible(transferHeading_);

    exportButton_.setAccessible(true);
    exportButton_.setWantsKeyboardFocus(true);
    exportButton_.setDescription("Open a menu to export an impulse response, Reference capture, Target capture, or Tone Trace model.");
    exportButton_.onClick = [this] { showExportMenu(); };
    addAndMakeVisible(exportButton_);

    importButton_.setAccessible(true);
    importButton_.setWantsKeyboardFocus(true);
    importButton_.setDescription("Open a menu to import a Reference capture, Target capture, or Tone Trace model.");
    importButton_.onClick = [this] { showImportMenu(); };
    addAndMakeVisible(importButton_);

    bandHeading_.setText("Editable correction bands", juce::dontSendNotification);
    configureDecorativeLabel(bandHeading_, true);
    addAndMakeVisible(bandHeading_);

    statusLabel_.setText("Workflow status", juce::dontSendNotification);
    configureDecorativeLabel(statusLabel_, true);
    addAndMakeVisible(statusLabel_);

    statusField_.setFont(type::small());
    statusField_.setColour(juce::TextEditor::backgroundColourId, colour::panelRaised);
    statusField_.setColour(juce::TextEditor::textColourId, colour::text);
    statusField_.setColour(juce::TextEditor::outlineColourId, colour::border);
    statusField_.setColour(juce::TextEditor::focusedOutlineColourId, colour::focus);
    statusField_.onFocusLost = [this] { triggerAsyncUpdate(); };
    addAndMakeVisible(statusField_);

    for (auto* parameter : {
             static_cast<juce::AudioProcessorParameter*>(&processor_.statusParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.lastCommandParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.confidenceParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.curveDriftParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.captureTimeParameter()) })
        parameter->addListener(this);

    exactEntryError_.setFont(type::small());
    exactEntryError_.setColour(juce::Label::textColourId, colour::warning);
    exactEntryError_.setJustificationType(juce::Justification::centredLeft);
    exactEntryError_.setAccessible(true);
    exactEntryError_.setVisible(false);
    exactEntryError_.setInterceptsMouseClicks(false, false);
    addChildComponent(exactEntryError_);

    // The product focus order contains only functional/readable controls.
    // Returning null at either end lets the host own entry/exit instead of
    // trapping focus inside the VST3 editor.
    int focusOrder = 1;
    ++focusOrder; // focus order 1 belongs to the selected page selector button (roving focus).
    captureReferenceButton_.setExplicitFocusOrder(focusOrder++);
    learnTargetButton_.setExplicitFocusOrder(focusOrder++);
    correctTargetButton_.setExplicitFocusOrder(focusOrder++);
    freezeCorrectionButton_.setExplicitFocusOrder(focusOrder++);
    matchModeControl_->setExplicitFocusOrder(focusOrder++);
    optionsButton_.setExplicitFocusOrder(focusOrder++);
    for (auto& control : continuousControls_) control->setExplicitFocusOrder(focusOrder++);
    for (auto& control : toggleControls_) control->setExplicitFocusOrder(focusOrder++);
    exportButton_.setExplicitFocusOrder(focusOrder++);
    importButton_.setExplicitFocusOrder(focusOrder++);
    statusField_.setExplicitFocusOrder(focusOrder++);
    curveReadout_.setExplicitFocusOrder(focusOrder++);
    traceButton_.setExplicitFocusOrder(focusOrder++);
    curveDescription_.setExplicitFocusOrder(focusOrder++);
    copyDescriptionButton_.setExplicitFocusOrder(focusOrder++);

    rebuildViewPages(false);
    updateGraph();
    updateVisualValueLabels();
    updateStatusField();

    // IMPORTANT: do not configure resize constraints until every child used by
    // resized() exists. JUCE's setResizeLimits() constrains the editor's current
    // bounds; from the constructor's initial 0x0 size that can synchronously
    // invoke our resized() override. Product Foundation 01 enabled the limits
    // above child construction, so resized() dereferenced matchModeControl_
    // before it existed and could crash the host while opening the editor.
    setSize(defaultWidth, defaultHeight);
    setResizeLimits(minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    setResizable(true, true);
    for (auto* child : getChildren()) child->addKeyListener(this);
}

ToneTraceEditor::~ToneTraceEditor()
{
    cancelPendingUpdate();
    for (std::size_t index = 0; index < continuousControls_.size(); ++index)
        processor_.continuousParameter(index).removeListener(this);
    for (std::size_t index = 0; index < toggleControls_.size(); ++index)
        processor_.toggleParameter(index).removeListener(this);
    processor_.matchModeParameter().removeListener(this);
    for (auto* parameter : {
             static_cast<juce::AudioProcessorParameter*>(&processor_.statusParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.lastCommandParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.confidenceParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.curveDriftParameter()),
             static_cast<juce::AudioProcessorParameter*>(&processor_.captureTimeParameter()) })
        parameter->removeListener(this);
    exactEditor_.reset();
}

void ToneTraceEditor::parameterValueChanged(int, float)
{
    triggerAsyncUpdate();
}

void ToneTraceEditor::parameterGestureChanged(int, bool)
{
}

void ToneTraceEditor::handleAsyncUpdate()
{
    for (std::size_t index = 0; index < continuousControls_.size(); ++index)
        continuousControls_[index]->setFromHost(processor_.continuousValue(index));
    for (std::size_t index = 0; index < toggleControls_.size(); ++index)
        toggleControls_[index]->setFromHost(processor_.toggleValue(index));
    matchModeControl_->setFromHostIndex(processor_.matchModeIndex());
    const bool pagesChanged = rebuildViewPages(true);
    if (pagesChanged) resized();
    if (pageSelector_.selectedPage() >= 2) updateBandControls();
    updateGraph();
    updateVisualValueLabels();
    updateStatusField();
}

void ToneTraceEditor::updateVisualValueLabels()
{
    for (std::size_t index = 0; index < continuousControls_.size(); ++index)
        continuousValueLabels_[index]->setText(
            continuousControls_[index]->formatValue(processor_.continuousValue(index), true),
            juce::dontSendNotification);
}

void ToneTraceEditor::updateStatusField()
{
    const int status = processor_.statusCode();
    const int command = processor_.lastCommandCode();
    const bool semanticChange = status != lastRenderedStatus_ || command != lastRenderedCommand_;

    // While a screen-reader user is reading this field, avoid replacing its
    // value for every 50 ms Capture Time / Curve Drift change. Status and Last
    // Action transitions still update immediately because they carry meaning.
    if (!statusField_.hasKeyboardFocus(true) || semanticChange)
    {
        statusField_.setContent(processor_.statusSummary(), false);
        lastRenderedStatus_ = status;
        lastRenderedCommand_ = command;
    }
}

void ToneTraceEditor::beginExactEntry(ToneTraceParameterSlider& target)
{
    if (exactEditor_ != nullptr) return;

    clearExactEntryError();
    bandExactTarget_ = nullptr;
    exactTarget_ = &target;
    const auto& parameter = *target.spec().parameter;

    exactEditor_ = std::make_unique<juce::TextEditor>();
    const auto exactName = juce::String(parameter.name) + " exact value";
    exactEditor_->setName(exactName);
    exactEditor_->setTitle(exactName);
    exactEditor_->setDescription("Type an exact value for " + juce::String(parameter.name) + ".");
    exactEditor_->setAccessible(true);
    exactEditor_->setMultiLine(false);
    exactEditor_->setReturnKeyStartsNewLine(false);
    exactEditor_->setEscapeAndReturnKeysConsumed(false);
    exactEditor_->setSelectAllWhenFocused(true);
    exactEditor_->setJustification(juce::Justification::centred);
    exactEditor_->setExplicitFocusOrder(target.getExplicitFocusOrder());
    exactEditor_->setText(target.formatValue(target.getValue(), false), false);
    exactEditor_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    exactEditor_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
    exactEditor_->setColour(juce::TextEditor::outlineColourId, tonetrace::ui::colour::focus);

    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    exactEditor_->onReturnKey = [safe]
    {
        if (safe != nullptr) safe->queueExactFinish(true, true);
    };
    exactEditor_->onEscapeKey = [safe]
    {
        if (safe != nullptr) safe->queueExactFinish(false, true);
    };
    exactEditor_->onFocusLost = [safe]
    {
        if (safe != nullptr) safe->queueExactFinish(false, false);
    };

    addAndMakeVisible(*exactEditor_);
    target.setVisible(false);
    resized();
    exactEditor_->selectAll();
    exactEditor_->grabKeyboardFocus();
}

void ToneTraceEditor::queueExactFinish(bool apply, bool returnFocus)
{
    if (exactEditor_ == nullptr || (exactTarget_ == nullptr && bandExactTarget_ == nullptr) || exactFinishQueued_) return;
    exactFinishQueued_ = true;
    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    juce::MessageManager::callAsync([safe, apply, returnFocus]
    {
        if (safe != nullptr) safe->finishExactEntry(apply, returnFocus);
    });
}

void ToneTraceEditor::finishExactEntry(bool apply, bool returnFocus)
{
    if (exactEditor_ == nullptr || (exactTarget_ == nullptr && bandExactTarget_ == nullptr)) return;

    exactFinishQueued_ = false;
    const auto enteredText = exactEditor_->getText();

    if (bandExactTarget_ != nullptr)
    {
        auto* target = bandExactTarget_;
        double parsed = target->getValue();
        const bool applied = apply && target->parseExactText(enteredText, parsed);
        exactEditor_->setVisible(false);
        target->setVisible(true);
        if (returnFocus) target->grabKeyboardFocus();
        removeChildComponent(exactEditor_.get());
        exactEditor_.reset();
        bandExactTarget_ = nullptr;
        if (applied)
        {
            clearExactEntryError();
            processor_.setBandValueDb(target->bandIndex(), parsed);
            target->setFromProcessor(parsed);
        }
        else if (apply)
            showBandExactEntryError(*target);
        updateBandControls();
        resized();
        return;
    }

    auto* target = exactTarget_;
    double parsed = target->getValue();
    const bool applied = apply && target->parseExactText(enteredText, parsed);

    exactEditor_->setVisible(false);
    target->setVisible(true);
    if (returnFocus) target->grabKeyboardFocus();
    removeChildComponent(exactEditor_.get());
    exactEditor_.reset();
    exactTarget_ = nullptr;

    if (applied)
    {
        clearExactEntryError();
        target->setFromExactEntry(parsed);
    }
    else if (apply)
        showExactEntryError(*target);
    updateVisualValueLabels();
    resized();
}

void ToneTraceEditor::clearExactEntryError()
{
    exactEntryError_.setVisible(false);
    exactEntryError_.setText({}, juce::dontSendNotification);
}

void ToneTraceEditor::showExactEntryError(const ToneTraceParameterSlider& target)
{
    const auto& control = target.spec();
    const auto& parameter = *control.parameter;
    const auto name = juce::String(parameter.name) + " exact value error";
    const auto minimum = target.formatValue(parameter.minimum, true);
    const auto maximum = target.formatValue(parameter.maximum, true);
    const juce::String message = "Invalid " + juce::String(parameter.name) + ". Enter a value from "
        + minimum + " to " + maximum + ". The previous value was kept.";

    exactEntryError_.setName(name);
    exactEntryError_.setTitle(name);
    exactEntryError_.setDescription("Exact value entry error message.");
    exactEntryError_.setText(message, juce::dontSendNotification);
    exactEntryError_.setVisible(true);
    juce::AccessibilityHandler::postAnnouncement(
        message, juce::AccessibilityHandler::AnnouncementPriority::high);
}

void ToneTraceEditor::beginBandExactEntry(std::size_t bandIndex)
{
    if (exactEditor_ != nullptr || bandIndex >= bandControls_.size()) return;
    clearExactEntryError();
    exactTarget_ = nullptr;
    bandExactTarget_ = bandControls_[bandIndex].get();
    auto& target = *bandExactTarget_;

    exactEditor_ = std::make_unique<juce::TextEditor>();
    const auto exactName = target.getName() + " exact value";
    exactEditor_->setName(exactName);
    exactEditor_->setTitle(exactName);
    exactEditor_->setDescription("Type an exact final correction value in dB for " + target.getName() + ".");
    exactEditor_->setAccessible(true);
    exactEditor_->setMultiLine(false);
    exactEditor_->setReturnKeyStartsNewLine(false);
    exactEditor_->setEscapeAndReturnKeysConsumed(false);
    exactEditor_->setSelectAllWhenFocused(true);
    exactEditor_->setJustification(juce::Justification::centred);
    exactEditor_->setExplicitFocusOrder(target.getExplicitFocusOrder());
    exactEditor_->setText(target.formatValue(target.getValue()), false);
    exactEditor_->setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    exactEditor_->setColour(juce::TextEditor::textColourId, juce::Colours::black);
    exactEditor_->setColour(juce::TextEditor::outlineColourId, tonetrace::ui::colour::focus);

    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    exactEditor_->onReturnKey = [safe] { if (safe != nullptr) safe->queueExactFinish(true, true); };
    exactEditor_->onEscapeKey = [safe] { if (safe != nullptr) safe->queueExactFinish(false, true); };
    exactEditor_->onFocusLost = [safe] { if (safe != nullptr) safe->queueExactFinish(false, false); };
    addAndMakeVisible(*exactEditor_);
    target.setVisible(false);
    resized();
    exactEditor_->selectAll();
    exactEditor_->grabKeyboardFocus();
}

void ToneTraceEditor::showBandExactEntryError(const ToneTraceBandSlider& target)
{
    const auto limit = processor_.bandLimitDb();
    const juce::String message = "Invalid " + target.getName() + ". Enter a value from "
        + juce::String(-limit, 3) + " dB to +" + juce::String(limit, 3)
        + " dB. The previous value was kept.";
    exactEntryError_.setName(target.getName() + " exact value error");
    exactEntryError_.setTitle(exactEntryError_.getName());
    exactEntryError_.setDescription("Exact band value entry error message.");
    exactEntryError_.setText(message, juce::dontSendNotification);
    exactEntryError_.setVisible(true);
    juce::AccessibilityHandler::postAnnouncement(message, juce::AccessibilityHandler::AnnouncementPriority::high);
}

void ToneTraceEditor::updateGraph()
{
    auto snapshot = processor_.profileSnapshotCopy();
    graphSurface_.updateFromState(snapshot.get(), processor_.graphRenderSettings(),
                                  processor_.graphMaximumCorrectionDb());
    if (snapshot != nullptr)
    {
        snapshot->renderSettings = processor_.graphRenderSettings();
        snapshot->matchSettings.maximumCorrectionDb = processor_.graphMaximumCorrectionDb();
    }
    curveDescription_.setContent(juce::String::fromUTF8(
        tonetrace::curveDescriptionText(snapshot != nullptr ? *snapshot : tonetrace::ProfileSnapshot{}).c_str()), false);
}

void ToneTraceEditor::toggleTrace()
{
    const bool enabled = !traceButton_.getToggleState();
    traceButton_.setToggleState(enabled, juce::dontSendNotification);
    if (enabled)
    {
        traceIndex_ = processor_.bandCount() / 2;
        const double hz = processor_.bandFrequencyHz(static_cast<std::size_t>(traceIndex_));
        updateCurveReadout(hz, true, true);
        graphSurface_.setTraceFrequency(hz);
        processor_.requestNavigationTone(hz);
    }
    else graphSurface_.setTraceFrequency(0.0);
}

void ToneTraceEditor::updateCurveReadout(double frequencyHz, bool bandReadout, bool announce)
{
    juce::String text;
    if (bandReadout)
    {
        traceIndex_ = juce::jlimit(0, processor_.bandCount() - 1, traceIndex_);
        const auto index = static_cast<std::size_t>(traceIndex_);
        const auto finalDb = processor_.bandValueDb(index);
        text = "Band " + juce::String(traceIndex_ + 1) + " of " + juce::String(processor_.bandCount())
            + ", " + juce::String(frequencyHz, 0) + " Hz, Final " + juce::String(finalDb, 3) + " dB";
        if (processor_.profileActive())
        {
            const double automatic = processor_.bandAutomaticDb(index);
            text += " (match " + juce::String(automatic, 3) + " dB, trim " + juce::String(finalDb - automatic, 3) + " dB)";
        }
        else text += "; manual graphic EQ, no learned correction";
    }
    else if (const auto snapshot = processor_.profileSnapshotCopy(); snapshot != nullptr)
    {
        const auto nearest = [frequencyHz](const tonetrace::SpectrumCapture& capture)
        {
            if (capture.points.empty()) return juce::String("unavailable");
            const auto point = std::min_element(capture.points.begin(), capture.points.end(),
                [frequencyHz](const auto& a, const auto& b) { return std::abs(a.frequencyHz - frequencyHz) < std::abs(b.frequencyHz - frequencyHz); });
            return juce::String(point->levelDb, 3) + " dB";
        };
        const double correction = tonetrace::evaluateCorrectionAt(snapshot->uncappedModel,
            processor_.graphMaximumCorrectionDb(), processor_.graphRenderSettings(), frequencyHz).tonalDb;
        text = juce::String(frequencyHz, 0) + " Hz | Reference " + nearest(snapshot->reference)
            + " | Target " + nearest(snapshot->target) + " | Correction " + juce::String(correction, 3) + " dB";
    }
    else text = "Curve Readout: move over the graph or use Trace Curve to inspect exact values.";
    curveReadout_.setContent(text, announce);
}

void ToneTraceEditor::moveBandFocus(std::size_t band, int delta)
{
    const int index = juce::jlimit(0, processor_.bandCount() - 1, static_cast<int>(band) + delta);
    int first = 0;
    for (int page = 0; page < bandPageCount_; ++page)
    {
        first += bandPageSizes_[static_cast<std::size_t>(page)];
        if (index < first)
        {
            pageSelector_.selectPageProgrammatically(page + 2);
            resized();
            bandControls_[static_cast<std::size_t>(index)]->grabKeyboardFocus();
            return;
        }
    }
}

bool ToneTraceEditor::keyPressed(const juce::KeyPress& key, juce::Component* origin)
{
    if (origin == exactEditor_.get()) return false;
    const int code = key.getKeyCode();
    if (key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown() || key.getModifiers().isCommandDown()) return false;
    if (code == 'T' || code == 't' || code == juce::KeyPress::F2Key) { toggleTrace(); return true; }
    if (!traceButton_.getToggleState() || origin == &curveDescription_
        || dynamic_cast<juce::ComboBox*>(origin) != nullptr
        || dynamic_cast<juce::Slider*>(origin) != nullptr) return false;
    const auto index = static_cast<std::size_t>(juce::jlimit(0, processor_.bandCount() - 1, traceIndex_));
    const double current = processor_.bandValueDb(index);
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        traceIndex_ = juce::jlimit(0, processor_.bandCount() - 1, traceIndex_ + (code == juce::KeyPress::leftKey ? -1 : 1));
        processor_.requestNavigationTone(processor_.bandFrequencyHz(static_cast<std::size_t>(traceIndex_)));
    }
    else if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        processor_.setBandValueDb(index, current + (code == juce::KeyPress::upKey ? 1.0 : -1.0));
    else if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
        processor_.setBandValueDb(index, current + (code == juce::KeyPress::pageUpKey ? 6.0 : -6.0));
    else if (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey)
        processor_.setBandValueDb(index, code == juce::KeyPress::homeKey ? processor_.bandLimitDb() : -processor_.bandLimitDb());
    else if (code == '0' || code == 'N' || code == 'n') processor_.neutralizeBand(index);
    else return false;
    const double hz = processor_.bandFrequencyHz(static_cast<std::size_t>(traceIndex_));
    updateCurveReadout(hz, true, true);
    graphSurface_.setTraceFrequency(hz);
    triggerAsyncUpdate();
    return true;
}

bool ToneTraceEditor::rebuildViewPages(bool preserveFocus)
{
    const int resolution = processor_.bandCount();
    const int usableWidth = std::max(560, getWidth() - 2 * tonetrace::ui::spacing::outer
        - 2 * tonetrace::ui::spacing::panelInset);
    const int widthCapacity = std::clamp(usableWidth / 72, 1, 10);
    const auto pages = tonetrace::ui::toneTraceBandPageSizes(resolution, widthCapacity);
    if (resolution == lastResolution_ && pages == bandPageSizes_) return false;

    if (bandExactTarget_ != nullptr && exactEditor_ != nullptr)
    {
        exactEditor_->setVisible(false);
        removeChildComponent(exactEditor_.get());
        exactEditor_.reset();
        bandExactTarget_ = nullptr;
        exactFinishQueued_ = false;
    }

    std::size_t focusedBand = 0;
    const int oldResolution = lastResolution_;
    bool hadBandFocus = false;
    if (preserveFocus)
        for (const auto& band : bandControls_)
            if (band->hasKeyboardFocus(true)) { focusedBand = band->bandIndex(); hadBandFocus = true; break; }

    bandControls_.clear();
    bandFrequencyLabels_.clear();
    bandValueLabels_.clear();
    bandPageSizes_ = pages;
    bandPageCount_ = static_cast<int>(pages.size());
    lastResolution_ = resolution;

    for (int i = 0; i < resolution; ++i)
    {
        auto slider = std::make_unique<ToneTraceBandSlider>(static_cast<std::size_t>(i), processor_.bandFrequencyHz(static_cast<std::size_t>(i)));
        slider->setColour(juce::Slider::backgroundColourId, tonetrace::ui::colour::grid);
        slider->setColour(juce::Slider::trackColourId, tonetrace::ui::colour::accent);
        slider->setColour(juce::Slider::thumbColourId, tonetrace::ui::colour::accentStrong);
        slider->onUserValueChanged = [this](std::size_t band, double value) { processor_.setBandValueDb(band, value); triggerAsyncUpdate(); };
        slider->onNeutralize = [this](std::size_t band) { processor_.neutralizeBand(band); triggerAsyncUpdate(); };
        slider->onRequestExactEntry = [this](std::size_t band) { beginBandExactEntry(band); };
        slider->onFocus = [this](std::size_t band) { traceIndex_ = static_cast<int>(band); processor_.requestNavigationTone(processor_.bandFrequencyHz(band)); };
        slider->onNavigate = [this](std::size_t band, int delta) { moveBandFocus(band, delta); };
        slider->onToggleTrace = [this] { toggleTrace(); };
        slider->addKeyListener(this);
        slider->setExplicitFocusOrder(200 + i);
        addAndMakeVisible(*slider);
        bandControls_.push_back(std::move(slider));

        auto frequencyLabel = std::make_unique<juce::Label>();
        configureDecorativeLabel(*frequencyLabel, false);
        frequencyLabel->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*frequencyLabel);
        bandFrequencyLabels_.push_back(std::move(frequencyLabel));

        auto valueLabel = std::make_unique<juce::Label>();
        configureDecorativeLabel(*valueLabel, true);
        valueLabel->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*valueLabel);
        bandValueLabels_.push_back(std::move(valueLabel));
    }

    const int oldSelected = juce::jmax(0, pageSelector_.selectedPage());
    int selectedPage = juce::jmin(oldSelected, 1 + bandPageCount_);
    std::size_t remappedBand = focusedBand;
    if (hadBandFocus && !bandControls_.empty())
    {
        if (oldResolution > 1 && resolution > 1)
            remappedBand = static_cast<std::size_t>(std::lround(
                static_cast<double>(focusedBand) * static_cast<double>(resolution - 1)
                / static_cast<double>(oldResolution - 1)));
        remappedBand = std::min(remappedBand, bandControls_.size() - 1);

        int firstBand = 0;
        for (int page = 0; page < bandPageCount_; ++page)
        {
            const int count = bandPageSizes_[static_cast<std::size_t>(page)];
            if (static_cast<int>(remappedBand) >= firstBand
                && static_cast<int>(remappedBand) < firstBand + count)
            {
                selectedPage = 2 + page;
                break;
            }
            firstBand += count;
        }
    }

    std::vector<juce::String> pageNames { "Capture and transfer", "Correction controls" };
    int first = 1;
    for (int page = 0; page < bandPageCount_; ++page)
    {
        const int count = bandPageSizes_[static_cast<std::size_t>(page)];
        const int last = first + count - 1;
        pageNames.push_back("Bands " + juce::String(first) + "–" + juce::String(last));
        first = last + 1;
    }
    pageSelector_.setPages(pageNames, selectedPage);
    for (auto* child : getChildren()) child->addKeyListener(this);
    updateBandControls();
    if (hadBandFocus && !bandControls_.empty())
        bandControls_[remappedBand]->grabKeyboardFocus();
    return true;
}

void ToneTraceEditor::updateBandControls()
{
    const auto bands = processor_.bandDisplayState();
    const auto count = std::min({ bandControls_.size(), bands.frequenciesHz.size(), bands.finalDb.size() });
    for (std::size_t i = 0; i < count; ++i)
    {
        auto& slider = *bandControls_[i];
        const double frequencyHz = bands.frequenciesHz[i];
        slider.setBandIdentity(i, frequencyHz, bands.limitDb);
        if (&slider != bandExactTarget_) slider.setFromProcessor(bands.finalDb[i]);
        auto frequencyText = frequencyHz >= 1000.0
            ? juce::String(frequencyHz / 1000.0, frequencyHz < 10000.0 ? 2 : 1) + " kHz"
            : juce::String(frequencyHz, frequencyHz < 100.0 ? 1 : 0) + " Hz";
        const auto concise = [](double hz) { return hz >= 1000.0
            ? juce::String(hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz"
            : juce::String(hz, hz < 100.0 ? 1 : 0) + " Hz"; };
        if ((i > 0 && concise(bands.frequenciesHz[i - 1]) == frequencyText)
            || (i + 1 < count && concise(bands.frequenciesHz[i + 1]) == frequencyText))
        {
            for (int precision = 0; precision <= 6; ++precision)
            {
                frequencyText = juce::String(frequencyHz, precision) + " Hz";
                if ((i == 0 || juce::String(bands.frequenciesHz[i - 1], precision) + " Hz" != frequencyText)
                    && (i + 1 == count || juce::String(bands.frequenciesHz[i + 1], precision) + " Hz" != frequencyText)) break;
            }
        }
        slider.setFrequencyText(frequencyText);
        bandFrequencyLabels_[i]->setText(frequencyText, juce::dontSendNotification);
        bandValueLabels_[i]->setText(slider.formatValue(bands.finalDb[i]), juce::dontSendNotification);
    }
}

void ToneTraceEditor::handlePageChanged(int pageIndex, bool userInitiated)
{
    updatePageVisibility();
    if (pageIndex >= 2) updateBandControls();
    if (userInitiated && pageIndex >= 2) requestBandPageSweep(pageIndex);
    if (userInitiated) resized();
}

void ToneTraceEditor::requestBandPageSweep(int pageIndex)
{
    const int selected = pageIndex - 2;
    if (selected < 0 || selected >= bandPageCount_) return;

    int firstBand = 0;
    for (int page = 0; page < selected; ++page)
        firstBand += bandPageSizes_[static_cast<std::size_t>(page)];
    const int count = bandPageSizes_[static_cast<std::size_t>(selected)];
    if (count <= 0) return;

    const int lastBand = firstBand + count - 1;
    const int totalBands = processor_.bandCount();
    if (firstBand < 0 || lastBand >= totalBands) return;

    const auto [rangeLow, rangeHigh] = processor_.bandFrequencyRangeHz();
    const auto [lowEdge, highEdge] = tonetrace::ui::toneTraceBandPageSweepEdges(
        totalBands, firstBand, count, rangeLow, rangeHigh);
    if (lowEdge <= 0.0 || highEdge <= lowEdge) return;
    processor_.requestNavigationSweep(lowEdge, highEdge, count, 1200.0);
}

void ToneTraceEditor::updatePageVisibility()
{
    const int pageIndex = juce::jmax(0, pageSelector_.selectedPage());
    const bool capturePage = pageIndex == 0;
    const bool correctionPage = pageIndex == 1;
    const bool bandPage = pageIndex >= 2;

    graphSurface_.setVisible(true);
    curveDescription_.setVisible(capturePage);
    curveDescriptionLabel_.setVisible(capturePage);
    copyDescriptionButton_.setVisible(capturePage);
    controlsHeading_.setVisible(capturePage);
    for (auto* button : { &captureReferenceButton_, &learnTargetButton_, &correctTargetButton_, &freezeCorrectionButton_ }) button->setVisible(capturePage);
    matchModeLabel_.setVisible(capturePage); matchModeControl_->setVisible(capturePage);
    optionsButton_.setVisible(capturePage);
    transferHeading_.setVisible(capturePage);
    exportButton_.setVisible(capturePage);
    importButton_.setVisible(capturePage);
    statusLabel_.setVisible(capturePage); statusField_.setVisible(capturePage);

    for (std::size_t i = 0; i < continuousControls_.size(); ++i)
    {
        const bool resolutionOnBandPage = bandPage
            && i == tonetrace::product::kCorrectionResolutionIndex;
        continuousLabels_[i]->setVisible(correctionPage || resolutionOnBandPage);
        continuousValueLabels_[i]->setVisible(correctionPage || resolutionOnBandPage);
        continuousControls_[i]->setVisible((correctionPage || resolutionOnBandPage)
                                           && continuousControls_[i].get() != exactTarget_);
    }
    for (auto& control : toggleControls_) control->setVisible(correctionPage);

    bandHeading_.setVisible(bandPage);
    int pageStart = 0;
    int pageCount = 0;
    if (bandPage)
    {
        const int selected = pageIndex - 2;
        for (int p = 0; p < selected && p < bandPageCount_; ++p) pageStart += bandPageSizes_[static_cast<std::size_t>(p)];
        if (selected >= 0 && selected < bandPageCount_) pageCount = bandPageSizes_[static_cast<std::size_t>(selected)];
    }
    for (std::size_t i = 0; i < bandControls_.size(); ++i)
    {
        const bool visible = bandPage && static_cast<int>(i) >= pageStart && static_cast<int>(i) < pageStart + pageCount;
        bandControls_[i]->setVisible(visible && bandControls_[i].get() != bandExactTarget_);
        bandFrequencyLabels_[i]->setVisible(visible);
        bandValueLabels_[i]->setVisible(visible);
    }
    if (exactEditor_ != nullptr) exactEditor_->setVisible(true);
}

void ToneTraceEditor::announceTransferResult(const juce::String& text, bool error)
{
    if (text.isEmpty()) return;
    // The released editor put transfer results in its visible readout as well
    // as announcing them to the accessibility API.  Keeping the message only
    // in an announcement made successful menu actions appear to do nothing
    // when a host had no active screen reader.
    curveReadout_.setText(text, juce::dontSendNotification);
    juce::AccessibilityHandler::postAnnouncement(text,
        error ? juce::AccessibilityHandler::AnnouncementPriority::high
              : juce::AccessibilityHandler::AnnouncementPriority::medium);
}


void ToneTraceEditor::showOptionsDialog()
{
    ToneTraceOptionsValues initial;
    initial.fullCorrectionRange = processor_.toggleValue(tonetrace::product::kFullCorrectionRangeIndex);
    initial.toneNotifications = processor_.toggleValue(tonetrace::product::kToneNotificationsIndex);
    initial.confidenceToneVolumeDb = processor_.continuousValue(tonetrace::product::kConfidenceToneVolumeIndex);
    initial.bypass = processor_.toggleValue(tonetrace::product::kBypassIndex);

    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    auto content = std::make_unique<ToneTraceOptionsComponent>(
        initial,
        [safe](const ToneTraceOptionsValues& values)
        {
            if (safe == nullptr) return;
            auto& processor = safe->processor_;

            const auto setToggle = [&processor](std::size_t index, bool value)
            {
                if (processor.toggleValue(index) == value) return;
                processor.beginToggleGesture(index);
                processor.setToggleFromUi(index, value);
                processor.endToggleGesture(index);
            };
            setToggle(tonetrace::product::kFullCorrectionRangeIndex, values.fullCorrectionRange);
            setToggle(tonetrace::product::kToneNotificationsIndex, values.toneNotifications);
            setToggle(tonetrace::product::kBypassIndex, values.bypass);

            const auto toneIndex = tonetrace::product::kConfidenceToneVolumeIndex;
            if (std::abs(processor.continuousValue(toneIndex) - values.confidenceToneVolumeDb) > 0.0005)
            {
                processor.beginContinuousGesture(toneIndex);
                processor.setContinuousFromUi(toneIndex, values.confidenceToneVolumeDb);
                processor.endContinuousGesture(toneIndex);
            }
        },
        [safe]
        {
            if (safe == nullptr) return;
            safe->processor_.setWorkflowFromUi(5);
            safe->processor_.setWorkflowFromUi(6);
        });

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Tone Trace EQ Options";
    options.dialogBackgroundColour = tonetrace::ui::colour::background;
    auto* const optionsContent = content.get();
    options.content.setOwned(content.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.useBottomRightCornerResizer = false;
    if (options.launchAsync() != nullptr)
        optionsContent->focusInitialControl();
}

void ToneTraceEditor::showExportMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Export Impulse Response (WAV)...");
    menu.addItem(2, "Export Reference Curve (TTS)...");
    menu.addItem(3, "Export Target Curve (TTS)...");
    menu.addSeparator();
    menu.addItem(4, "Export Correction Model (TTM)...");

    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent(&exportButton_)
        .withParentComponent(this)
        .withInitiallySelectedItem(1)
        .withMinimumWidth(exportButton_.getWidth())
        .withDeletionCheck(*this);
    menu.showMenuAsync(options,
        [safe](int result)
        {
            if (safe == nullptr) return;
            switch (result)
            {
                case 1: safe->chooseExport(0); break;
                case 2: safe->chooseExport(1); break;
                case 3: safe->chooseExport(2); break;
                case 4: safe->chooseExport(3); break;
                default: break;
            }
        });
}

void ToneTraceEditor::showImportMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Import Reference Curve (TTS)...");
    menu.addItem(2, "Import Target Curve (TTS)...");
    menu.addSeparator();
    menu.addItem(3, "Import Correction Model (TTM)...");

    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    const auto options = juce::PopupMenu::Options()
        .withTargetComponent(&importButton_)
        .withParentComponent(this)
        .withInitiallySelectedItem(1)
        .withMinimumWidth(importButton_.getWidth())
        .withDeletionCheck(*this);
    menu.showMenuAsync(options,
        [safe](int result)
        {
            if (safe == nullptr) return;
            switch (result)
            {
                case 1: safe->chooseImport(0); break;
                case 2: safe->chooseImport(1); break;
                case 3: safe->chooseImport(2); break;
                default: break;
            }
        });
}

void ToneTraceEditor::chooseExport(int kind)
{
    if (kind == 0 && !processor_.profileActive())
    {
        // Released Tone Trace allows a manual-only IR, but requires an explicit
        // accessible OK/Cancel decision. A flat unmatched instance remains an
        // error and never opens a save chooser.
        if (!processor_.exportableImpulseResponse().has_value())
        {
            announceTransferResult("No matching curve is available to export yet.", true);
            return;
        }

        const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
        juce::NativeMessageBox::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon,
            "No learned match is available. Export the impulse response of the manually created curve?",
            "No learned match is available. Tone Trace will export an impulse response of the manually created curve that is currently active.\n\nContinue?",
            this,
            juce::ModalCallbackFunction::create([safe](int result)
            {
                if (safe != nullptr && result == 1) safe->chooseExportAfterConfirmation(0);
            }));
        return;
    }

    chooseExportAfterConfirmation(kind);
}

void ToneTraceEditor::chooseExportAfterConfirmation(int kind)
{
    if (fileChooser_ != nullptr)
    {
        announceTransferResult("A Tone Trace file chooser is already open.", true);
        return;
    }

    juce::String extension = kind == 0 ? "*.wav" : (kind == 3 ? "*.ttm" : "*.tts");
    juce::String defaultName = kind == 0 ? "ToneTrace-IR.wav" : kind == 1 ? "ToneTrace-Reference.tts" : kind == 2 ? "ToneTrace-Target.tts" : "ToneTrace-Model.ttm";
    fileChooser_ = std::make_unique<juce::FileChooser>("Export from Tone Trace", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultName), extension);
    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, kind](const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            if (file == juce::File{}) { safe->fileChooser_.reset(); return; }
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension(kind == 0 ? ".wav" : (kind == 3 ? ".ttm" : ".tts"));
            try
            {
                if (kind == 0)
                {
                    const auto audio = safe->processor_.exportableImpulseResponse();
                    if (!audio.has_value()) throw std::runtime_error("No correction curve is available to export yet.");
                    tonetrace::writeFloatWav(toneTraceFilesystemPath(file), *audio);
                }
                else if (kind == 1)
                {
                    const auto capture = safe->processor_.exportableReference();
                    if (!capture.has_value()) throw std::runtime_error("No Reference curve is available to export yet.");
                    tonetrace::saveSpectrumCapture(toneTraceFilesystemPath(file), *capture);
                }
                else if (kind == 2)
                {
                    const auto capture = safe->processor_.exportableTarget();
                    if (!capture.has_value()) throw std::runtime_error("No Target curve is available to export yet.");
                    tonetrace::saveSpectrumCapture(toneTraceFilesystemPath(file), *capture);
                }
                else
                {
                    const auto snapshot = safe->processor_.profileSnapshotCopy();
                    if (snapshot == nullptr) throw std::runtime_error("No learned correction model is available to export yet.");
                    snapshot->uncappedModel.save(toneTraceFilesystemPath(file));
                }
                static constexpr const char* completed[] {
                    "Impulse response exported.",
                    "Reference curve exported.",
                    "Target curve exported.",
                    "Correction model exported."
                };
                safe->announceTransferResult(completed[juce::jlimit(0, 3, kind)], false);
            }
            catch (const std::exception& e) { safe->announceTransferResult("Export failed: " + juce::String::fromUTF8(e.what()), true); }
            safe->fileChooser_.reset();
        });
}

void ToneTraceEditor::chooseImport(int kind)
{
    if (fileChooser_ != nullptr)
    {
        announceTransferResult("A Tone Trace file chooser is already open.", true);
        return;
    }

    const juce::String extension = kind == 2 ? "*.ttm" : "*.tts";
    fileChooser_ = std::make_unique<juce::FileChooser>("Import into Tone Trace", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), extension);
    const auto safe = juce::Component::SafePointer<ToneTraceEditor>(this);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, kind](const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) { safe->fileChooser_.reset(); return; }
            try
            {
                ToneTraceTransferResult result;
                if (kind == 0) result = safe->processor_.importReference(tonetrace::loadSpectrumCapture(toneTraceFilesystemPath(file)));
                else if (kind == 1) result = safe->processor_.importTarget(tonetrace::loadSpectrumCapture(toneTraceFilesystemPath(file)));
                else result = safe->processor_.importModel(tonetrace::CorrectionModel::load(toneTraceFilesystemPath(file)));
                if (!result.ok) safe->announceTransferResult("Import failed: " + result.message, true);
                else
                {
                    static constexpr const char* completed[] {
                        "Reference curve imported. Capture or import the target.",
                        "Target curve imported.",
                        "Correction model imported."
                    };
                    auto message = juce::String(completed[juce::jlimit(0, 2, kind)]);
                    if (!result.message.isEmpty()) message << " " << result.message;
                    safe->announceTransferResult(message, false);
                }
                safe->triggerAsyncUpdate();
            }
            catch (const std::exception& e) { safe->announceTransferResult("Import failed: " + juce::String::fromUTF8(e.what()), true); }
            safe->fileChooser_.reset();
        });
}

#if defined(TONETRACE_VISUAL_SNAPSHOT_BUILD)
void ToneTraceEditor::setSnapshotPage(int pageId)
{
    const int count = getSnapshotPageCount();
    pageSelector_.selectPageProgrammatically(juce::jlimit(1, juce::jmax(1, count), pageId) - 1);
    resized();
}

int ToneTraceEditor::getSnapshotPageCount() const noexcept
{
    return juce::jmax(1, pageSelector_.pageCount());
}

std::unique_ptr<juce::Component> ToneTraceEditor::createOptionsSnapshotComponent()
{
    ToneTraceOptionsValues initial;
    initial.fullCorrectionRange = processor_.toggleValue(tonetrace::product::kFullCorrectionRangeIndex);
    initial.toneNotifications = processor_.toggleValue(tonetrace::product::kToneNotificationsIndex);
    initial.confidenceToneVolumeDb = processor_.continuousValue(tonetrace::product::kConfidenceToneVolumeIndex);
    initial.bypass = processor_.toggleValue(tonetrace::product::kBypassIndex);
    return std::make_unique<ToneTraceOptionsComponent>(initial, [](const ToneTraceOptionsValues&) {}, [] {});
}
#endif

bool ToneTraceEditor::keyPressed(const juce::KeyPress& key)
{
    if (keyPressed(key, this)) return true;
    const auto modifiers = key.getModifiers();
    if (key.getKeyCode() == juce::KeyPress::tabKey && modifiers.isCtrlDown()
        && !modifiers.isAltDown())
        return pageSelector_.selectRelativeFromUser(modifiers.isShiftDown() ? -1 : 1);

    return juce::AudioProcessorEditor::keyPressed(key);
}

void ToneTraceEditor::paint(juce::Graphics& g)
{
    using namespace tonetrace::ui;
    g.fillAll(colour::background);

    g.setColour(colour::panel);
    g.fillRoundedRectangle(controlsPanelBounds_.toFloat(), spacing::cornerRadius);
    g.setColour(colour::border);
    g.drawRoundedRectangle(controlsPanelBounds_.toFloat().reduced(0.5f), spacing::cornerRadius, 1.0f);
}

void ToneTraceEditor::resized()
{
    using namespace tonetrace::ui;

    // Width is part of the released band-page capacity rule. Recompute here so
    // minimum/default/wide layouts cannot retain pagination calculated at a
    // previous size. The helper returns immediately when nothing changed.
    (void) rebuildViewPages(true);

    auto area = getLocalBounds().reduced(spacing::outer);
    heading_.setBounds(area.removeFromTop(35));
    subtitle_.setBounds(area.removeFromTop(25));
    area.removeFromTop(6);
    auto selectorRow = area.removeFromTop(34);
    pageSelector_.setBounds(selectorRow);
    area.removeFromTop(6);
    auto readoutRow = area.removeFromTop(44);
    traceButton_.setBounds(readoutRow.removeFromRight(130));
    curveReadout_.setBounds(readoutRow);
    area.removeFromTop(spacing::sectionGap);

    constexpr int controlsHeight = 500;
    controlsPanelBounds_ = area.removeFromBottom(std::min(controlsHeight, std::max(360, area.getHeight() * 2 / 3)));
    area.removeFromBottom(spacing::sectionGap);
    graphPanelBounds_ = area;
    graphSurface_.setBounds(graphPanelBounds_);

    auto controls = controlsPanelBounds_.reduced(spacing::panelInset);
    const int pageIndex = juce::jmax(0, pageSelector_.selectedPage());

    if (pageIndex == 0)
    {
        controlsHeading_.setBounds(controls.removeFromTop(24));
        controls.removeFromTop(spacing::rowGap);
        auto row = controls.removeFromTop(spacing::rowHeight);
        auto leftButton = row.removeFromLeft((row.getWidth() - spacing::rowGap) / 2);
        row.removeFromLeft(spacing::rowGap);
        captureReferenceButton_.setBounds(leftButton);
        learnTargetButton_.setBounds(row);
        controls.removeFromTop(spacing::rowGap);

        row = controls.removeFromTop(spacing::rowHeight);
        leftButton = row.removeFromLeft((row.getWidth() - spacing::rowGap) / 2);
        row.removeFromLeft(spacing::rowGap);
        correctTargetButton_.setBounds(leftButton);
        freezeCorrectionButton_.setBounds(row);
        controls.removeFromTop(spacing::rowGap);

        row = controls.removeFromTop(spacing::rowHeight);
        matchModeLabel_.setBounds(row.removeFromLeft(spacing::labelWidth));
        row.removeFromLeft(spacing::rowGap);
        optionsButton_.setBounds(row.removeFromRight(120));
        row.removeFromRight(spacing::rowGap);
        matchModeControl_->setBounds(row);
        controls.removeFromTop(spacing::rowGap);

        transferHeading_.setBounds(controls.removeFromTop(22));
        controls.removeFromTop(4);
        row = controls.removeFromTop(spacing::rowHeight);
        auto exportArea = row.removeFromLeft((row.getWidth() - spacing::rowGap) / 2);
        row.removeFromLeft(spacing::rowGap);
        exportButton_.setBounds(exportArea);
        importButton_.setBounds(row);
        controls.removeFromTop(spacing::rowGap);
        auto labels = controls.removeFromTop(22);
        statusLabel_.setBounds(labels.removeFromLeft((labels.getWidth() - spacing::rowGap) / 2));
        labels.removeFromLeft(spacing::rowGap);
        curveDescriptionLabel_.setBounds(labels);
        controls.removeFromTop(4);
        statusField_.setBounds(controls.removeFromLeft((controls.getWidth() - spacing::rowGap) / 2));
        controls.removeFromLeft(spacing::rowGap);
        copyDescriptionButton_.setBounds(controls.removeFromBottom(28));
        controls.removeFromBottom(4);
        curveDescription_.setBounds(controls);
    }
    else if (pageIndex == 1)
    {
        auto available = controls;
        const int rowHeight = std::max(31, std::min(spacing::rowHeight,
            (available.getHeight() - static_cast<int>(continuousControls_.size()) * spacing::rowGap - 50)
                / static_cast<int>(continuousControls_.size() + 1)));
        for (std::size_t index = 0; index < continuousControls_.size(); ++index)
        {
            auto row = available.removeFromTop(rowHeight);
            continuousLabels_[index]->setBounds(row.removeFromLeft(spacing::labelWidth));
            row.removeFromLeft(spacing::rowGap);
            continuousValueLabels_[index]->setBounds(row.removeFromRight(spacing::valueWidth));
            row.removeFromRight(spacing::rowGap);
            continuousControls_[index]->setBounds(row);
            if (exactTarget_ == continuousControls_[index].get() && exactEditor_ != nullptr)
                exactEditor_->setBounds(row.reduced(1, 3));
            available.removeFromTop(spacing::rowGap);
        }
        if (!toggleControls_.empty())
        {
            auto row = available.removeFromTop(std::max(32, available.getHeight()));
            const int width = std::max(1, (row.getWidth() - static_cast<int>(toggleControls_.size() - 1) * spacing::rowGap)
                / static_cast<int>(toggleControls_.size()));
            for (std::size_t i = 0; i < toggleControls_.size(); ++i)
            {
                toggleControls_[i]->setBounds(row.removeFromLeft(width));
                if (i + 1 < toggleControls_.size()) row.removeFromLeft(spacing::rowGap);
            }
        }
    }
    else
    {
        bandHeading_.setBounds(controls.removeFromTop(24));
        controls.removeFromTop(spacing::rowGap);
        {
            const auto i = tonetrace::product::kCorrectionResolutionIndex;
            auto row = controls.removeFromTop(spacing::rowHeight);
            auto labelArea = row.removeFromLeft(170);
            continuousLabels_[i]->setBounds(labelArea);
            auto valueArea = row.removeFromRight(100);
            continuousValueLabels_[i]->setBounds(valueArea);
            continuousControls_[i]->setBounds(row.reduced(8, 0));
            if (exactTarget_ == continuousControls_[i].get() && exactEditor_ != nullptr)
                exactEditor_->setBounds(row.reduced(9, 3));
        }
        controls.removeFromTop(spacing::sectionGap);
        const int selected = pageIndex - 2;
        int pageStart = 0;
        for (int p = 0; p < selected && p < bandPageCount_; ++p) pageStart += bandPageSizes_[static_cast<std::size_t>(p)];
        const int pageCount = (selected >= 0 && selected < bandPageCount_)
            ? bandPageSizes_[static_cast<std::size_t>(selected)] : 0;
        if (pageCount > 0)
        {
            const int gap = 5;
            const int columnWidth = std::max(48, (controls.getWidth() - (pageCount - 1) * gap) / pageCount);
            for (int offset = 0; offset < pageCount; ++offset)
            {
                const std::size_t index = static_cast<std::size_t>(pageStart + offset);
                auto column = controls.removeFromLeft(columnWidth);
                auto frequencyArea = column.removeFromTop(24);
                auto valueArea = column.removeFromBottom(24);
                bandFrequencyLabels_[index]->setBounds(frequencyArea);
                bandValueLabels_[index]->setBounds(valueArea);
                bandControls_[index]->setBounds(column.reduced(5, 0));
                if (bandExactTarget_ == bandControls_[index].get() && exactEditor_ != nullptr)
                    exactEditor_->setBounds(bandControls_[index]->getBounds().reduced(0, bandControls_[index]->getHeight() / 2 - 18));
                if (offset + 1 < pageCount) controls.removeFromLeft(gap);
            }
        }
    }

    exactEntryError_.setBounds(controlsPanelBounds_.reduced(spacing::panelInset).removeFromBottom(24));
}
