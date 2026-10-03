#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ToneTraceProductCatalog.h"
#include "capture/ToneTraceCaptureModel.h"
#include "correction/ToneTraceCorrectionController.h"
#include "tone/ToneTraceNotificationTone.h"
#include "tone/ToneTraceOutputBypass.h"
#include "platform/ToneTraceStoppedTransportTone.h"

struct ToneTraceTransferResult
{
    bool ok = false;
    juce::String message;
};

struct ToneTraceBandDisplayState
{
    double limitDb = 60.0;
    std::vector<double> frequenciesHz;
    std::vector<double> automaticDb;
    std::vector<double> finalDb;
};

class ToneTraceProcessor final : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    ToneTraceProcessor();
    ~ToneTraceProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void setNonRealtime(bool isNonRealtime) noexcept override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override
    {
       #if defined(TONETRACE_VISUAL_SNAPSHOT_BUILD)
        return "Tone Trace EQ";
       #else
        return JucePlugin_Name;
       #endif
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return tailSeconds_.load(std::memory_order_acquire); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    bool restoreState(const void*, int);

    std::size_t continuousParameterCount() const noexcept { return continuousParameters_.size(); }
    juce::AudioParameterFloat& continuousParameter(std::size_t index) noexcept;
    const juce::AudioParameterFloat& continuousParameter(std::size_t index) const noexcept;
    double continuousValue(std::size_t index) const noexcept;
    void beginContinuousGesture(std::size_t index);
    void setContinuousFromUi(std::size_t index, double value);
    void endContinuousGesture(std::size_t index);

    juce::AudioParameterChoice& matchModeParameter() noexcept { return *matchMode_; }
    std::size_t toggleParameterCount() const noexcept { return toggleParameters_.size(); }
    juce::AudioParameterBool& toggleParameter(std::size_t index) noexcept { return *toggleParameters_.at(index); }
    const juce::AudioParameterBool& toggleParameter(std::size_t index) const noexcept { return *toggleParameters_.at(index); }
    bool toggleValue(std::size_t index) const noexcept { return toggleParameters_.at(index)->get(); }
    void beginToggleGesture(std::size_t index) { toggleParameters_.at(index)->beginChangeGesture(); }
    void setToggleFromUi(std::size_t index, bool value) { toggleParameters_.at(index)->setValueNotifyingHost(value ? 1.0F : 0.0F); }
    void endToggleGesture(std::size_t index) { toggleParameters_.at(index)->endChangeGesture(); }
    const juce::AudioParameterChoice& matchModeParameter() const noexcept { return *matchMode_; }
    int matchModeIndex() const noexcept;
    void beginMatchModeGesture();
    void setMatchModeFromUi(int index);
    void endMatchModeGesture();

    juce::AudioParameterFloat& workflowParameter() noexcept { return *workflowStep_; }
    juce::AudioParameterFloat& statusParameter() noexcept { return *statusParameter_; }
    juce::AudioParameterFloat& lastCommandParameter() noexcept { return *lastCommandParameter_; }
    juce::AudioParameterFloat& confidenceParameter() noexcept { return *confidenceParameter_; }
    juce::AudioParameterFloat& curveDriftParameter() noexcept { return *curveDriftParameter_; }
    juce::AudioParameterFloat& captureTimeParameter() noexcept { return *captureTimeParameter_; }

    void setWorkflowFromUi(int step);
    [[nodiscard]] int workflowStep() const noexcept;
    [[nodiscard]] int statusCode() const noexcept;
    [[nodiscard]] int lastCommandCode() const noexcept;
    [[nodiscard]] double captureSeconds() const noexcept;
    [[nodiscard]] double captureConfidence() const noexcept;
    [[nodiscard]] double curveDriftDb() const noexcept;
    [[nodiscard]] bool captureActive() const noexcept;
    [[nodiscard]] bool profileActive() const noexcept { return hasProfile_.load(std::memory_order_acquire); }
    [[nodiscard]] juce::String statusSummary() const;

    // Product-state access used by the editor. These are snapshots/copies; the
    // editor never owns DSP state.
    [[nodiscard]] std::unique_ptr<tonetrace::ProfileSnapshot> profileSnapshotCopy() const;
    [[nodiscard]] tonetrace::IrRenderSettings graphRenderSettings() const;
    [[nodiscard]] double graphMaximumCorrectionDb() const noexcept;
    [[nodiscard]] std::optional<tonetrace::SpectrumCapture> exportableReference() const;
    [[nodiscard]] std::optional<tonetrace::SpectrumCapture> exportableTarget() const;
    [[nodiscard]] std::optional<tonetrace::AudioBuffer> exportableImpulseResponse() const;
    [[nodiscard]] std::vector<double> manualGainsCopy() const;
    [[nodiscard]] int bandCount() const noexcept;
    [[nodiscard]] std::pair<double, double> bandFrequencyRangeHz() const;
    [[nodiscard]] double bandFrequencyHz(std::size_t index) const;
    [[nodiscard]] double bandAutomaticDb(std::size_t index) const;
    [[nodiscard]] double bandValueDb(std::size_t index) const;
    [[nodiscard]] double bandLimitDb() const;
    [[nodiscard]] ToneTraceBandDisplayState bandDisplayState() const;
    void setBandValueDb(std::size_t index, double finalDb);
    void neutralizeBand(std::size_t index);
    void requestNavigationTone(double frequencyHz) noexcept;
    void requestNavigationSweep(double fromHz, double toHz, int bandCount, double durationMs) noexcept;

    [[nodiscard]] ToneTraceTransferResult importReference(const tonetrace::SpectrumCapture& capture);
    [[nodiscard]] ToneTraceTransferResult importTarget(const tonetrace::SpectrumCapture& capture);
    [[nodiscard]] ToneTraceTransferResult importModel(const tonetrace::CorrectionModel& model);

private:
    struct StateParameterRecord
    {
        juce::Identifier key;
        juce::AudioProcessorParameter* parameter = nullptr;
    };

    enum class AnalysisJobKind : std::uint8_t
    {
        AnalyzeCaptured,
        RebuildProfile,
    };

    struct AnalysisJob
    {
        AnalysisJobKind kind = AnalysisJobKind::AnalyzeCaptured;
        std::uint64_t epoch = 0;
        tonetrace::jucebridge::CorrectionSettings settings;
    };

    static constexpr double kGainSmoothingSeconds = 0.020;
    static constexpr int kStateSchemaVersion = 4;
    static constexpr int kControlTimerHz = 20;

    void timerCallback() override;
    void consumeMatchModeChange();
    void consumeWorkflowParameterChange();
    void consumeDeferredWorkflowCommand();
    void consumeCorrectionSettingChanges();
    void handleWorkflowCommand(int step);
    void performConfirmedReset();
    void executePendingAudioCommand() noexcept;
    void captureInputSlice(juce::AudioBuffer<float>& buffer) noexcept;
    [[nodiscard]] bool statusTonesAllowed() const noexcept;
    [[nodiscard]] bool navigationTonesAllowed() const noexcept;
    void cancelActiveTones() noexcept;
    void requestWarningTone() noexcept;
    void startConfidenceTone(int oldLevel, int newLevel) noexcept;
    void renderGeneratedTone(float* const* output, std::size_t channels,
                                std::size_t count) noexcept;
    void applyGlobalBypass(float* const* wet, const float* const* dry,
                           std::size_t channels, std::size_t count) noexcept;
    void resetPublishedCaptureStateForCommand(tonetrace::capture::WorkflowPhase phase,
                                               tonetrace::capture::Status status) noexcept;
    void resetWorkflowToReadyForStateLoad();
    void applyRestoredFrozenWorkflow(double confidence);
    void setStatus(tonetrace::capture::Status status) noexcept;
    void publishTelemetryParameters();
    void publishParameter(juce::AudioParameterFloat& parameter, double actualValue);
    void setWorkflowStepFromEngine(int step);
    void ensureCaptureCapacityForMode(tonetrace::capture::MatchMode mode);
    [[nodiscard]] bool captureBuffersReadyForMode(tonetrace::capture::MatchMode mode) const noexcept;
    [[nodiscard]] tonetrace::capture::MatchMode currentMatchMode() const noexcept;

    [[nodiscard]] tonetrace::jucebridge::CorrectionSettings currentCorrectionSettings() const noexcept;
    [[nodiscard]] std::pair<double, double> currentTransferRangeHz(
        double fallbackLowHz, double fallbackHighHz) const noexcept;
    [[nodiscard]] std::array<double, 7> currentObservedCorrectionSettings() const noexcept;
    void synchronizeObservedControlState() noexcept;
    [[nodiscard]] static tonetrace::MatchMode releasedMatchMode(tonetrace::capture::MatchMode mode) noexcept;
    [[nodiscard]] static tonetrace::CaptureDiagnostics releasedDiagnostics(
        const tonetrace::capture::CaptureDiagnostics& diagnostics) noexcept;
    [[nodiscard]] static tonetrace::AudioBuffer copyCaptureAudio(
        const tonetrace::capture::CaptureBuffer& capture, int sampleRate);

    void queueAnalysisJob(AnalysisJobKind kind);
    void analysisThreadMain();
    void runAnalyzeJob(const AnalysisJob& job);
    void runRebuildJob(const AnalysisJob& job);
    void finishAnalyzeJob(const AnalysisJob& job,
                          const tonetrace::jucebridge::CorrectionResult& result);
    void finishRebuildJob(const AnalysisJob& job,
                          const tonetrace::jucebridge::CorrectionResult& result);
    void stopAnalysisThread();
    [[nodiscard]] bool jobStillCurrent(const AnalysisJob& job) const noexcept;
    void invalidateAnalysisWork() noexcept;

    std::array<juce::AudioParameterFloat*, tonetrace::product::kImplementedContinuousControls.size()>
        continuousParameters_ {};
    juce::AudioParameterChoice* matchMode_ = nullptr; // owned by AudioProcessor
    std::array<juce::AudioParameterBool*, tonetrace::product::kImplementedToggleControls.size()>
        toggleParameters_ {};
    juce::AudioParameterFloat* workflowStep_ = nullptr; // owned by AudioProcessor
    juce::AudioParameterFloat* statusParameter_ = nullptr; // host mirror only
    juce::AudioParameterFloat* lastCommandParameter_ = nullptr; // host mirror only
    juce::AudioParameterFloat* confidenceParameter_ = nullptr; // host mirror only
    juce::AudioParameterFloat* curveDriftParameter_ = nullptr; // host mirror only
    juce::AudioParameterFloat* captureTimeParameter_ = nullptr; // host mirror only
    std::vector<StateParameterRecord> stateParameters_;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmoother_;
    tonetrace::tone::SweepTone generatedTone_;
    bool activeToneIsNavigation_ = false; // audio-thread owned
    std::atomic<bool> pendingGeneratedToneStop_ { false };
    std::atomic<bool> pendingCoreAudioReset_ { false };
    std::atomic<bool> pendingWarningTone_ { false }; // released 420 -> 180 Hz warning
    std::atomic<bool> pendingNavigationTone_ { false };
    std::atomic<double> pendingNavigationFromHz_ { 0.0 };
    std::atomic<double> pendingNavigationToHz_ { 0.0 };
    std::atomic<double> pendingNavigationDurationMs_ { 0.0 };
    std::atomic<bool> hostProcessingActive_ { false };
    std::atomic<bool> offlineRendering_ { false };
    tonetrace::platform::StoppedTransportTone stoppedTransportTone_;
    juce::AudioBuffer<float> dryBuffer_;
    tonetrace::tone::OutputBypassMixer outputBypass_;

    // Capture buffers are allocated/resized only on the control thread. During
    // active capture the audio thread owns their mutable contents exclusively.
    tonetrace::capture::CaptureBuffer referenceCapture_;
    tonetrace::capture::CaptureBuffer targetCapture_;
    mutable std::mutex captureControlMutex_; // never acquired by processBlock
    std::atomic<unsigned int> captureWriterCount_ { 0 };
    std::atomic<int> captureSampleRate_ { 0 };
    std::atomic<std::size_t> captureCapacityFrames_ { 0 };

    std::atomic<tonetrace::capture::WorkflowPhase> phase_ {
        tonetrace::capture::WorkflowPhase::Ready };
    std::atomic<int> status_ { static_cast<int>(tonetrace::capture::Status::Ready) };
    std::atomic<int> lastCommand_ { 0 };
    std::atomic<double> captureSeconds_ { 0.0 };
    std::atomic<double> confidence_ { 0.0 };
    std::atomic<double> curveDriftDb_ { 60.0 };
    std::atomic<int> activeCaptureMode_ { static_cast<int>(tonetrace::capture::MatchMode::Voice) };
    std::atomic<int> lockedMatchModeIndex_ { 1 };
    std::atomic<int> lastWorkflowStep_ { 0 };
    std::atomic<int> pendingAudioCommand_ { 0 }; // 1 reset ref+target, 2 reset target
    // Latest workflow command requested while Correct is busy. Released 1.0.5
    // holds rather than discards such commands; the message timer replays it
    // after analysis completes through the same handleWorkflowCommand() path.
    std::atomic<int> deferredWorkflowCommand_ { 0 };
    std::atomic<bool> referenceReady_ { false };
    std::atomic<bool> targetCorrectable_ { false };
    std::atomic<bool> captureBlocked_ { false };
    std::atomic<bool> setupLockedNotice_ { false };
    std::atomic<bool> pendingReferenceStartAfterCapacity_ { false };
    std::atomic<bool> resetArmed_ { false };
    std::atomic<tonetrace::capture::WorkflowPhase> phaseBeforeReset_ {
        tonetrace::capture::WorkflowPhase::Ready };

    // Released correction engine. process() is audio-thread-only; analysis,
    // rebuild, state, and kernel reclamation stay on control/worker threads.
    tonetrace::jucebridge::CorrectionController correctionController_;
    std::atomic<bool> hasProfile_ { false };
    std::atomic<double> tailSeconds_ { 0.0 };

    std::thread analysisThread_;
    std::mutex analysisMutex_;
    std::condition_variable analysisCv_;
    std::optional<AnalysisJob> pendingAnalysisJob_;
    bool analysisThreadExit_ = false; // guarded by analysisMutex_
    std::atomic<bool> analysisBusy_ { false };
    std::atomic<bool> stateRestoreBusy_ { false };
    std::atomic<std::uint64_t> analysisEpoch_ { 1 };

    // Message-thread observation used to coalesce profile rebuilds when retained
    // captures are reinterpreted after Correct/Freeze.
    std::atomic<int> lastObservedMatchModeIndex_ { 1 };
    std::array<double, 7> lastObservedCorrectionSettings_ {};


    mutable std::mutex manualStateMutex_;
    std::vector<double> manualGains_ = std::vector<double>(30, 0.0);
    std::optional<tonetrace::SpectrumCapture> importedReference_;
    std::optional<tonetrace::SpectrumCapture> importedTarget_;

    // State may arrive before prepareToPlay creates the released realtime core.
    std::mutex pendingStateMutex_;
    std::string pendingCoreState_;
    bool pendingCoreStateFrozen_ = false;
    bool pendingCoreNeedsRebuild_ = false;
    double pendingCoreConfidence_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceProcessor)
};
