#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <sstream>

namespace
{
const tonetrace::product::ReleasedParameterSpec& released(unsigned int id)
{
    const auto* spec = tonetrace::product::findReleasedParameterById(id);
    jassert(spec != nullptr);
    return *spec;
}

juce::String formatCompact(double value, int decimals, const char* unit)
{
    auto text = juce::String(value, decimals);
    while (text.containsChar('.') && text.endsWithChar('0')) text = text.dropLastCharacters(1);
    if (text.endsWithChar('.')) text = text.dropLastCharacters(1);
    if (unit != nullptr && unit[0] != '\0') text << " " << unit;
    return text;
}

juce::String serializeManualGains(const std::vector<double>& gains)
{
    juce::String text;
    for (std::size_t i = 0; i < gains.size(); ++i)
    {
        if (i != 0) text << ";";
        text << juce::String(gains[i], 12);
    }
    return text;
}

bool parseManualGains(const juce::String& text, std::vector<double>& gains)
{
    gains.clear();
    if (text.isEmpty()) return true;
    juce::StringArray tokens;
    tokens.addTokens(text, ";", "");
    if (tokens.isEmpty() || tokens.size() > 120) return false;
    for (const auto& token : tokens)
    {
        const auto trimmed = token.trim();
        if (trimmed.isEmpty()) return false;
        const auto ascii = trimmed.toRawUTF8();
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(ascii, &end);
        if (end == ascii || end == nullptr || *end != '\0' || errno == ERANGE
            || !std::isfinite(value) || value < -120.0 || value > 120.0)
            return false;
        gains.push_back(value);
    }
    return true;
}
} // namespace

ToneTraceProcessor::ToneTraceProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    using namespace tonetrace::product;

    stateParameters_.reserve(kImplementedContinuousControls.size() + kImplementedToggleControls.size() + 1);

    for (std::size_t index = 0; index < kImplementedContinuousControls.size(); ++index)
    {
        const auto* control = &kImplementedContinuousControls[index];
        const auto* parameterSpec = control->parameter;
        auto attributes = juce::AudioParameterFloatAttributes()
            .withLabel(parameterSpec->unit)
            .withStringFromValueFunction([control](float value, int)
            {
                return juce::String::fromUTF8(formatContinuous(*control, value, false).c_str());
            })
            .withValueFromStringFunction([control](const juce::String& text)
            {
                double parsed = control->parameter->defaultValue;
                return parseContinuous(*control, text.toStdString(), parsed)
                    ? static_cast<float>(parsed)
                    : static_cast<float>(control->parameter->defaultValue);
            });

        auto* parameter = new juce::AudioParameterFloat(
            juce::ParameterID { parameterSpec->key, 1 },
            parameterSpec->name,
            juce::NormalisableRange<float> {
                static_cast<float>(parameterSpec->minimum),
                static_cast<float>(parameterSpec->maximum),
                static_cast<float>(control->publicResolution) },
            static_cast<float>(parameterSpec->defaultValue),
            attributes);

        addParameter(parameter);
        continuousParameters_[index] = parameter;
        stateParameters_.push_back({ juce::Identifier(parameterSpec->key), parameter });
    }

    juce::StringArray matchModeChoices;
    for (const auto* choice : kImplementedMatchMode.choices)
        matchModeChoices.add(choice);

    matchMode_ = new juce::AudioParameterChoice(
        juce::ParameterID { kImplementedMatchMode.parameter->key, 1 },
        kImplementedMatchMode.parameter->name,
        matchModeChoices,
        kImplementedMatchMode.defaultIndex,
        juce::AudioParameterChoiceAttributes {});
    addParameter(matchMode_);
    stateParameters_.push_back({ juce::Identifier(kImplementedMatchMode.parameter->key), matchMode_ });

    for (std::size_t index = 0; index < kImplementedToggleControls.size(); ++index)
    {
        const auto* spec = kImplementedToggleControls[index];
        auto* parameter = new juce::AudioParameterBool(
            juce::ParameterID { spec->key, 1 },
            spec->name,
            spec->defaultValue >= 0.5);
        addParameter(parameter);
        toggleParameters_[index] = parameter;
        stateParameters_.push_back({ juce::Identifier(spec->key), parameter });
    }

    const auto& workflowSpec = released(100);
    auto workflowAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int)
        {
            const auto step = std::clamp(static_cast<int>(std::lround(value)), 0, 7);
            return juce::String::fromUTF8(tonetrace::capture::workflowText(step));
        })
        .withValueFromStringFunction([](const juce::String& text)
        {
            for (int step = 0; step <= 7; ++step)
                if (text.equalsIgnoreCase(tonetrace::capture::workflowText(step)))
                    return static_cast<float>(step);
            return static_cast<float>(text.getDoubleValue());
        });
    workflowStep_ = new juce::AudioParameterFloat(
        juce::ParameterID { workflowSpec.key, 1 }, workflowSpec.name,
        juce::NormalisableRange<float> { 0.0F, 7.0F, 0.001F }, 0.0F,
        workflowAttributes);
    addParameter(workflowStep_);

    const auto makeTelemetry = [this](const tonetrace::product::ReleasedParameterSpec& spec,
                                      float interval,
                                      auto stringFromValue)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
            .withLabel(spec.unit)
            .withAutomatable(false)
            .withStringFromValueFunction(stringFromValue)
            .withValueFromStringFunction([&spec, stringFromValue](const juce::String& text)
            {
                if (spec.stepped || spec.releasedNumericId == 200)
                {
                    const int steps = spec.releasedNumericId == 200 ? 3 : static_cast<int>(spec.maximum);
                    for (int i = 0; i <= steps; ++i)
                    {
                        const float value = spec.releasedNumericId == 200 ? static_cast<float>(i) / 3.0F : static_cast<float>(i);
                        if (text.equalsIgnoreCase(stringFromValue(value, 0))) return value;
                    }
                }
                return text.getFloatValue();
            });
        auto* parameter = new juce::AudioParameterFloat(
            juce::ParameterID { spec.key, 1 }, spec.name,
            juce::NormalisableRange<float> {
                static_cast<float>(spec.minimum),
                static_cast<float>(spec.maximum), interval },
            static_cast<float>(spec.defaultValue), attributes);
        addParameter(parameter);
        return parameter;
    };

    statusParameter_ = makeTelemetry(released(230), 1.0F,
        [](float value, int)
        {
            const auto status = static_cast<tonetrace::capture::Status>(
                std::clamp(static_cast<int>(std::lround(value)), 0, 30));
            return juce::String::fromUTF8(tonetrace::capture::statusText(status));
        });
    lastCommandParameter_ = makeTelemetry(released(195), 1.0F,
        [](float value, int)
        {
            const auto step = std::clamp(static_cast<int>(std::lround(value)), 0, 7);
            return juce::String::fromUTF8(tonetrace::capture::workflowText(step));
        });
    confidenceParameter_ = makeTelemetry(released(200), 0.0F,
        [this](float value, int)
        {
            const auto level = std::clamp(static_cast<int>(std::lround(value * 3.0F)), 0, 3);
            return juce::String::fromUTF8(tonetrace::capture::confidenceText(
                level, captureSeconds_.load(std::memory_order_acquire) > 0.0));
        });
    curveDriftParameter_ = makeTelemetry(released(210), 0.001F,
        [](float value, int) { return formatCompact(value, 3, "dB"); });
    captureTimeParameter_ = makeTelemetry(released(220), 0.001F,
        [](float value, int) { return formatCompact(value, 3, "s accepted audio"); });

    activeCaptureMode_.store(matchModeIndex(), std::memory_order_release);
    lockedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
    synchronizeObservedControlState();
    analysisThread_ = std::thread([this] { analysisThreadMain(); });
    startTimerHz(kControlTimerHz);
}

ToneTraceProcessor::~ToneTraceProcessor()
{
    stopTimer();
    stopAnalysisThread();
}

void ToneTraceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostProcessingActive_.store(false, std::memory_order_release);

    const auto requestedRate = static_cast<int>(std::lround(sampleRate));
    const auto currentPhase = phase_.load(std::memory_order_acquire);
    const bool liveCapture = tonetrace::capture::isLiveCapturePhase(currentPhase);
    bool mustResetLiveCapture = false;

    {
        // prepareToPlay may arrive on a host control thread while the JUCE
        // message timer also performs inactive buffer growth. Serialize those
        // two control-plane operations without ever putting a lock on audio.
        const std::lock_guard<std::mutex> lock(captureControlMutex_);
        const int previousRate = captureSampleRate_.load(std::memory_order_acquire);
        const bool preserveLiveCapture = tonetrace::capture::shouldPreserveCaptureAcrossReprepare(
            currentPhase, previousRate, requestedRate,
            referenceCapture_.capacityFrames > 0 && targetCapture_.capacityFrames > 0);

        captureSampleRate_.store(requestedRate, std::memory_order_release);
        if (preserveLiveCapture)
        {
            const auto mode = static_cast<tonetrace::capture::MatchMode>(
                activeCaptureMode_.load(std::memory_order_acquire));
            const auto seconds = tonetrace::capture::maximumCaptureSeconds(mode);
            referenceCapture_.ensureCapacity(requestedRate, seconds);
            targetCapture_.ensureCapacity(requestedRate, seconds);
        }
        else
        {
            const auto mode = currentMatchMode();
            const auto seconds = tonetrace::capture::maximumCaptureSeconds(mode);
            referenceCapture_.prepare(requestedRate, seconds);
            targetCapture_.prepare(requestedRate, seconds);
            mustResetLiveCapture = liveCapture;
        }

        captureCapacityFrames_.store(
            std::min(referenceCapture_.capacityFrames, targetCapture_.capacityFrames),
            std::memory_order_release);
    }

    if (mustResetLiveCapture)
    {
        // A real sample-rate change cannot safely retain raw capture samples.
        invalidateAnalysisWork();
        resetWorkflowToReadyForStateLoad();
    }

    const auto corePrepare = correctionController_.prepare(requestedRate, 2);
    if (!corePrepare.accepted)
        setStatus(tonetrace::capture::Status::AnalysisFailed);

    // Host state commonly arrives before prepareToPlay. The textual core state
    // is fully validated in setStateInformation(), then installed here once the
    // sample-rate-specific realtime renderer exists.
    std::string pendingCore;
    bool pendingFrozen = false;
    bool pendingNeedsRebuild = false;
    double pendingConfidence = 0.0;
    {
        const std::lock_guard<std::mutex> lock(pendingStateMutex_);
        pendingCore = pendingCoreState_;
        pendingFrozen = pendingCoreStateFrozen_;
        pendingNeedsRebuild = pendingCoreNeedsRebuild_;
        pendingConfidence = pendingCoreConfidence_;
    }
    if (!pendingCore.empty() && corePrepare.accepted)
    {
        const auto restored = correctionController_.restoreProjectState(pendingCore);
        if (restored.accepted)
        {
            {
                const std::lock_guard<std::mutex> lock(pendingStateMutex_);
                if (pendingCoreState_ == pendingCore)
                {
                    pendingCoreState_.clear();
                    pendingCoreStateFrozen_ = false;
                    pendingCoreNeedsRebuild_ = false;
                    pendingCoreConfidence_ = 0.0;
                }
            }
            if (pendingFrozen)
            {
                applyRestoredFrozenWorkflow(pendingConfidence);
                if (pendingNeedsRebuild)
                {
                    setStatus(tonetrace::capture::Status::SetupChanged);
                    queueAnalysisJob(AnalysisJobKind::RebuildProfile);
                }
            }
            else
                resetWorkflowToReadyForStateLoad();
        }
        else
        {
            setStatus(restored.issue == tonetrace::ProfileIssue::RendererBusy
                ? tonetrace::capture::Status::RendererBusy
                : tonetrace::capture::Status::AnalysisFailed);
        }
    }

    hasProfile_.store(correctionController_.hasProfile(), std::memory_order_release);
    if (const auto snapshot = correctionController_.snapshotCopy(); snapshot != nullptr)
        tailSeconds_.store(snapshot->renderSettings.durationSeconds, std::memory_order_release);
    else
    {
        tailSeconds_.store(0.0, std::memory_order_release);
        const auto manual = manualGainsCopy();
        if (corePrepare.accepted && std::any_of(manual.begin(), manual.end(),
                [](double value) { return std::abs(value) > 1.0e-9; }))
            queueAnalysisJob(AnalysisJobKind::RebuildProfile);
    }

    gainSmoother_.reset(sampleRate, kGainSmoothingSeconds);
    const auto currentGain = juce::Decibels::decibelsToGain(
        continuousParameters_[tonetrace::product::kCorrectionGainIndex]->get());
    gainSmoother_.setCurrentAndTargetValue(currentGain);

    // Global bypass covers the complete JUCE-owned output path (released
    // correction + separate Correction Gain + generated tones). The released
    // realtime core used a 10 ms dry/wet transition; keep that exact timing here
    // because Correction Gain intentionally remains outside the vendored core.
    dryBuffer_.setSize(2, std::max(1, samplesPerBlock), false, false, true);
    outputBypass_.prepare(static_cast<int>(std::lround(sampleRate)),
                          toggleParameters_[tonetrace::product::kBypassIndex]->get());
    generatedTone_.stop();
    activeToneIsNavigation_ = false;
    pendingWarningTone_.store(false, std::memory_order_release);
    pendingNavigationTone_.store(false, std::memory_order_release);
}

void ToneTraceProcessor::setNonRealtime(bool offline) noexcept
{
    juce::AudioProcessor::setNonRealtime(offline);
    offlineRendering_.store(offline, std::memory_order_release);
    if (offline)
    {
        pendingWarningTone_.store(false, std::memory_order_release);
        pendingNavigationTone_.store(false, std::memory_order_release);
        stoppedTransportTone_.cancel();
    }
}

void ToneTraceProcessor::releaseResources()
{
    reset();
    stoppedTransportTone_.cancel();
    // Intentionally keep referenceCapture_ / targetCapture_. REAPER and other
    // hosts may deactivate an effect when transport stops; released 1.0.5
    // preserves a live Reference/Target capture across that same-rate lifecycle.
}

void ToneTraceProcessor::reset()
{
    // Keep the released responsibilities separate: CLAP pluginReset resets the
    // correction core's audio history, while ordinary stopTones() workflow
    // transitions do not. Drop queued sounds now and perform both audio-owned
    // resets safely at the next process block.
    // A new band-tone request made after reset remains eligible on resume.
    pendingWarningTone_.store(false, std::memory_order_release);
    pendingNavigationTone_.store(false, std::memory_order_release);
    pendingGeneratedToneStop_.store(true, std::memory_order_release);
    pendingCoreAudioReset_.store(true, std::memory_order_release);
    hostProcessingActive_.store(false, std::memory_order_release);
}

bool ToneTraceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ToneTraceProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midi);
    hostProcessingActive_.store(true, std::memory_order_release);
    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    if (pendingCoreAudioReset_.exchange(false, std::memory_order_acq_rel))
        correctionController_.resetAudio();
    if (pendingGeneratedToneStop_.exchange(false, std::memory_order_acq_rel))
    {
        generatedTone_.stop();
        activeToneIsNavigation_ = false;
    }

    // The writer count closes the only capture/control race that matters here.
    // A Correct request first changes phase away from capture, then the worker
    // waits for this count to reach zero before copying raw audio. A block that
    // entered before the phase change finishes its write; a later block sees the
    // destination phase and writes nothing. No lock/wait/allocation is added to audio.
    captureWriterCount_.fetch_add(1, std::memory_order_acq_rel);
    executePendingAudioCommand();
    captureInputSlice(buffer);
    captureWriterCount_.fetch_sub(1, std::memory_order_acq_rel);

    const auto channels = static_cast<std::size_t>(std::min(2, buffer.getNumChannels()));
    if (channels == 0) return;

    // JUCE explicitly permits processBlock() sizes that differ from the
    // samplesPerBlock estimate passed to prepareToPlay(). Keep the dry scratch
    // buffer control-thread allocated, then process an arbitrarily large host
    // block in bounded chunks rather than resizing/allocating on the audio thread.
    const int scratchSamples = dryBuffer_.getNumSamples();
    jassert(scratchSamples > 0 && dryBuffer_.getNumChannels() >= static_cast<int>(channels));
    if (scratchSamples <= 0 || dryBuffer_.getNumChannels() < static_cast<int>(channels)) return;

    const auto targetGain = juce::Decibels::decibelsToGain(
        continuousParameters_[tonetrace::product::kCorrectionGainIndex]->get());
    gainSmoother_.setTargetValue(targetGain);

    const bool bypassed = toggleParameters_[tonetrace::product::kBypassIndex]->get();
    for (int offset = 0; offset < numSamples;)
    {
        const int count = std::min(scratchSamples, numSamples - offset);
        const float* inputs[2] { nullptr, nullptr };
        float* outputs[2] { nullptr, nullptr };
        const float* dry[2] { nullptr, nullptr };

        for (std::size_t channel = 0; channel < channels; ++channel)
        {
            dryBuffer_.copyFrom(static_cast<int>(channel), 0, buffer,
                                static_cast<int>(channel), offset, count);
            inputs[channel] = buffer.getReadPointer(static_cast<int>(channel), offset);
            outputs[channel] = buffer.getWritePointer(static_cast<int>(channel), offset);
            dry[channel] = dryBuffer_.getReadPointer(static_cast<int>(channel));
        }

        // Released 1.0.5 HeadlessPluginCore: identity until a validated profile
        // exists, then zero-latency click-free realtime convolution in place.
        correctionController_.process(inputs, outputs, channels,
                                      static_cast<std::size_t>(count));

        // Correction Gain keeps the already human-passed Candidate C / Foundation
        // 01a ownership path: one separate 20 ms output-trim stage downstream of
        // the learned tonal correction. Advance the smoother once per frame so
        // every channel receives the same gain while chunking remains transparent.
        for (int sample = 0; sample < count; ++sample)
        {
            const float gain = gainSmoother_.getNextValue();
            for (std::size_t channel = 0; channel < channels; ++channel)
                outputs[channel][sample] *= gain;
        }

        renderGeneratedTone(outputs, channels, static_cast<std::size_t>(count));

        // Released Emergency Clip Guard is a plug-in-output safety ceiling and
        // deliberately is not part of exported impulse responses.
        if (!bypassed)
        {
            const float guard = juce::Decibels::decibelsToGain(
                continuousParameters_[tonetrace::product::kEmergencyClipGuardIndex]->get());
            for (std::size_t channel = 0; channel < channels; ++channel)
                for (int sample = 0; sample < count; ++sample)
                    outputs[channel][sample] = juce::jlimit(-guard, guard, outputs[channel][sample]);
        }

        applyGlobalBypass(outputs, dry, channels, static_cast<std::size_t>(count));
        offset += count;
    }
}

bool ToneTraceProcessor::statusTonesAllowed() const noexcept
{
    return navigationTonesAllowed()
        && phase_.load(std::memory_order_acquire) != tonetrace::capture::WorkflowPhase::Frozen;
}

bool ToneTraceProcessor::navigationTonesAllowed() const noexcept
{
    return !offlineRendering_.load(std::memory_order_acquire)
        && toggleParameters_[tonetrace::product::kToneNotificationsIndex]->get()
        && continuousParameters_[tonetrace::product::kConfidenceToneVolumeIndex]->get() > -59.5F
        && !toggleParameters_[tonetrace::product::kBypassIndex]->get();
}

void ToneTraceProcessor::requestNavigationTone(double frequencyHz) noexcept
{
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0 || !navigationTonesAllowed()) return;

    // Released 1.0.5 always queued the audio-thread band tone, then used the
    // asynchronous Windows Beep worker as a fallback only when CLAP was not
    // processing. Preserve that asymmetry exactly.
    pendingNavigationFromHz_.store(frequencyHz, std::memory_order_relaxed);
    pendingNavigationToHz_.store(frequencyHz, std::memory_order_relaxed);
    pendingNavigationDurationMs_.store(125.0, std::memory_order_relaxed);
    pendingNavigationTone_.store(true, std::memory_order_release);

    if (!hostProcessingActive_.load(std::memory_order_acquire) && navigationTonesAllowed())
        stoppedTransportTone_.requestTone(frequencyHz);
}

void ToneTraceProcessor::requestNavigationSweep(double fromHz, double toHz, int bandCount,
                                                double durationMs) noexcept
{
    if (!std::isfinite(fromHz) || !std::isfinite(toHz) || !std::isfinite(durationMs)
        || fromHz <= 0.0 || toHz <= 0.0 || bandCount <= 0 || durationMs <= 0.0
        || !navigationTonesAllowed())
        return;

    // Released 1.0.5 sent stopped-transport page sweeps only to the Beep
    // worker so a stale sweep could not start later when processing resumed.
    if (!hostProcessingActive_.load(std::memory_order_acquire) && navigationTonesAllowed())
    {
        stoppedTransportTone_.requestSweep(fromHz, toHz, bandCount);
        return;
    }

    pendingNavigationFromHz_.store(fromHz, std::memory_order_relaxed);
    pendingNavigationToHz_.store(toHz, std::memory_order_relaxed);
    pendingNavigationDurationMs_.store(durationMs, std::memory_order_relaxed);
    pendingNavigationTone_.store(true, std::memory_order_release);
}

void ToneTraceProcessor::requestWarningTone() noexcept
{
    pendingWarningTone_.store(true, std::memory_order_release);
}

void ToneTraceProcessor::cancelActiveTones() noexcept
{
    // Match released 1.0.5's stopTones() semantics without stopping the
    // audio-thread-owned oscillator from the message/worker thread. Any tone
    // already rendering is stopped at the next process block; queued
    // navigation/warning tones and the stopped-transport worker are cancelled
    // immediately so an old cue cannot start after a workflow/import change.
    pendingWarningTone_.store(false, std::memory_order_release);
    pendingNavigationTone_.store(false, std::memory_order_release);
    pendingGeneratedToneStop_.store(true, std::memory_order_release);
    stoppedTransportTone_.cancel();
}

void ToneTraceProcessor::startConfidenceTone(int oldLevel, int newLevel) noexcept
{
    if (!statusTonesAllowed() || generatedTone_.active()) return;
    if (const auto sweep = tonetrace::tone::confidenceTransition(oldLevel, newLevel); sweep.has_value())
    {
        activeToneIsNavigation_ = false;
        generatedTone_.start(*sweep, captureSampleRate_.load(std::memory_order_acquire));
    }
}

void ToneTraceProcessor::renderGeneratedTone(float* const* output,
                                             std::size_t channels,
                                             std::size_t count) noexcept
{
    const int sampleRate = captureSampleRate_.load(std::memory_order_acquire);
    if (sampleRate <= 0)
    {
        generatedTone_.stop();
        activeToneIsNavigation_ = false;
        pendingWarningTone_.store(false, std::memory_order_release);
        pendingNavigationTone_.store(false, std::memory_order_release);
        return;
    }

    const bool navigationAllowed = navigationTonesAllowed();
    const bool statusAllowed = statusTonesAllowed();
    if (!navigationAllowed)
    {
        generatedTone_.stop();
        activeToneIsNavigation_ = false;
        pendingWarningTone_.store(false, std::memory_order_release);
        pendingNavigationTone_.store(false, std::memory_order_release);
        return;
    }

    if (pendingNavigationTone_.exchange(false, std::memory_order_acq_rel))
    {
        const double fromHz = pendingNavigationFromHz_.load(std::memory_order_relaxed);
        const double toHz = pendingNavigationToHz_.load(std::memory_order_relaxed);
        const double durationMs = pendingNavigationDurationMs_.load(std::memory_order_relaxed);
        generatedTone_.start({ fromHz, toHz, durationMs }, sampleRate);
        activeToneIsNavigation_ = generatedTone_.active();
    }

    if (pendingWarningTone_.exchange(false, std::memory_order_acq_rel) && statusAllowed)
    {
        generatedTone_.start(tonetrace::tone::warningSweep(), sampleRate);
        activeToneIsNavigation_ = false;
    }

    if (generatedTone_.active() && !activeToneIsNavigation_ && !statusAllowed)
    {
        generatedTone_.stop();
        return;
    }

    if (!generatedTone_.active() || output == nullptr || channels == 0 || count == 0) return;
    const auto linearGain = juce::Decibels::decibelsToGain(
        continuousParameters_[tonetrace::product::kConfidenceToneVolumeIndex]->get());
    generatedTone_.render(output, channels, count, sampleRate, linearGain);
    if (!generatedTone_.active()) activeToneIsNavigation_ = false;
}

void ToneTraceProcessor::applyGlobalBypass(float* const* wet,
                                           const float* const* dry,
                                           std::size_t channels,
                                           std::size_t count) noexcept
{
    outputBypass_.process(wet, dry, channels, count,
                          toggleParameters_[tonetrace::product::kBypassIndex]->get());
}

void ToneTraceProcessor::executePendingAudioCommand() noexcept
{
    const int command = pendingAudioCommand_.exchange(0, std::memory_order_acq_rel);
    if (command == 1)
    {
        referenceCapture_.reset();
        targetCapture_.reset();
    }
    else if (command == 2)
    {
        targetCapture_.reset();
    }
}

void ToneTraceProcessor::captureInputSlice(juce::AudioBuffer<float>& buffer) noexcept
{
    const auto currentPhase = phase_.load(std::memory_order_acquire);
    tonetrace::capture::CaptureBuffer* destination = nullptr;
    if (currentPhase == tonetrace::capture::WorkflowPhase::CapturingReference)
        destination = &referenceCapture_;
    else if (currentPhase == tonetrace::capture::WorkflowPhase::CapturingTarget)
        destination = &targetCapture_;
    const int sampleRate = captureSampleRate_.load(std::memory_order_acquire);
    if (destination == nullptr || sampleRate <= 0) return;

    const auto channels = static_cast<std::size_t>(std::min(2, buffer.getNumChannels()));
    if (channels == 0) return;
    const float* input[2] { nullptr, nullptr };
    for (std::size_t channel = 0; channel < channels; ++channel)
        input[channel] = buffer.getReadPointer(static_cast<int>(channel));

    const auto mode = static_cast<tonetrace::capture::MatchMode>(
        activeCaptureMode_.load(std::memory_order_acquire));
    const int oldConfidenceLevel = destination->confidenceLevel;
    destination->append(input, channels, static_cast<std::size_t>(buffer.getNumSamples()),
                        sampleRate, mode);

    const auto seconds = destination->validSeconds(sampleRate);
    const auto confidenceLevel = destination->confidenceLevel;
    if (confidenceLevel != oldConfidenceLevel)
        startConfidenceTone(oldConfidenceLevel, confidenceLevel);
    captureSeconds_.store(seconds, std::memory_order_release);
    confidence_.store(static_cast<double>(confidenceLevel) / 3.0, std::memory_order_release);
    curveDriftDb_.store(std::clamp(destination->stability * 60.0, 0.0, 60.0),
                        std::memory_order_release);

    if (currentPhase == tonetrace::capture::WorkflowPhase::CapturingReference)
    {
        const bool ready = destination->readyForSave(sampleRate);
        referenceReady_.store(ready, std::memory_order_release);
        if (ready) captureBlocked_.store(false, std::memory_order_release);
    }
    else
    {
        const bool correctable = confidenceLevel >= 1 || destination->overflowed;
        targetCorrectable_.store(correctable, std::memory_order_release);
        if (correctable) captureBlocked_.store(false, std::memory_order_release);
    }

    if (destination->overflowed)
    {
        setStatus(tonetrace::capture::Status::CaptureFull);
        return;
    }

    if (setupLockedNotice_.load(std::memory_order_acquire))
    {
        setStatus(tonetrace::capture::Status::SetupLocked);
        return;
    }

    if (captureBlocked_.load(std::memory_order_acquire))
    {
        setStatus(currentPhase == tonetrace::capture::WorkflowPhase::CapturingReference
            ? tonetrace::capture::Status::CannotSaveYet
            : tonetrace::capture::Status::InvalidCapture);
        return;
    }

    setStatus(tonetrace::capture::captureStatus(
        currentPhase, confidenceLevel, *destination, sampleRate));
}

juce::AudioParameterFloat& ToneTraceProcessor::continuousParameter(std::size_t index) noexcept
{
    jassert(index < continuousParameters_.size());
    return *continuousParameters_[index];
}

const juce::AudioParameterFloat& ToneTraceProcessor::continuousParameter(std::size_t index) const noexcept
{
    jassert(index < continuousParameters_.size());
    return *continuousParameters_[index];
}

double ToneTraceProcessor::continuousValue(std::size_t index) const noexcept
{
    jassert(index < continuousParameters_.size());
    return static_cast<double>(continuousParameters_[index]->get());
}

void ToneTraceProcessor::beginContinuousGesture(std::size_t index)
{
    jassert(index < continuousParameters_.size());
    continuousParameters_[index]->beginChangeGesture();
}

void ToneTraceProcessor::setContinuousFromUi(std::size_t index, double value)
{
    jassert(index < continuousParameters_.size());
    const auto& spec = tonetrace::product::kImplementedContinuousControls[index];
    const auto canonical = static_cast<float>(tonetrace::product::canonicalise(spec, value));
    auto* parameter = continuousParameters_[index];
    parameter->setValueNotifyingHost(parameter->convertTo0to1(canonical));
}

void ToneTraceProcessor::endContinuousGesture(std::size_t index)
{
    jassert(index < continuousParameters_.size());
    continuousParameters_[index]->endChangeGesture();
}

int ToneTraceProcessor::matchModeIndex() const noexcept
{
    return matchMode_->getIndex();
}

void ToneTraceProcessor::beginMatchModeGesture()
{
    matchMode_->beginChangeGesture();
}

void ToneTraceProcessor::setMatchModeFromUi(int index)
{
    if (captureActive())
    {
        setupLockedNotice_.store(true, std::memory_order_release);
        setStatus(tonetrace::capture::Status::SetupLocked);
        return;
    }

    const auto bounded = std::clamp(index, 0,
        static_cast<int>(tonetrace::product::kImplementedMatchMode.choices.size()) - 1);
    matchMode_->setValueNotifyingHost(matchMode_->convertTo0to1(static_cast<float>(bounded)));
    lockedMatchModeIndex_.store(bounded, std::memory_order_release);

    if (captureSampleRate_.load(std::memory_order_acquire) > 0)
        ensureCaptureCapacityForMode(tonetrace::capture::matchModeFromIndex(bounded));
}

void ToneTraceProcessor::endMatchModeGesture()
{
    matchMode_->endChangeGesture();
}

void ToneTraceProcessor::setWorkflowFromUi(int step)
{
    const auto bounded = std::clamp(step, 0, 7);
    workflowStep_->beginChangeGesture();
    workflowStep_->setValueNotifyingHost(
        workflowStep_->convertTo0to1(static_cast<float>(bounded)));
    workflowStep_->endChangeGesture();
    consumeWorkflowParameterChange();
}

int ToneTraceProcessor::workflowStep() const noexcept
{
    return lastWorkflowStep_.load(std::memory_order_acquire);
}

int ToneTraceProcessor::statusCode() const noexcept
{
    return status_.load(std::memory_order_acquire);
}

int ToneTraceProcessor::lastCommandCode() const noexcept
{
    return lastCommand_.load(std::memory_order_acquire);
}

double ToneTraceProcessor::captureSeconds() const noexcept
{
    return captureSeconds_.load(std::memory_order_acquire);
}

double ToneTraceProcessor::captureConfidence() const noexcept
{
    return confidence_.load(std::memory_order_acquire);
}

double ToneTraceProcessor::curveDriftDb() const noexcept
{
    return curveDriftDb_.load(std::memory_order_acquire);
}

bool ToneTraceProcessor::captureActive() const noexcept
{
    const auto current = phase_.load(std::memory_order_acquire);
    return current == tonetrace::capture::WorkflowPhase::CapturingReference
        || current == tonetrace::capture::WorkflowPhase::CapturingTarget;
}

juce::String ToneTraceProcessor::statusSummary() const
{
    const auto currentStatus = static_cast<tonetrace::capture::Status>(statusCode());
    const auto seconds = captureSeconds();
    const auto level = std::clamp(static_cast<int>(std::lround(captureConfidence() * 3.0)), 0, 3);

    juce::String summary;
    summary << "Status: " << tonetrace::capture::statusText(currentStatus) << ".\n";
    summary << "Last action: " << tonetrace::capture::workflowText(lastCommandCode()) << ".\n";
    summary << "Capture time: " << formatCompact(seconds, 3, "seconds accepted audio") << ".\n";
    summary << "Confidence: " << tonetrace::capture::confidenceText(level, seconds > 0.0) << ".\n";
    summary << "Curve drift: " << formatCompact(curveDriftDb(), 3, "dB") << ".";
    return summary;
}


std::unique_ptr<tonetrace::ProfileSnapshot> ToneTraceProcessor::profileSnapshotCopy() const
{
    return correctionController_.snapshotCopy();
}

tonetrace::IrRenderSettings ToneTraceProcessor::graphRenderSettings() const
{
    tonetrace::IrRenderSettings render;
    render.sampleRate = std::max(8000, correctionController_.sampleRate());
    render.correctionStrength = continuousValue(tonetrace::product::kCorrectionStrengthIndex);
    render.correctionSharpness = continuousValue(tonetrace::product::kCorrectionSharpnessIndex);
    render.correctionGainDb = continuousValue(tonetrace::product::kCorrectionGainIndex);
    render.rangeLowHz = continuousValue(tonetrace::product::kCorrectionRangeLowIndex);
    render.rangeHighHz = continuousValue(tonetrace::product::kCorrectionRangeHighIndex);
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        render.manualGains = manualGains_;
    }
    return render;
}

double ToneTraceProcessor::graphMaximumCorrectionDb() const noexcept
{
    return toggleValue(tonetrace::product::kFullCorrectionRangeIndex)
        ? 60.0
        : continuousValue(tonetrace::product::kMaximumCorrectionIndex);
}

std::vector<double> ToneTraceProcessor::manualGainsCopy() const
{
    const std::lock_guard<std::mutex> lock(manualStateMutex_);
    return manualGains_;
}

int ToneTraceProcessor::bandCount() const noexcept
{
    return std::clamp(static_cast<int>(std::lround(
        continuousValue(tonetrace::product::kCorrectionResolutionIndex))), 1, 120);
}

static std::pair<double, double> toneTraceBandRange(const tonetrace::ProfileSnapshot* snapshot)
{
    double low = 20.0;
    double high = 20000.0;
    if (snapshot != nullptr)
    {
        low = std::max(snapshot->uncappedModel.analysisLowHz, 20.0);
        high = std::min(snapshot->uncappedModel.analysisHighHz, 20000.0);
    }
    if (!std::isfinite(low) || !std::isfinite(high) || high <= low)
        return { 20.0, 20000.0 };
    return { low, high };
}

static double toneTraceBandFrequency(int count, std::size_t index,
                                     const tonetrace::ProfileSnapshot* snapshot)
{
    if (count <= 0 || index >= static_cast<std::size_t>(count)) return 0.0;
    const auto [low, high] = toneTraceBandRange(snapshot);
    if (count <= 1) return std::sqrt(low * high);
    const double fraction = static_cast<double>(index) / static_cast<double>(count - 1);
    return low * std::pow(high / low, fraction);
}

static double toneTraceBandLimit(const tonetrace::ProfileSnapshot* snapshot)
{
    if (snapshot != nullptr
        && std::isfinite(snapshot->matchSettings.maximumCorrectionDb)
        && snapshot->matchSettings.maximumCorrectionDb > 0.0)
        return std::clamp(snapshot->matchSettings.maximumCorrectionDb, 1.0, 60.0);
    return 60.0;
}

std::pair<double, double> ToneTraceProcessor::bandFrequencyRangeHz() const
{
    const auto snapshot = correctionController_.snapshotCopy();
    return toneTraceBandRange(snapshot.get());
}

double ToneTraceProcessor::bandFrequencyHz(std::size_t index) const
{
    const int count = bandCount();
    const auto snapshot = correctionController_.snapshotCopy();
    return toneTraceBandFrequency(count, index, snapshot.get());
}

double ToneTraceProcessor::bandAutomaticDb(std::size_t index) const
{
    const int count = bandCount();
    const auto snapshot = correctionController_.snapshotCopy();
    if (snapshot == nullptr || index >= static_cast<std::size_t>(count)) return 0.0;
    const double frequency = toneTraceBandFrequency(count, index, snapshot.get());
    if (frequency < snapshot->renderSettings.rangeLowHz
        || frequency > snapshot->renderSettings.rangeHighHz)
        return 0.0;
    return tonetrace::evaluateCorrectionAt(
        snapshot->uncappedModel,
        snapshot->matchSettings.maximumCorrectionDb,
        snapshot->renderSettings,
        frequency).automaticDb;
}

double ToneTraceProcessor::bandValueDb(std::size_t index) const
{
    const int count = bandCount();
    if (index >= static_cast<std::size_t>(count)) return 0.0;
    const auto snapshot = correctionController_.snapshotCopy();
    const double frequency = toneTraceBandFrequency(count, index, snapshot.get());
    if (frequency < continuousValue(tonetrace::product::kCorrectionRangeLowIndex)
        || frequency > continuousValue(tonetrace::product::kCorrectionRangeHighIndex))
        return 0.0;

    double automatic = 0.0;
    if (snapshot != nullptr
        && frequency >= snapshot->renderSettings.rangeLowHz
        && frequency <= snapshot->renderSettings.rangeHighHz)
    {
        automatic = tonetrace::evaluateCorrectionAt(
            snapshot->uncappedModel,
            snapshot->matchSettings.maximumCorrectionDb,
            snapshot->renderSettings,
            frequency).automaticDb;
    }

    const std::lock_guard<std::mutex> lock(manualStateMutex_);
    if (index >= manualGains_.size()) return 0.0;
    return std::clamp(automatic + manualGains_[index],
                      -toneTraceBandLimit(snapshot.get()),
                      toneTraceBandLimit(snapshot.get()));
}

double ToneTraceProcessor::bandLimitDb() const
{
    const auto snapshot = correctionController_.snapshotCopy();
    return toneTraceBandLimit(snapshot.get());
}

ToneTraceBandDisplayState ToneTraceProcessor::bandDisplayState() const
{
    ToneTraceBandDisplayState state;
    const int count = bandCount();
    const auto snapshot = correctionController_.snapshotCopy();
    std::vector<double> manual;
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        manual = manualGains_;
    }

    state.limitDb = toneTraceBandLimit(snapshot.get());
    state.frequenciesHz.resize(static_cast<std::size_t>(count));
    state.automaticDb.resize(static_cast<std::size_t>(count));
    state.finalDb.resize(static_cast<std::size_t>(count));

    const double activeLow = continuousValue(tonetrace::product::kCorrectionRangeLowIndex);
    const double activeHigh = continuousValue(tonetrace::product::kCorrectionRangeHighIndex);
    for (int band = 0; band < count; ++band)
    {
        const auto index = static_cast<std::size_t>(band);
        const double frequency = toneTraceBandFrequency(count, index, snapshot.get());
        state.frequenciesHz[index] = frequency;

        double automatic = 0.0;
        if (snapshot != nullptr
            && frequency >= snapshot->renderSettings.rangeLowHz
            && frequency <= snapshot->renderSettings.rangeHighHz)
        {
            automatic = tonetrace::evaluateCorrectionAt(
                snapshot->uncappedModel,
                snapshot->matchSettings.maximumCorrectionDb,
                snapshot->renderSettings,
                frequency).automaticDb;
        }
        state.automaticDb[index] = automatic;

        if (frequency < activeLow || frequency > activeHigh || index >= manual.size())
            state.finalDb[index] = 0.0;
        else
            state.finalDb[index] = std::clamp(automatic + manual[index],
                                               -state.limitDb, state.limitDb);
    }
    return state;
}

void ToneTraceProcessor::setBandValueDb(std::size_t index, double finalDb)
{
    if (analysisBusy_.load(std::memory_order_acquire)) return;
    const int count = bandCount();
    if (index >= static_cast<std::size_t>(count) || !std::isfinite(finalDb)) return;
    const double automatic = bandAutomaticDb(index);
    const double limit = bandLimitDb();
    const double desired = std::clamp(finalDb, -limit, limit);
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        if (manualGains_.size() != static_cast<std::size_t>(count))
            manualGains_.resize(static_cast<std::size_t>(count), 0.0);
        manualGains_[index] = std::clamp(desired - automatic, -120.0, 120.0);
    }
    queueAnalysisJob(AnalysisJobKind::RebuildProfile);
}

void ToneTraceProcessor::neutralizeBand(std::size_t index)
{
    setBandValueDb(index, 0.0);
}

std::optional<tonetrace::SpectrumCapture> ToneTraceProcessor::exportableReference() const
{
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        if (importedReference_.has_value()) return importedReference_;
    }
    if (phase_.load(std::memory_order_acquire) == tonetrace::capture::WorkflowPhase::CapturingTarget
        && referenceReady_.load(std::memory_order_acquire))
    {
        while (captureWriterCount_.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
        try
        {
            tonetrace::MatchEngine engine;
            auto settings = currentCorrectionSettings();
            tonetrace::MatchSettings match;
            match.mode = settings.mode;
            match.rangeLowHz = 10.0;
            match.rangeHighHz = 30000.0;
            match.resolution = settings.resolution;
            match.maximumCorrectionDb = 60.0;
            return engine.capture(copyCaptureAudio(referenceCapture_, captureSampleRate_.load()), match);
        }
        catch (...) {}
    }
    if (const auto snapshot = correctionController_.snapshotCopy(); snapshot != nullptr)
        return snapshot->reference;
    return std::nullopt;
}

std::optional<tonetrace::SpectrumCapture> ToneTraceProcessor::exportableTarget() const
{
    if (const auto snapshot = correctionController_.snapshotCopy(); snapshot != nullptr)
        return snapshot->target;
    const std::lock_guard<std::mutex> lock(manualStateMutex_);
    return importedTarget_;
}

std::optional<tonetrace::AudioBuffer> ToneTraceProcessor::exportableImpulseResponse() const
{
    const int rate = correctionController_.sampleRate();
    if (rate <= 0) return std::nullopt;

    tonetrace::AudioBuffer audio;
    audio.sampleRate = rate;
    if (auto snapshot = correctionController_.snapshotCopy(); snapshot != nullptr)
    {
        snapshot->renderSettings.sampleRate = rate;
        snapshot->renderSettings.correctionGainDb =
            continuousValue(tonetrace::product::kCorrectionGainIndex);
        audio.channels = { tonetrace::renderProfileKernel(*snapshot) };
        return audio;
    }

    tonetrace::IrRenderSettings render;
    render.sampleRate = rate;
    render.correctionStrength = continuousValue(tonetrace::product::kCorrectionStrengthIndex);
    render.correctionSharpness = continuousValue(tonetrace::product::kCorrectionSharpnessIndex);
    render.correctionGainDb = continuousValue(tonetrace::product::kCorrectionGainIndex);
    render.rangeLowHz = continuousValue(tonetrace::product::kCorrectionRangeLowIndex);
    render.rangeHighHz = continuousValue(tonetrace::product::kCorrectionRangeHighIndex);
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        render.manualGains = manualGains_;
    }
    if (!tonetrace::hasManualCorrection(render)) return std::nullopt;
    audio.channels = { tonetrace::renderManualCorrectionIr(render) };
    return audio;
}

ToneTraceTransferResult ToneTraceProcessor::importReference(const tonetrace::SpectrumCapture& capture)
{
    try { tonetrace::validateSpectrumCapture(capture); }
    catch (const std::exception& e) { return { false, juce::String::fromUTF8(e.what()) }; }
    const int rate = correctionController_.sampleRate();
    if (rate <= 0) return { false, "Audio engine is not prepared." };
    const auto [rangeLowHz, rangeHighHz] = currentTransferRangeHz(20.0, 20000.0);
    const auto check = tonetrace::assessCaptureImport(capture, rangeLowHz, rangeHighHz, rate);
    if (!check.usable) return { false, juce::String::fromUTF8(check.reason.c_str()) };

    cancelActiveTones();
    invalidateAnalysisWork();
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        importedReference_ = capture;
        importedTarget_.reset();
    }
    referenceReady_.store(true, std::memory_order_release);
    targetCorrectable_.store(false, std::memory_order_release);
    activeCaptureMode_.store(static_cast<int>(currentMatchMode()), std::memory_order_release);
    lockedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
    captureBlocked_.store(false, std::memory_order_release);
    setupLockedNotice_.store(false, std::memory_order_release);
    pendingAudioCommand_.store(2, std::memory_order_release);
    phase_.store(tonetrace::capture::WorkflowPhase::CapturingTarget, std::memory_order_release);
    captureSeconds_.store(0.0, std::memory_order_release);
    confidence_.store(0.0, std::memory_order_release);
    curveDriftDb_.store(60.0, std::memory_order_release);
    setStatus(tonetrace::capture::Status::ImportedReference);
    setWorkflowStepFromEngine(2);
    return { true, check.truncatedByNyquist
        ? "Reference imported; content above this project's Nyquist will not be applied."
        : juce::String{} };
}

ToneTraceTransferResult ToneTraceProcessor::importTarget(const tonetrace::SpectrumCapture& capture)
{
    try { tonetrace::validateSpectrumCapture(capture); }
    catch (const std::exception& e) { return { false, juce::String::fromUTF8(e.what()) }; }
    const int rate = correctionController_.sampleRate();
    if (rate <= 0) return { false, "Audio engine is not prepared." };
    const auto [rangeLowHz, rangeHighHz] = currentTransferRangeHz(20.0, 20000.0);
    const auto check = tonetrace::assessCaptureImport(capture, rangeLowHz, rangeHighHz, rate);
    if (!check.usable) return { false, juce::String::fromUTF8(check.reason.c_str()) };

    bool hasImportedReference = false;
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        hasImportedReference = importedReference_.has_value();
    }
    const auto phase = phase_.load(std::memory_order_acquire);
    const bool hasLiveReference = referenceReady_.load(std::memory_order_acquire)
        && (phase == tonetrace::capture::WorkflowPhase::CapturingReference
            || phase == tonetrace::capture::WorkflowPhase::CapturingTarget);
    if (!hasImportedReference && !hasLiveReference)
    {
        setStatus(tonetrace::capture::Status::InvalidCapture);
        requestWarningTone();
        return { false, "Import a Reference curve before importing a Target curve." };
    }

    cancelActiveTones();
    // Match the released CLAP transaction: publish the imported Target first,
    // close capture, then let the normal analysis worker build/commit the new
    // profile. This keeps file-dialog/UI work bounded and leaves the last
    // known-good correction active until the replacement is accepted.
    invalidateAnalysisWork();
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        importedTarget_ = capture;
    }
    captureBlocked_.store(false, std::memory_order_release);
    setupLockedNotice_.store(false, std::memory_order_release);
    targetCorrectable_.store(false, std::memory_order_release);
    pendingAudioCommand_.store(0, std::memory_order_release);
    phase_.store(tonetrace::capture::WorkflowPhase::Preview, std::memory_order_release);
    setStatus(tonetrace::capture::Status::ImportedTarget);
    analysisBusy_.store(true, std::memory_order_release);
    setWorkflowStepFromEngine(3);
    queueAnalysisJob(AnalysisJobKind::AnalyzeCaptured);
    return { true, check.truncatedByNyquist
        ? "Target imported; content above this project's Nyquist will not be applied."
        : juce::String{} };
}

ToneTraceTransferResult ToneTraceProcessor::importModel(const tonetrace::CorrectionModel& model)
{
    try { tonetrace::validateCorrectionModel(model); }
    catch (const std::exception& e) { return { false, juce::String::fromUTF8(e.what()) }; }
    const int rate = correctionController_.sampleRate();
    if (rate <= 0) return { false, "Audio engine is not prepared." };
    const auto [rangeLowHz, rangeHighHz] = currentTransferRangeHz(10.0, 30000.0);
    const auto check = tonetrace::assessModelImport(model, rangeLowHz, rangeHighHz, rate);
    if (!check.usable) return { false, juce::String::fromUTF8(check.reason.c_str()) };

    // Build the complete candidate first. Nothing visible or persistent is
    // published until the realtime core accepts this model.
    tonetrace::SpectrumCapture reference, target;
    reference.sampleRate = target.sampleRate = rate;
    reference.fftSize = target.fftSize = 1;
    reference.acceptedFrames = target.acceptedFrames = 3;
    reference.confidence = target.confidence = 1.0;
    reference.points.reserve(model.nodes.size()); target.points.reserve(model.nodes.size());
    for (const auto& node : model.nodes)
    {
        reference.points.push_back({ node.frequencyHz, 0.0, 1.0, 0.0 });
        target.points.push_back({ node.frequencyHz, -node.gainDb, 1.0, 0.0 });
    }

    const auto oldSettings = currentCorrectionSettings();
    tonetrace::ProfileSnapshot candidate;
    candidate.reference = reference;
    candidate.target = target;
    candidate.uncappedModel = model;
    candidate.matchSettings.mode = model.mode;
    candidate.matchSettings.resolution = model.resolution;
    candidate.matchSettings.rangeLowHz = model.analysisLowHz;
    candidate.matchSettings.rangeHighHz = model.analysisHighHz;
    candidate.matchSettings.maximumCorrectionDb = oldSettings.fullCorrectionRange ? 60.0 : oldSettings.maximumCorrectionDb;
    candidate.renderSettings.sampleRate = rate;
    candidate.renderSettings.correctionStrength = oldSettings.correctionStrength;
    candidate.renderSettings.correctionSharpness = oldSettings.correctionSharpness;
    candidate.renderSettings.correctionGainDb = 0.0;
    candidate.renderSettings.rangeLowHz = model.analysisLowHz;
    candidate.renderSettings.rangeHighHz = model.analysisHighHz;
    candidate.renderSettings.manualGains.assign(static_cast<std::size_t>(std::max(1, model.resolution)), 0.0);

    invalidateAnalysisWork();
    const auto result = correctionController_.commitSnapshot(candidate);
    if (!result.accepted)
    {
        setStatus(result.issue == tonetrace::ProfileIssue::RendererBusy
            ? tonetrace::capture::Status::RendererBusy
            : tonetrace::capture::Status::InvalidCapture);
        if (result.issue != tonetrace::ProfileIssue::RendererBusy) requestWarningTone();
        return { false, juce::String::fromUTF8(result.message.c_str()) };
    }

    // Commit succeeded. Now publish the imported model's control/state view as
    // one transaction. These calls may notify the host but cannot invalidate the
    // already accepted correction.
    setMatchModeFromUi(static_cast<int>(model.mode));
    setContinuousFromUi(tonetrace::product::kCorrectionResolutionIndex, model.resolution);
    setContinuousFromUi(tonetrace::product::kCorrectionRangeLowIndex, model.analysisLowHz);
    setContinuousFromUi(tonetrace::product::kCorrectionRangeHighIndex, model.analysisHighHz);
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        manualGains_.assign(static_cast<std::size_t>(std::max(1, model.resolution)), 0.0);
        importedReference_ = reference;
        importedTarget_ = target;
    }

    synchronizeObservedControlState();
    hasProfile_.store(true, std::memory_order_release);
    tailSeconds_.store(candidate.renderSettings.durationSeconds, std::memory_order_release);
    referenceReady_.store(true, std::memory_order_release);
    targetCorrectable_.store(false, std::memory_order_release);
    phase_.store(tonetrace::capture::WorkflowPhase::Frozen, std::memory_order_release);
    confidence_.store(1.0, std::memory_order_release);
    curveDriftDb_.store(0.0, std::memory_order_release);
    setStatus(tonetrace::capture::Status::Frozen);
    setWorkflowStepFromEngine(4);
    return { true, check.truncatedByNyquist
        ? "Model imported; correction above this project's Nyquist will not be applied."
        : juce::String{} };
}

void ToneTraceProcessor::timerCallback()
{
    if (!navigationTonesAllowed() || hostProcessingActive_.load(std::memory_order_acquire))
        stoppedTransportTone_.cancel();
    // Workflow first: a command held during Correct may start a recapture and
    // should win before we schedule any now-obsolete post-profile rebuild.
    consumeWorkflowParameterChange();
    consumeDeferredWorkflowCommand();
    consumeMatchModeChange();
    consumeCorrectionSettingChanges();

    if (!captureActive() && !analysisBusy_.load(std::memory_order_acquire)
        && captureSampleRate_.load(std::memory_order_acquire) > 0)
        ensureCaptureCapacityForMode(currentMatchMode());

    if (pendingReferenceStartAfterCapacity_.load(std::memory_order_acquire)
        && captureBuffersReadyForMode(currentMatchMode()))
    {
        pendingReferenceStartAfterCapacity_.store(false, std::memory_order_release);
        handleWorkflowCommand(1);
    }

    // This is a non-blocking control-thread cleanup; it returns immediately if
    // the analysis worker currently owns the released core.
    correctionController_.collectRetiredKernels();
    publishTelemetryParameters();
}

void ToneTraceProcessor::consumeMatchModeChange()
{
    const int requested = matchModeIndex();
    if (captureActive())
    {
        const int locked = lockedMatchModeIndex_.load(std::memory_order_acquire);
        if (requested != locked)
        {
            setupLockedNotice_.store(true, std::memory_order_release);
            setStatus(tonetrace::capture::Status::SetupLocked);
            matchMode_->setValueNotifyingHost(
                matchMode_->convertTo0to1(static_cast<float>(locked)));
        }
        return;
    }

    lockedMatchModeIndex_.store(requested, std::memory_order_release);
    if (requested != lastObservedMatchModeIndex_.load(std::memory_order_acquire))
    {
        // Do not consume a setting change while Correct still owns the analysis
        // transaction. Leaving lastObserved untouched makes the next timer tick
        // rebuild the newly committed profile from retained spectra using the
        // user's latest setting.
        if (analysisBusy_.load(std::memory_order_acquire)) return;

        lastObservedMatchModeIndex_.store(requested, std::memory_order_release);
        if (hasProfile_.load(std::memory_order_acquire))
        {
            setStatus(tonetrace::capture::Status::Analyzing);
            queueAnalysisJob(AnalysisJobKind::RebuildProfile);
        }
    }
}

void ToneTraceProcessor::consumeWorkflowParameterChange()
{
    const int previous = lastWorkflowStep_.load(std::memory_order_acquire);
    const double requested = static_cast<double>(workflowStep_->get());
    const int step = tonetrace::capture::workflowStepFromHostValue(requested, previous);
    // Compare decoded commands, as the released CLAP wrapper does. JUCE's
    // float range round-trips step 3 as 3.00000023842; comparing that raw value
    // to integer 3 replays Correct on every timer tick, even after it succeeds.
    if (step == previous) return;

    setWorkflowStepFromEngine(step);
    handleWorkflowCommand(step);
}

void ToneTraceProcessor::consumeDeferredWorkflowCommand()
{
    if (analysisBusy_.load(std::memory_order_acquire)
        || stateRestoreBusy_.load(std::memory_order_acquire))
        return;

    const int command = deferredWorkflowCommand_.exchange(0, std::memory_order_acq_rel);
    if (command != 0) handleWorkflowCommand(command);
}

void ToneTraceProcessor::consumeCorrectionSettingChanges()
{
    const auto current = currentObservedCorrectionSettings();

    bool changed = false;
    for (std::size_t i = 0; i < current.size(); ++i)
        changed = changed || std::abs(current[i] - lastObservedCorrectionSettings_[i]) > 0.0005;
    if (!changed) return;

    // A setting edited during Correct remains pending and is rebuilt against the
    // newly committed profile rather than being marked observed prematurely.
    if (analysisBusy_.load(std::memory_order_acquire)) return;

    const int oldResolution = std::max(1, static_cast<int>(std::lround(lastObservedCorrectionSettings_[2])));
    const int newResolution = std::max(1, static_cast<int>(std::lround(current[2])));
    if (oldResolution != newResolution)
    {
        double low = 20.0, high = 20000.0;
        if (const auto snapshot = correctionController_.snapshotCopy(); snapshot != nullptr)
        {
            low = snapshot->uncappedModel.analysisLowHz;
            high = snapshot->uncappedModel.analysisHighHz;
        }
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        manualGains_ = tonetrace::resampleManualGains(
            manualGains_, static_cast<std::size_t>(newResolution), low, high);
    }

    lastObservedCorrectionSettings_ = current;
    queueAnalysisJob(AnalysisJobKind::RebuildProfile);
}

void ToneTraceProcessor::handleWorkflowCommand(int step)
{
    const auto phase = phase_.load(std::memory_order_acquire);
    const auto workflowForPhase = [phase]()
    {
        switch (phase)
        {
            case tonetrace::capture::WorkflowPhase::CapturingReference: return 1;
            case tonetrace::capture::WorkflowPhase::CapturingTarget: return 2;
            case tonetrace::capture::WorkflowPhase::Preview: return 3;
            case tonetrace::capture::WorkflowPhase::Frozen: return 4;
            case tonetrace::capture::WorkflowPhase::Ready:
            default: return 0;
        }
    };

    if (step <= 0 || step > 7)
    {
        setWorkflowStepFromEngine(workflowForPhase());
        return;
    }

    lastCommand_.store(step, std::memory_order_release);

    if (stateRestoreBusy_.load(std::memory_order_acquire))
    {
        setWorkflowStepFromEngine(workflowForPhase());
        return;
    }

    if (analysisBusy_.load(std::memory_order_acquire))
    {
        // Released 1.0.5 holds the latest workflow command while Correct is
        // analysing. Keep the host-visible step where the user put it and replay
        // the command through this same path after the analysis transaction ends.
        deferredWorkflowCommand_.store(step, std::memory_order_release);
        setStatus(tonetrace::capture::Status::Analyzing);
        return;
    }

    // A pending capture reset must reach the audio block boundary before a
    // later structural command can observe that buffer. This prevents rapid
    // Learn -> Correct from making stale Target audio reachable.
    if (pendingAudioCommand_.load(std::memory_order_acquire) != 0)
    {
        setWorkflowStepFromEngine(workflowForPhase());
        return;
    }

    if (step == 5)
    {
        phaseBeforeReset_.store(phase, std::memory_order_release);
        phase_.store(tonetrace::capture::WorkflowPhase::Ready, std::memory_order_release);
        resetArmed_.store(true, std::memory_order_release);
        setStatus(tonetrace::capture::Status::ResetArmed);
        return;
    }

    if (step == 6)
    {
        if (resetArmed_.exchange(false, std::memory_order_acq_rel))
        {
            cancelActiveTones();
            performConfirmedReset();
        }
        else
            setWorkflowStepFromEngine(workflowForPhase());
        return;
    }

    if (step == 7)
    {
        if (resetArmed_.exchange(false, std::memory_order_acq_rel))
        {
            const auto previous = phaseBeforeReset_.load(std::memory_order_acquire);
            phase_.store(previous, std::memory_order_release);
            setStatus(previous == tonetrace::capture::WorkflowPhase::Frozen
                ? tonetrace::capture::Status::Frozen
                : (hasProfile_.load(std::memory_order_acquire)
                    ? tonetrace::capture::Status::Preview
                    : tonetrace::capture::Status::Ready));
        }
        else
        {
            setWorkflowStepFromEngine(workflowForPhase());
        }
        return;
    }

    if (step == 3)
    {
        if (phase != tonetrace::capture::WorkflowPhase::CapturingTarget
            || !targetCorrectable_.load(std::memory_order_acquire))
        {
            captureBlocked_.store(true, std::memory_order_release);
            setWorkflowStepFromEngine(2);
            setStatus(tonetrace::capture::Status::InvalidCapture);
            requestWarningTone();
            return;
        }

        cancelActiveTones();
        captureBlocked_.store(false, std::memory_order_release);
        setupLockedNotice_.store(false, std::memory_order_release);
        // Changing phase before queueing work prevents any later audio block
        // from writing capture data. The worker additionally waits for any block
        // that already entered captureInputSlice() to leave.
        phase_.store(tonetrace::capture::WorkflowPhase::Preview,
                     std::memory_order_release);
        setStatus(tonetrace::capture::Status::Analyzing);
        analysisBusy_.store(true, std::memory_order_release);
        queueAnalysisJob(AnalysisJobKind::AnalyzeCaptured);
        return;
    }

    if (step == 4)
    {
        if (hasProfile_.load(std::memory_order_acquire))
        {
            cancelActiveTones();
            phase_.store(tonetrace::capture::WorkflowPhase::Frozen,
                         std::memory_order_release);
            setStatus(tonetrace::capture::Status::Frozen);
        }
        else
        {
            setWorkflowStepFromEngine(3);
            setStatus(tonetrace::capture::Status::InvalidCapture);
        }
        return;
    }

    const auto mode = currentMatchMode();
    if (!captureBuffersReadyForMode(mode))
    {
        if (captureActive())
        {
            setStatus(tonetrace::capture::Status::SetupLocked);
            setWorkflowStepFromEngine(workflowForPhase());
            return;
        }
        setStatus(tonetrace::capture::Status::PreparingCapture);
        pendingReferenceStartAfterCapacity_.store(step == 1, std::memory_order_release);
        ensureCaptureCapacityForMode(mode);
        if (step == 2 && captureBuffersReadyForMode(mode)) handleWorkflowCommand(2);
        return;
    }

    if (step == 1)
    {
        cancelActiveTones();
        invalidateAnalysisWork();
        {
            const std::lock_guard<std::mutex> lock(manualStateMutex_);
            importedReference_.reset();
            importedTarget_.reset();
        }
        activeCaptureMode_.store(static_cast<int>(mode), std::memory_order_release);
        lockedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
        lastObservedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
        referenceReady_.store(false, std::memory_order_release);
        targetCorrectable_.store(false, std::memory_order_release);
        captureBlocked_.store(false, std::memory_order_release);
        setupLockedNotice_.store(false, std::memory_order_release);
        resetPublishedCaptureStateForCommand(
            tonetrace::capture::WorkflowPhase::CapturingReference,
            tonetrace::capture::Status::CapturingReference);
        pendingAudioCommand_.store(1, std::memory_order_release);
        return;
    }

    if (!referenceReady_.load(std::memory_order_acquire))
    {
        const auto currentPhase = phase_.load(std::memory_order_acquire);
        activeCaptureMode_.store(static_cast<int>(mode), std::memory_order_release);
        lockedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
        lastObservedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
        setupLockedNotice_.store(false, std::memory_order_release);

        if (currentPhase == tonetrace::capture::WorkflowPhase::CapturingReference)
        {
            // Premature Learn while Reference is actively accumulating must not
            // erase useful partial audio.
            captureBlocked_.store(true, std::memory_order_release);
            setWorkflowStepFromEngine(1);
            setStatus(tonetrace::capture::Status::CannotSaveYet);
            requestWarningTone();
            return;
        }

        // Direct Learn from Ready/Frozen state without live raw Reference data
        // falls back to a fresh Reference capture. Project state stores spectra,
        // never raw capture samples.
        invalidateAnalysisWork();
        referenceReady_.store(false, std::memory_order_release);
        targetCorrectable_.store(false, std::memory_order_release);
        captureBlocked_.store(true, std::memory_order_release);
        resetPublishedCaptureStateForCommand(
            tonetrace::capture::WorkflowPhase::CapturingReference,
            tonetrace::capture::Status::CannotSaveYet);
        setWorkflowStepFromEngine(1);
        pendingAudioCommand_.store(1, std::memory_order_release);
        requestWarningTone();
        return;
    }

    cancelActiveTones();
    invalidateAnalysisWork();
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        // Learn Target is a Target recapture operation. Preserve an imported
        // Reference, but never let an older imported Target override the new
        // live Target when Correct is pressed.
        importedTarget_.reset();
    }
    activeCaptureMode_.store(static_cast<int>(mode), std::memory_order_release);
    lockedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
    lastObservedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
    targetCorrectable_.store(false, std::memory_order_release);
    captureBlocked_.store(false, std::memory_order_release);
    setupLockedNotice_.store(false, std::memory_order_release);
    resetPublishedCaptureStateForCommand(
        tonetrace::capture::WorkflowPhase::CapturingTarget,
        tonetrace::capture::Status::CapturingTarget);
    pendingAudioCommand_.store(2, std::memory_order_release);
}

void ToneTraceProcessor::performConfirmedReset()
{
    invalidateAnalysisWork();
    deferredWorkflowCommand_.store(0, std::memory_order_release);
    pendingReferenceStartAfterCapacity_.store(false, std::memory_order_release);

    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        std::fill(manualGains_.begin(), manualGains_.end(), 0.0);
        importedReference_.reset();
        importedTarget_.reset();
    }

    auto& gain = *continuousParameters_[tonetrace::product::kCorrectionGainIndex];
    gain.beginChangeGesture();
    gain.setValueNotifyingHost(gain.convertTo0to1(0.0F));
    gain.endChangeGesture();

    {
        const std::lock_guard<std::mutex> lock(pendingStateMutex_);
        pendingCoreState_.clear();
        pendingCoreStateFrozen_ = false;
        pendingCoreNeedsRebuild_ = false;
        pendingCoreConfidence_ = 0.0;
    }

    const auto result = correctionController_.reset();
    if (!result.accepted)
    {
        setStatus(result.issue == tonetrace::ProfileIssue::RendererBusy
            ? tonetrace::capture::Status::RendererBusy
            : tonetrace::capture::Status::AnalysisFailed);
        if (result.issue != tonetrace::ProfileIssue::RendererBusy) requestWarningTone();
        setWorkflowStepFromEngine(0);
        return;
    }

    hasProfile_.store(false, std::memory_order_release);
    tailSeconds_.store(0.0, std::memory_order_release);
    referenceReady_.store(false, std::memory_order_release);
    targetCorrectable_.store(false, std::memory_order_release);
    captureBlocked_.store(false, std::memory_order_release);
    setupLockedNotice_.store(false, std::memory_order_release);
    pendingAudioCommand_.store(1, std::memory_order_release);
    resetPublishedCaptureStateForCommand(
        tonetrace::capture::WorkflowPhase::Ready,
        tonetrace::capture::Status::Ready);
    setWorkflowStepFromEngine(0);
}

void ToneTraceProcessor::resetPublishedCaptureStateForCommand(
    tonetrace::capture::WorkflowPhase newPhase,
    tonetrace::capture::Status newStatus) noexcept
{
    phase_.store(newPhase, std::memory_order_release);
    captureSeconds_.store(0.0, std::memory_order_release);
    confidence_.store(0.0, std::memory_order_release);
    curveDriftDb_.store(60.0, std::memory_order_release);
    setStatus(newStatus);
}

void ToneTraceProcessor::resetWorkflowToReadyForStateLoad()
{
    phase_.store(tonetrace::capture::WorkflowPhase::Ready, std::memory_order_release);
    pendingAudioCommand_.store(0, std::memory_order_release);
    deferredWorkflowCommand_.store(0, std::memory_order_release);
    pendingReferenceStartAfterCapacity_.store(false, std::memory_order_release);
    resetArmed_.store(false, std::memory_order_release);
    referenceReady_.store(false, std::memory_order_release);
    targetCorrectable_.store(false, std::memory_order_release);
    captureBlocked_.store(false, std::memory_order_release);
    setupLockedNotice_.store(false, std::memory_order_release);
    lastCommand_.store(0, std::memory_order_release);
    captureSeconds_.store(0.0, std::memory_order_release);
    confidence_.store(0.0, std::memory_order_release);
    curveDriftDb_.store(60.0, std::memory_order_release);
    setStatus(tonetrace::capture::Status::Ready);
    setWorkflowStepFromEngine(0);
    lockedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
    activeCaptureMode_.store(matchModeIndex(), std::memory_order_release);
    synchronizeObservedControlState();
}

void ToneTraceProcessor::applyRestoredFrozenWorkflow(double restoredConfidence)
{
    phase_.store(tonetrace::capture::WorkflowPhase::Frozen, std::memory_order_release);
    pendingAudioCommand_.store(0, std::memory_order_release);
    deferredWorkflowCommand_.store(0, std::memory_order_release);
    pendingReferenceStartAfterCapacity_.store(false, std::memory_order_release);
    referenceReady_.store(false, std::memory_order_release);
    targetCorrectable_.store(false, std::memory_order_release);
    captureBlocked_.store(false, std::memory_order_release);
    setupLockedNotice_.store(false, std::memory_order_release);
    lastCommand_.store(0, std::memory_order_release);
    captureSeconds_.store(0.0, std::memory_order_release);
    confidence_.store(std::clamp(restoredConfidence, 0.0, 1.0), std::memory_order_release);
    curveDriftDb_.store(0.0, std::memory_order_release);
    setStatus(tonetrace::capture::Status::Frozen);
    // Released project-state restore starts Workflow Step at No action even
    // though the restored processing phase is Frozen.
    setWorkflowStepFromEngine(0);
    lockedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
    activeCaptureMode_.store(matchModeIndex(), std::memory_order_release);
    synchronizeObservedControlState();
}

void ToneTraceProcessor::setStatus(tonetrace::capture::Status value) noexcept
{
    status_.store(static_cast<int>(value), std::memory_order_release);
}

void ToneTraceProcessor::publishTelemetryParameters()
{
    publishParameter(*statusParameter_, static_cast<double>(statusCode()));
    publishParameter(*lastCommandParameter_, static_cast<double>(lastCommandCode()));
    publishParameter(*confidenceParameter_, captureConfidence());
    publishParameter(*curveDriftParameter_, curveDriftDb());
    publishParameter(*captureTimeParameter_, captureSeconds());
}

void ToneTraceProcessor::publishParameter(juce::AudioParameterFloat& parameter,
                                          double actualValue)
{
    const auto target = static_cast<float>(actualValue);
    if (std::abs(static_cast<double>(parameter.get()) - static_cast<double>(target)) < 0.0005)
        return;
    parameter.setValueNotifyingHost(parameter.convertTo0to1(target));
}

void ToneTraceProcessor::setWorkflowStepFromEngine(int step)
{
    const auto bounded = std::clamp(step, 0, 7);
    lastWorkflowStep_.store(bounded, std::memory_order_release);
    const auto target = static_cast<float>(bounded);
    if (std::abs(static_cast<double>(workflowStep_->get()) - bounded) > 1.0e-7)
        workflowStep_->setValueNotifyingHost(workflowStep_->convertTo0to1(target));
}

void ToneTraceProcessor::ensureCaptureCapacityForMode(tonetrace::capture::MatchMode mode)
{
    if (captureActive() || analysisBusy_.load(std::memory_order_acquire)) return;
    const int sampleRate = captureSampleRate_.load(std::memory_order_acquire);
    if (sampleRate <= 0) return;

    const std::lock_guard<std::mutex> lock(captureControlMutex_);
    // Recheck after acquiring the control-plane lock. The message thread owns
    // workflow phase transitions, so this is defensive against host lifecycle
    // work rather than an audio-thread lock.
    if (captureActive()) return;
    const auto seconds = tonetrace::capture::maximumCaptureSeconds(mode);
    referenceCapture_.ensureCapacity(sampleRate, seconds);
    targetCapture_.ensureCapacity(sampleRate, seconds);
    captureCapacityFrames_.store(
        std::min(referenceCapture_.capacityFrames, targetCapture_.capacityFrames),
        std::memory_order_release);
}

bool ToneTraceProcessor::captureBuffersReadyForMode(tonetrace::capture::MatchMode mode) const noexcept
{
    const int sampleRate = captureSampleRate_.load(std::memory_order_acquire);
    if (sampleRate <= 0) return false;
    const auto required = static_cast<std::size_t>(std::ceil(
        static_cast<double>(sampleRate) * tonetrace::capture::maximumCaptureSeconds(mode)));
    return captureCapacityFrames_.load(std::memory_order_acquire) >= required;
}

tonetrace::capture::MatchMode ToneTraceProcessor::currentMatchMode() const noexcept
{
    return tonetrace::capture::matchModeFromIndex(matchModeIndex());
}

tonetrace::jucebridge::CorrectionSettings ToneTraceProcessor::currentCorrectionSettings() const noexcept
{
    tonetrace::jucebridge::CorrectionSettings settings;
    settings.mode = releasedMatchMode(currentMatchMode());
    settings.resolution = static_cast<int>(std::lround(
        continuousValue(tonetrace::product::kCorrectionResolutionIndex)));
    settings.maximumCorrectionDb = continuousValue(tonetrace::product::kMaximumCorrectionIndex);
    settings.fullCorrectionRange = toggleValue(tonetrace::product::kFullCorrectionRangeIndex);
    settings.correctionStrength = continuousValue(tonetrace::product::kCorrectionStrengthIndex);
    settings.correctionSharpness = continuousValue(tonetrace::product::kCorrectionSharpnessIndex);
    settings.rangeLowHz = continuousValue(tonetrace::product::kCorrectionRangeLowIndex);
    settings.rangeHighHz = continuousValue(tonetrace::product::kCorrectionRangeHighIndex);
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        settings.manualGains = manualGains_;
    }
    return settings;
}

std::pair<double, double> ToneTraceProcessor::currentTransferRangeHz(
    double fallbackLowHz, double fallbackHighHz) const noexcept
{
    const double low = continuousValue(tonetrace::product::kCorrectionRangeLowIndex);
    const double high = continuousValue(tonetrace::product::kCorrectionRangeHighIndex);
    if (std::isfinite(low) && std::isfinite(high) && low > 0.0 && high > low)
        return { low, high };
    return { fallbackLowHz, fallbackHighHz };
}

std::array<double, 7> ToneTraceProcessor::currentObservedCorrectionSettings() const noexcept
{
    return {{
        continuousValue(tonetrace::product::kMaximumCorrectionIndex),
        continuousValue(tonetrace::product::kCorrectionStrengthIndex),
        continuousValue(tonetrace::product::kCorrectionResolutionIndex),
        continuousValue(tonetrace::product::kCorrectionRangeLowIndex),
        continuousValue(tonetrace::product::kCorrectionRangeHighIndex),
        continuousValue(tonetrace::product::kCorrectionSharpnessIndex),
        toggleValue(tonetrace::product::kFullCorrectionRangeIndex) ? 1.0 : 0.0
    }};
}

void ToneTraceProcessor::synchronizeObservedControlState() noexcept
{
    lastObservedMatchModeIndex_.store(matchModeIndex(), std::memory_order_release);
    lastObservedCorrectionSettings_ = currentObservedCorrectionSettings();
}

tonetrace::MatchMode ToneTraceProcessor::releasedMatchMode(
    tonetrace::capture::MatchMode mode) noexcept
{
    switch (mode)
    {
        case tonetrace::capture::MatchMode::Voice: return tonetrace::MatchMode::Voice;
        case tonetrace::capture::MatchMode::Drums: return tonetrace::MatchMode::Drums;
        case tonetrace::capture::MatchMode::BassSynth: return tonetrace::MatchMode::BassSynth;
        case tonetrace::capture::MatchMode::CustomMaxCapture: return tonetrace::MatchMode::CustomMaxCapture;
        case tonetrace::capture::MatchMode::FullMix:
        default: return tonetrace::MatchMode::FullMix;
    }
}

tonetrace::CaptureDiagnostics ToneTraceProcessor::releasedDiagnostics(
    const tonetrace::capture::CaptureDiagnostics& diagnostics) noexcept
{
    tonetrace::CaptureDiagnostics result;
    result.sampleCount = diagnostics.sampleCount;
    result.nonFiniteSamples = diagnostics.nonFiniteSamples;
    result.clippedSamples = diagnostics.clippedSamples;
    result.peakAbsolute = diagnostics.peakAbsolute;
    return result;
}

tonetrace::AudioBuffer ToneTraceProcessor::copyCaptureAudio(
    const tonetrace::capture::CaptureBuffer& capture, int sampleRate)
{
    if (sampleRate <= 0 || capture.frames == 0 || capture.channelCount == 0
        || capture.channelCount > capture.channels.size())
        throw std::runtime_error("Capture contains no usable audio");

    tonetrace::AudioBuffer audio;
    audio.sampleRate = sampleRate;
    audio.channels.assign(capture.channelCount,
                          std::vector<double>(capture.frames, 0.0));
    for (std::size_t channel = 0; channel < capture.channelCount; ++channel)
    {
        if (capture.channels[channel].size() < capture.frames)
            throw std::runtime_error("Capture buffer is internally inconsistent");
        for (std::size_t frame = 0; frame < capture.frames; ++frame)
            audio.channels[channel][frame] = capture.channels[channel][frame];
    }
    return audio;
}

void ToneTraceProcessor::queueAnalysisJob(AnalysisJobKind kind)
{
    AnalysisJob job;
    job.kind = kind;
    job.epoch = analysisEpoch_.load(std::memory_order_acquire);
    job.settings = currentCorrectionSettings();

    {
        const std::lock_guard<std::mutex> lock(analysisMutex_);
        if (analysisThreadExit_) return;
        // A pending Analyze job is structural and may not be replaced by a
        // post-profile rebuild. Rebuilds, however, intentionally coalesce to
        // the latest parameter snapshot.
        if (pendingAnalysisJob_.has_value()
            && pendingAnalysisJob_->kind == AnalysisJobKind::AnalyzeCaptured
            && kind == AnalysisJobKind::RebuildProfile)
            return;
        pendingAnalysisJob_ = job;
    }
    analysisCv_.notify_one();
}

void ToneTraceProcessor::analysisThreadMain()
{
    for (;;)
    {
        AnalysisJob job;
        {
            std::unique_lock<std::mutex> lock(analysisMutex_);
            analysisCv_.wait(lock, [this]
            {
                return analysisThreadExit_ || pendingAnalysisJob_.has_value();
            });
            if (analysisThreadExit_) return;
            job = *pendingAnalysisJob_;
            pendingAnalysisJob_.reset();
        }

        if (job.kind == AnalysisJobKind::AnalyzeCaptured)
            runAnalyzeJob(job);
        else
            runRebuildJob(job);
    }
}

void ToneTraceProcessor::runAnalyzeJob(const AnalysisJob& job)
{
    // Phase has already left capture. Wait only on this background thread for
    // a block that entered capture before that transition to finish.
    while (captureWriterCount_.load(std::memory_order_acquire) != 0)
    {
        if (!jobStillCurrent(job))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    tonetrace::AudioBuffer referenceAudio;
    tonetrace::AudioBuffer targetAudio;
    tonetrace::CaptureDiagnostics referenceDiagnostics;
    tonetrace::CaptureDiagnostics targetDiagnostics;
    std::optional<tonetrace::SpectrumCapture> importedReference;
    std::optional<tonetrace::SpectrumCapture> importedTarget;
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        importedReference = importedReference_;
        importedTarget = importedTarget_;
    }
    try
    {
        const std::lock_guard<std::mutex> lock(captureControlMutex_);
        if (!jobStillCurrent(job))
            return;
        const int sampleRate = captureSampleRate_.load(std::memory_order_acquire);
        if (!importedReference.has_value())
            referenceAudio = copyCaptureAudio(referenceCapture_, sampleRate);
        if (!importedTarget.has_value())
            targetAudio = copyCaptureAudio(targetCapture_, sampleRate);
        if (!importedReference.has_value())
            referenceDiagnostics = releasedDiagnostics(referenceCapture_.profileDiagnostics());
        if (!importedTarget.has_value())
            targetDiagnostics = releasedDiagnostics(targetCapture_.profileDiagnostics());
    }
    catch (const std::exception& error)
    {
        finishAnalyzeJob(job, { false, tonetrace::ProfileIssue::InvalidCapture,
                                error.what(), 0.0 });
        return;
    }

    if (!jobStillCurrent(job))
        return;

    const auto result = correctionController_.analyze(
        referenceAudio, targetAudio, referenceDiagnostics, targetDiagnostics,
        job.settings, importedReference.has_value() ? &*importedReference : nullptr,
        importedTarget.has_value() ? &*importedTarget : nullptr);
    finishAnalyzeJob(job, result);
}

void ToneTraceProcessor::runRebuildJob(const AnalysisJob& job)
{
    if (!jobStillCurrent(job)) return;
    const bool learnedProfile = correctionController_.hasProfile();
    const auto result = learnedProfile
        ? correctionController_.rebuild(job.settings)
        : correctionController_.commitManualCorrection(job.settings);
    finishRebuildJob(job, result);
}

void ToneTraceProcessor::finishAnalyzeJob(
    const AnalysisJob& job,
    const tonetrace::jucebridge::CorrectionResult& result)
{
    if (!jobStillCurrent(job))
        return;

    if (result.accepted)
    {
        hasProfile_.store(true, std::memory_order_release);
        if (const auto snapshot = correctionController_.snapshotCopy(); snapshot != nullptr)
            tailSeconds_.store(snapshot->renderSettings.durationSeconds, std::memory_order_release);
        confidence_.store(std::clamp(result.confidence, 0.0, 1.0), std::memory_order_release);
        curveDriftDb_.store(0.0, std::memory_order_release);
        phase_.store(tonetrace::capture::WorkflowPhase::Preview, std::memory_order_release);
        setStatus(tonetrace::capture::Status::Preview);
    }
    else
    {
        hasProfile_.store(correctionController_.hasProfile(), std::memory_order_release);
        setStatus(result.issue == tonetrace::ProfileIssue::RendererBusy
            ? tonetrace::capture::Status::RendererBusy
            : tonetrace::capture::Status::InvalidCapture);
        if (result.issue != tonetrace::ProfileIssue::RendererBusy) requestWarningTone();
    }
    analysisBusy_.store(false, std::memory_order_release);
}

void ToneTraceProcessor::finishRebuildJob(
    const AnalysisJob& job,
    const tonetrace::jucebridge::CorrectionResult& result)
{
    if (!jobStillCurrent(job)) return;
    if (result.accepted)
    {
        const bool learnedProfile = correctionController_.hasProfile();
        hasProfile_.store(learnedProfile, std::memory_order_release);
        if (const auto snapshot = correctionController_.snapshotCopy(); snapshot != nullptr)
            tailSeconds_.store(snapshot->renderSettings.durationSeconds, std::memory_order_release);
        else
            tailSeconds_.store(0.0, std::memory_order_release);
        if (learnedProfile)
        {
            const auto currentPhase = phase_.load(std::memory_order_acquire);
            setStatus(currentPhase == tonetrace::capture::WorkflowPhase::Frozen
                ? tonetrace::capture::Status::Frozen
                : tonetrace::capture::Status::Preview);
        }
    }
    else
    {
        setStatus(result.issue == tonetrace::ProfileIssue::RendererBusy
            ? tonetrace::capture::Status::RendererBusy
            : tonetrace::capture::Status::AnalysisFailed);
        if (result.issue != tonetrace::ProfileIssue::RendererBusy) requestWarningTone();
    }
}

bool ToneTraceProcessor::jobStillCurrent(const AnalysisJob& job) const noexcept
{
    return job.epoch == analysisEpoch_.load(std::memory_order_acquire);
}

void ToneTraceProcessor::invalidateAnalysisWork() noexcept
{
    analysisEpoch_.fetch_add(1, std::memory_order_acq_rel);
    analysisBusy_.store(false, std::memory_order_release);
    const std::lock_guard<std::mutex> lock(analysisMutex_);
    pendingAnalysisJob_.reset();
}

void ToneTraceProcessor::stopAnalysisThread()
{
    analysisEpoch_.fetch_add(1, std::memory_order_acq_rel);
    {
        const std::lock_guard<std::mutex> lock(analysisMutex_);
        analysisThreadExit_ = true;
        pendingAnalysisJob_.reset();
    }
    analysisCv_.notify_all();
    if (analysisThread_.joinable()) analysisThread_.join();
}

juce::AudioProcessorEditor* ToneTraceProcessor::createEditor()
{
    return new ToneTraceEditor(*this);
}

void ToneTraceProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("ToneTraceJuce11State");
    state.setProperty("schema", kStateSchemaVersion, nullptr);
    for (const auto& record : stateParameters_)
        state.setProperty(record.key, static_cast<double>(record.parameter->getValue()), nullptr);
    const auto [manualLow, manualHigh] = bandFrequencyRangeHz();
    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        state.setProperty("manualGains", serializeManualGains(tonetrace::resampleManualGains(
            manualGains_, static_cast<std::size_t>(bandCount()), manualLow, manualHigh)), nullptr);
    }

    // If state arrived before prepareToPlay, preserve that exact validated core
    // snapshot. Otherwise the controller's cache returns the last validated
    // profile immediately, even if a new analysis is currently in progress.
    std::string coreState;
    {
        const std::lock_guard<std::mutex> lock(pendingStateMutex_);
        coreState = pendingCoreState_;
    }
    if (coreState.empty()) coreState = correctionController_.saveProjectState();
    state.setProperty("coreState",
        juce::String::fromUTF8(coreState.data(), static_cast<int>(coreState.size())), nullptr);

    if (const auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void ToneTraceProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    (void) restoreState(data, sizeInBytes);
}

bool ToneTraceProcessor::restoreState(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > 32 * 1024 * 1024) return false;
    const std::string_view bytes(static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes));
    if (bytes.starts_with("ToneTraceClapState "))
    {
        std::istringstream stream { std::string(bytes) };
        std::string token;
        int version = 0;
        std::size_t count = 0;
        stream >> token >> version >> token >> count;
        if (!stream || version < 1 || version > 3 || token != "parameters"
            || count > stateParameters_.size()) return false;
        juce::ValueTree migrated("ToneTraceJuce11State");
        migrated.setProperty("schema", kStateSchemaVersion, nullptr);
        for (const auto& record : stateParameters_)
            migrated.setProperty(record.key, static_cast<double>(record.parameter->getDefaultValue()), nullptr);
        for (std::size_t i = 0; i < count; ++i)
        {
            unsigned int id = 0;
            double value = 0.0;
            stream >> token >> id >> value;
            const auto* spec = tonetrace::product::findReleasedParameterById(id);
            if (!stream || token != "parameter" || spec == nullptr || spec->readOnly
                || spec->kind == tonetrace::product::ParameterKind::Workflow || !std::isfinite(value)) return false;
            const auto found = std::find_if(stateParameters_.begin(), stateParameters_.end(),
                [spec](const auto& record) { return record.key.toString() == spec->key; });
            if (found == stateParameters_.end()) return false;
            const auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(found->parameter);
            if (ranged == nullptr) return false;
            migrated.setProperty(found->key, static_cast<double>(ranged->convertTo0to1(
                static_cast<float>(std::clamp(value, spec->minimum, spec->maximum)))), nullptr);
        }
        if (version >= 2)
        {
            int display = 0;
            stream >> token >> display;
            if (!stream || token != "tracedisplay" || (display != 0 && display != 1)) return false;
        }
        std::vector<double> gains;
        if (version >= 3)
        {
            stream >> token >> count;
            if (!stream || token != "manualgains" || count > 120) return false;
            for (std::size_t i = 0; i < count; ++i)
            {
                double value = 0.0;
                stream >> token >> value;
                if (!stream || token != "manualgain" || !std::isfinite(value) || std::abs(value) > 120.0) return false;
                gains.push_back(value);
            }
        }
        std::size_t coreSize = 0;
        stream >> token >> coreSize;
        if (!stream || token != "core" || coreSize > bytes.size()) return false;
        if (stream.peek() == '\r') stream.get();
        if (stream.peek() == '\n') stream.get();
        std::string core(coreSize, '\0');
        stream.read(core.data(), static_cast<std::streamsize>(coreSize));
        if (static_cast<std::size_t>(stream.gcount()) != coreSize) return false;
        stream >> std::ws;
        if (!stream.eof()) return false;
        try
        {
            auto parsed = tonetrace::deserializeProjectState(core);
            if (parsed.snapshot != nullptr)
            {
                // The port applies Correction Gain once, in the JUCE output stage.
                parsed.snapshot->renderSettings.correctionGainDb = 0.0;
                core = tonetrace::serializeProjectState(parsed.snapshot.get());
            }
        }
        catch (const std::exception&) { return false; }
        migrated.setProperty("manualGains", serializeManualGains(gains), nullptr);
        migrated.setProperty("coreState", juce::String::fromUTF8(core.data(), static_cast<int>(core.size())), nullptr);
        juce::MemoryBlock block;
        copyXmlToBinary(*migrated.createXml(), block);
        return restoreState(block.getData(), static_cast<int>(block.getSize()));
    }
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName("ToneTraceJuce11State")) return false;

    const auto state = juce::ValueTree::fromXml(*xml);
    const int schema = static_cast<int>(state.getProperty("schema", 0));
    if (!state.isValid() || (schema != 3 && schema != kStateSchemaVersion)) return false;

    // Schema 3 is the last known-good correction build. Its existing named
    // parameters are restored exactly; newly recovered controls take their
    // released defaults. Schema 4 requires the complete current parameter set.
    std::vector<float> restored;
    restored.reserve(stateParameters_.size());
    for (const auto& record : stateParameters_)
    {
        if (!state.hasProperty(record.key))
        {
            if (schema != 3) return false;
            restored.push_back(record.parameter->getDefaultValue());
            continue;
        }
        const auto normalised = static_cast<double>(state.getProperty(record.key));
        if (!std::isfinite(normalised) || normalised < 0.0 || normalised > 1.0) return false;
        restored.push_back(static_cast<float>(normalised));
    }
    std::vector<double> restoredManualGains;
    if (schema == kStateSchemaVersion)
    {
        if (!state.hasProperty("manualGains")
            || !parseManualGains(state.getProperty("manualGains").toString(), restoredManualGains))
            return false;
    }
    if (!state.hasProperty("coreState")) return false;
    const auto coreText = state.getProperty("coreState").toString();
    const auto coreState = coreText.toStdString();
    if (coreState.empty()) return false;

    // Parse/validate the whole released project snapshot before touching the live
    // renderer or JUCE parameters. A malformed state therefore changes nothing.
    tonetrace::RestoredProjectState parsedCore;
    try
    {
        parsedCore = tonetrace::deserializeProjectState(coreState);
    }
    catch (const std::exception&)
    {
        return false;
    }

    const bool frozen = parsedCore.phase == tonetrace::WorkflowPhase::Frozen
        && parsedCore.snapshot != nullptr;
    if (schema == 3 && frozen)
        restoredManualGains = parsedCore.snapshot->renderSettings.manualGains;

    // REAPER may round-trip our own persisted VST3 state during an ordinary
    // transport stop/restart. That state intentionally contains no raw capture.
    // If it is semantically identical to the state we already own, treating the
    // echo as an explicit restore would erase an in-progress Reference/Target.
    // Released 1.0.5 preserves that capture across the same lifecycle.
    bool parametersEquivalent = restored.size() == stateParameters_.size();
    for (std::size_t index = 0; parametersEquivalent && index < restored.size(); ++index)
    {
        parametersEquivalent = std::abs(
            static_cast<double>(restored[index])
            - static_cast<double>(stateParameters_[index].parameter->getValue())) <= 1.0e-6;
    }

    std::string currentCoreState;
    {
        const std::lock_guard<std::mutex> lock(pendingStateMutex_);
        currentCoreState = pendingCoreState_;
    }
    if (currentCoreState.empty()) currentCoreState = correctionController_.saveProjectState();

    if (tonetrace::capture::shouldPreserveCaptureForEquivalentStateEcho(
            phase_.load(std::memory_order_acquire), parametersEquivalent,
            coreState == currentCoreState))
    {
        return true;
    }
    const double restoredConfidence = frozen
        ? std::min(parsedCore.snapshot->reference.confidence,
                   parsedCore.snapshot->target.confidence)
        : 0.0;

    // The outer JUCE controls can legitimately be newer than the controller's
    // cached last-known-good profile if a host saved during a coalesced rebuild.
    // Preserve that state and rebuild from retained spectra after restore rather
    // than rejecting it as inconsistent.
    const auto restoredActual = [this, &restored](std::size_t index)
    {
        return static_cast<double>(continuousParameters_[index]->convertFrom0to1(restored[index]));
    };
    constexpr std::size_t matchModeStateIndex = tonetrace::product::kImplementedContinuousControls.size();
    const int restoredModeIndex = static_cast<int>(std::lround(
        matchMode_->convertFrom0to1(restored[matchModeStateIndex])));
    constexpr std::size_t firstToggleStateIndex = matchModeStateIndex + 1;
    const bool restoredFullRange = toggleParameters_[tonetrace::product::kFullCorrectionRangeIndex]
        ->convertFrom0to1(restored[firstToggleStateIndex + tonetrace::product::kFullCorrectionRangeIndex]) >= 0.5F;
    bool needsRebuild = false;
    if (frozen)
    {
        const auto& snap = *parsedCore.snapshot;
        const auto outerMode = releasedMatchMode(tonetrace::capture::matchModeFromIndex(restoredModeIndex));
        const double restoredMaximum = restoredFullRange ? 60.0
            : restoredActual(tonetrace::product::kMaximumCorrectionIndex);
        needsRebuild = snap.matchSettings.mode != outerMode
            || snap.matchSettings.resolution != static_cast<int>(std::lround(restoredActual(tonetrace::product::kCorrectionResolutionIndex)))
            || std::abs(snap.matchSettings.maximumCorrectionDb - restoredMaximum) > 0.0005
            || std::abs(snap.renderSettings.correctionStrength - restoredActual(tonetrace::product::kCorrectionStrengthIndex)) > 0.0005
            || std::abs(snap.renderSettings.correctionSharpness - restoredActual(tonetrace::product::kCorrectionSharpnessIndex)) > 0.0005
            || std::abs(snap.renderSettings.rangeLowHz - restoredActual(tonetrace::product::kCorrectionRangeLowIndex)) > 0.0005
            || std::abs(snap.renderSettings.rangeHighHz - restoredActual(tonetrace::product::kCorrectionRangeHighIndex)) > 0.0005
            || snap.renderSettings.manualGains != restoredManualGains;
    }

    stateRestoreBusy_.store(true, std::memory_order_release);
    invalidateAnalysisWork();

    bool coreAccepted = true;
    const bool corePrepared = correctionController_.sampleRate() > 0;
    if (corePrepared)
    {
        const auto result = correctionController_.restoreProjectState(coreState);
        coreAccepted = result.accepted;
        if (!coreAccepted)
        {
            setStatus(result.issue == tonetrace::ProfileIssue::RendererBusy
                ? tonetrace::capture::Status::RendererBusy
                : tonetrace::capture::Status::AnalysisFailed);
            stateRestoreBusy_.store(false, std::memory_order_release);
            return false;
        }
        const std::lock_guard<std::mutex> lock(pendingStateMutex_);
        pendingCoreState_.clear();
        pendingCoreStateFrozen_ = false;
        pendingCoreNeedsRebuild_ = false;
        pendingCoreConfidence_ = 0.0;
    }
    else
    {
        const std::lock_guard<std::mutex> lock(pendingStateMutex_);
        pendingCoreState_ = coreState;
        pendingCoreStateFrozen_ = frozen;
        pendingCoreNeedsRebuild_ = needsRebuild;
        pendingCoreConfidence_ = restoredConfidence;
    }

    if (!coreAccepted)
    {
        stateRestoreBusy_.store(false, std::memory_order_release);
        return false;
    }

    // No operation below this point can fail: the core state and every
    // normalized parameter were validated above, so applying them is atomic at
    // the product-state level.
    for (std::size_t index = 0; index < stateParameters_.size(); ++index)
        stateParameters_[index].parameter->setValueNotifyingHost(restored[index]);

    {
        const std::lock_guard<std::mutex> lock(manualStateMutex_);
        if (frozen)
        {
            manualGains_ = tonetrace::resampleManualGains(
                restoredManualGains, static_cast<std::size_t>(bandCount()),
                parsedCore.snapshot->uncappedModel.analysisLowHz,
                parsedCore.snapshot->uncappedModel.analysisHighHz);
        }
        else
        {
            manualGains_ = tonetrace::resampleManualGains(
                restoredManualGains, static_cast<std::size_t>(bandCount()), 20.0, 20000.0);
        }
        // Project-state restore owns a frozen processing snapshot, not a live
        // transfer session. Released 1.0.5 deliberately clears imported
        // Reference/Target eligibility here; the saved spectra remain available
        // through the frozen snapshot for export, but a later Target import must
        // first establish a Reference in the current workflow.
        importedReference_.reset();
        importedTarget_.reset();
    }

    hasProfile_.store(frozen, std::memory_order_release);
    tailSeconds_.store(frozen ? parsedCore.snapshot->renderSettings.durationSeconds : 0.0,
                       std::memory_order_release);

    if (frozen)
    {
        applyRestoredFrozenWorkflow(restoredConfidence);
        if (needsRebuild && corePrepared)
        {
            setStatus(tonetrace::capture::Status::SetupChanged);
            queueAnalysisJob(AnalysisJobKind::RebuildProfile);
        }
    }
    else
    {
        resetWorkflowToReadyForStateLoad();
        const auto manual = manualGainsCopy();
        if (corePrepared && std::any_of(manual.begin(), manual.end(),
                [](double value) { return std::abs(value) > 1.0e-9; }))
            queueAnalysisJob(AnalysisJobKind::RebuildProfile);
    }

    synchronizeObservedControlState();
    stateRestoreBusy_.store(false, std::memory_order_release);
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ToneTraceProcessor();
}

bool toneTraceRestoreClapState(juce::AudioProcessor& processor, const void* data, int size)
{
    return static_cast<ToneTraceProcessor&>(processor).restoreState(data, size);
}
