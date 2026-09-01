from pathlib import Path
import re

ROOT = Path('.')

def replace_once(path, old, new, label):
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{path}: expected one {label}, found {count}')
    p.write_text(text.replace(old, new), encoding='utf-8')


def regex_once(path, pattern, replacement, label, flags=re.S):
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    new_text, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f'{path}: expected one {label}, found {count}')
    p.write_text(new_text, encoding='utf-8')

replace_once(
    'include/tonetrace/tonetrace_realtime.h',
    '  EmergencyClipGuardDb = 270,\n};',
    '''  EmergencyClipGuardDb = 270,\n  // Workflow Step (100) is retired. Explicit momentary actions mirror the\n  // four native workflow buttons, while Workflow Stage reports the actual\n  // state reached by the engine. New ids prevent old automation from being\n  // reinterpreted as a destructive command.\n  WorkflowStage = 280,\n  CaptureReferenceCommand = 281,\n  LearnTargetCommand = 282,\n  CorrectTargetCommand = 283,\n  FreezeCorrectionCommand = 284,\n};''',
    'new workflow parameter ids')

regex_once(
    'include/tonetrace/tonetrace_realtime.h',
    r'// A normal live Target needs at least low confidence\..*?\n\}\n\nenum class ProfileIssue',
    '''// Reference and Target use one live-capture readiness rule. Imported\n// captures are already committed material; live captures must reach Low\n// confidence, with Capture Full as the explicit escape hatch.\n[[nodiscard]] constexpr bool captureCanAdvance(WorkflowPhase phase,\n                                                WorkflowPhase expectedPhase,\n                                                int confidenceLevel,\n                                                bool captureFull,\n                                                bool importedCapture) noexcept {\n  return importedCapture ||\n         (phase == expectedPhase && (confidenceLevel >= 1 || captureFull));\n}\n\n[[nodiscard]] constexpr bool referenceCaptureCanAdvance(\n    WorkflowPhase phase, int confidenceLevel, bool captureFull,\n    bool importedReference) noexcept {\n  return captureCanAdvance(phase, WorkflowPhase::CapturingReference,\n                           confidenceLevel, captureFull, importedReference);\n}\n\n[[nodiscard]] constexpr bool targetCaptureCanCorrect(\n    WorkflowPhase phase, int confidenceLevel, bool captureFull,\n    bool importedTarget) noexcept {\n  return captureCanAdvance(phase, WorkflowPhase::CapturingTarget,\n                           confidenceLevel, captureFull, importedTarget);\n}\n\nenum class ProfileIssue''',
    'shared capture readiness helper')

regex_once(
    'src/tonetrace_realtime.cpp',
    r'// Every parameter must be automatable so that OSARA.*?\nconst std::vector<ParameterDescriptor>& parameterDescriptors\(\) \{.*?\n  return descriptors;\n\}',
    '''// Writable controls remain automatable so REAPER/OSARA exposes them.\n// Workflow commands are explicit momentary actions, not a stateful slider.\n// Only the authoritative Workflow Stage and concise Status are host-visible\n// read-only feedback. Fast capture telemetry remains available internally to\n// the native Status panel but does not compete with host control traffic.\nconst std::vector<ParameterDescriptor>& parameterDescriptors() {\n  static const std::vector<ParameterDescriptor> descriptors{\n      {ParameterId::CaptureReferenceCommand, "Capture Reference", 0.0, 1.0, 0.0, "", true, false, true},\n      {ParameterId::LearnTargetCommand, "Learn Target", 0.0, 1.0, 0.0, "", true, false, true},\n      {ParameterId::CorrectTargetCommand, "Correct Target", 0.0, 1.0, 0.0, "", true, false, true},\n      {ParameterId::FreezeCorrectionCommand, "Freeze Correction", 0.0, 1.0, 0.0, "", true, false, true},\n      {ParameterId::WorkflowStage, "Workflow Stage", 0.0, 4.0, 0.0, "", true, true, true},\n      {ParameterId::Status, "Status", 0.0, 31.0, 0.0, "", true, true, true},\n      {ParameterId::LastCommand, "Last Action", 0.0, 4.0, 0.0, "", false, true, true},\n      {ParameterId::MatchMode, "Match Mode", 0.0, 4.0, 1.0, "", true, false, true},\n      {ParameterId::MaximumCorrectionDb, "Maximum Correction", 1.0, 60.0, 18.0, "dB", true, false, false},\n      {ParameterId::CompleteMatch, "Full Correction Range", 0.0, 1.0, 0.0, "", true, false, true},\n      {ParameterId::CorrectionStrength, "Correction Strength", -1.0, 1.0, 1.0, "x", true, false, false},\n      {ParameterId::Resolution, "Correction Resolution", 1.0, 120.0, 30.0, "bands", true, false, true},\n      {ParameterId::RangeLowHz, "Correction Range Low", 10.0, 18000.0, 10.0, "Hz", true, false, false},\n      {ParameterId::RangeHighHz, "Correction Range High", 20.0, 24000.0, 20000.0, "Hz", true, false, false},\n      {ParameterId::CorrectionSharpness, "Correction Q / Sharpness", 0.25, 4.0, 1.0, "x", true, false, false},\n      {ParameterId::CorrectionGainDb, "Correction Gain", -24.0, 24.0, 0.0, "dB", true, false, false},\n      {ParameterId::EmergencyClipGuardDb, "Emergency Clip Guard", -60.0, 24.0, 6.0, "dB", true, false, false},\n      {ParameterId::ToneLevelDb, "Confidence Tone Volume", -60.0, 0.0, -12.0, "dB", true, false, false},\n      {ParameterId::Confidence, "Capture Confidence", 0.0, 1.0, 0.0, "", false, true, false},\n      {ParameterId::CurveDriftDb, "Curve Drift", 0.0, 60.0, 60.0, "dB", false, true, false},\n      {ParameterId::CaptureSeconds, "Capture Time", 0.0, 3600.0, 0.0, "s", false, true, false},\n      {ParameterId::ToneNotifications, "Tone Notifications", 0.0, 1.0, 1.0, "", true, false, true},\n      {ParameterId::Bypass, "Bypass", 0.0, 1.0, 0.0, "", true, false, true},\n  };\n  return descriptors;\n}''',
    'parameter descriptor inventory')

replace_once(
    'plugins/clap/tonetrace_clap.cpp',
    '  ImportedReference = 28,\n  ImportedTarget = 29,\n};',
    '''  ImportedReference = 28,\n  ImportedTarget = 29,\n  TargetNotReady = 30,\n  WrongWorkflowStage = 31,\n};''',
    'workflow rejection statuses')
replace_once(
    'plugins/clap/tonetrace_clap.cpp',
    '    case Status::ReferenceReady:\n      return "Reference capture ready";',
    '    case Status::ReferenceReady:\n      return "Capturing Reference; stable; waiting for Low confidence";',
    'reference stable status')
replace_once(
    'plugins/clap/tonetrace_clap.cpp',
    '    case Status::TargetReady:\n      return "Target capture ready";',
    '    case Status::TargetReady:\n      return "Learning Target; stable; waiting for Low confidence";',
    'target stable status')
replace_once(
    'plugins/clap/tonetrace_clap.cpp',
    '    case Status::CannotSaveYet:\n      return "Cannot save yet; keep capturing";\n    case Status::ImportedReference:',
    '''    case Status::CannotSaveYet:\n      return "Reference not ready; keep capturing until confidence reaches Low";\n    case Status::TargetNotReady:\n      return "Target not ready; keep capturing until confidence reaches Low";\n    case Status::WrongWorkflowStage:\n      return "That action is not available from the current workflow stage";\n    case Status::ImportedReference:''',
    'clear rejection status text')

regex_once(
    'plugins/clap/tonetrace_clap.cpp',
    r'const char\* workflowText\(int value\) noexcept \{.*?\n\}\n\nconst char\* modeText',
    '''const char* workflowActionText(int value) noexcept {\n  switch (value) {\n    case 1: return "Capture Reference";\n    case 2: return "Learn Target";\n    case 3: return "Correct Target";\n    case 4: return "Freeze Correction";\n    default: return "None";\n  }\n}\n\nconst char* workflowStageText(int value) noexcept {\n  switch (value) {\n    case 1: return "Capturing Reference";\n    case 2: return "Learning Target";\n    case 3: return "Preview Correction";\n    case 4: return "Frozen Correction";\n    default: return "Ready";\n  }\n}\n\nbool isWorkflowCommandParameter(tonetrace::ParameterId id) noexcept {\n  return id == tonetrace::ParameterId::CaptureReferenceCommand ||\n         id == tonetrace::ParameterId::LearnTargetCommand ||\n         id == tonetrace::ParameterId::CorrectTargetCommand ||\n         id == tonetrace::ParameterId::FreezeCorrectionCommand;\n}\n\nint workflowCommandNumber(tonetrace::ParameterId id) noexcept {\n  if (id == tonetrace::ParameterId::CaptureReferenceCommand) return 1;\n  if (id == tonetrace::ParameterId::LearnTargetCommand) return 2;\n  if (id == tonetrace::ParameterId::CorrectTargetCommand) return 3;\n  if (id == tonetrace::ParameterId::FreezeCorrectionCommand) return 4;\n  return 0;\n}\n\nbool isHostVisibleFeedback(tonetrace::ParameterId id) noexcept {\n  return id == tonetrace::ParameterId::WorkflowStage ||\n         id == tonetrace::ParameterId::Status;\n}\n\nconst char* modeText''',
    'workflow action/stage helpers')

replace_once(
    'plugins/clap/tonetrace_clap.cpp',
    '''  void markDirty(std::size_t index) noexcept {\n    if (index < dirtyValues_.size()) {\n      dirtyValues_[index]->store(true, std::memory_order_release);\n    }\n  }''',
    '''  void markDirty(std::size_t index) noexcept {\n    const auto& descriptors = tonetrace::parameterDescriptors();\n    if (index >= dirtyValues_.size() || index >= descriptors.size()) return;\n    if (descriptors[index].readOnly &&\n        !isHostVisibleFeedback(descriptors[index].id)) {\n      return;\n    }\n    dirtyValues_[index]->store(true, std::memory_order_release);\n  }''',
    'host telemetry filter')

regex_once(
    'plugins/clap/tonetrace_clap.cpp',
    r'  void setWorkflowStep\(int step\) noexcept \{.*?\n  bool applyParameter\(clap_id id, double requested\) noexcept \{',
    '''  void setWorkflowPhase(tonetrace::WorkflowPhase phase) noexcept {\n    phase_.store(phase, std::memory_order_release);\n    setValue(tonetrace::ParameterId::WorkflowStage,\n             static_cast<double>(phase), true);\n  }\n\n  void resetCaptureTelemetry() noexcept {\n    setValue(tonetrace::ParameterId::Confidence, 0.0, true);\n    setValue(tonetrace::ParameterId::CurveDriftDb, 60.0, true);\n    setValue(tonetrace::ParameterId::CaptureSeconds, 0.0, true);\n  }\n\n  [[nodiscard]] Status captureStatus(tonetrace::WorkflowPhase phase,\n                                     int level,\n                                     const CaptureBuffer& capture) const noexcept {\n    if (phase == tonetrace::WorkflowPhase::CapturingReference) {\n      if (level >= 3) return Status::ReferenceHighConfidence;\n      if (level == 2) return Status::ReferenceMediumConfidence;\n      if (level == 1) return Status::ReferenceLowConfidence;\n      if (capture.validFrames == 0) return Status::CapturingReference;\n      if (capture.updateCounter == 0) return Status::ReferenceAudioDetected;\n      if (capture.validSeconds(sampleRate_) < 0.35) return Status::ReferenceCollecting;\n      if (capture.stability > 0.18) return Status::ReferenceUnstable;\n      return Status::ReferenceReady;\n    }\n    if (level >= 3) return Status::TargetHighConfidence;\n    if (level == 2) return Status::TargetMediumConfidence;\n    if (level == 1) return Status::TargetLowConfidence;\n    if (capture.validFrames == 0) return Status::CapturingTarget;\n    if (capture.updateCounter == 0) return Status::TargetAudioDetected;\n    if (capture.validSeconds(sampleRate_) < 0.35) return Status::TargetCollecting;\n    if (capture.stability > 0.18) return Status::TargetUnstable;\n    return Status::TargetReady;\n  }\n\n  [[nodiscard]] bool executeWorkflowAction(int command) noexcept {\n    if (command <= 0 || command > 4) return false;\n    setValue(tonetrace::ParameterId::LastCommand, static_cast<double>(command), true);\n    if (controlBusy_.load(std::memory_order_acquire)) {\n      setStatus(Status::Analyzing);\n      return false;\n    }\n    const auto currentPhase = phase_.load(std::memory_order_acquire);\n    switch (command) {\n      case 1:\n        stopTones();\n        captureBlocked_ = false;\n        setupLockedNotice_ = false;\n        importedReference_.reset();\n        importedTarget_.reset();\n        stagedReferenceForExport_.reset();\n        reference_.reset();\n        target_.reset();\n        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingReference);\n        setStatus(Status::CapturingReference);\n        resetCaptureTelemetry();\n        return true;\n      case 2:\n        if (currentPhase == tonetrace::WorkflowPhase::CapturingTarget) {\n          stopTones();\n          captureBlocked_ = false;\n          setupLockedNotice_ = false;\n          importedTarget_.reset();\n          target_.reset();\n          setStatus(Status::CapturingTarget);\n          resetCaptureTelemetry();\n          return true;\n        }\n        if (currentPhase != tonetrace::WorkflowPhase::CapturingReference) {\n          requestWarningSweep();\n          setStatus(Status::WrongWorkflowStage);\n          return false;\n        }\n        if (!tonetrace::referenceCaptureCanAdvance(\n                currentPhase, reference_.confidenceLevel, reference_.overflowed,\n                importedReference_.has_value())) {\n          captureBlocked_ = true;\n          requestWarningSweep();\n          setStatus(Status::CannotSaveYet);\n          return false;\n        }\n        stopTones();\n        captureBlocked_ = false;\n        setupLockedNotice_ = false;\n        importedTarget_.reset();\n        target_.reset();\n        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingTarget);\n        setStatus(Status::CapturingTarget);\n        resetCaptureTelemetry();\n        return true;\n      case 3:\n        if (currentPhase != tonetrace::WorkflowPhase::CapturingTarget) {\n          requestWarningSweep();\n          setStatus(Status::WrongWorkflowStage);\n          return false;\n        }\n        if (!tonetrace::targetCaptureCanCorrect(\n                currentPhase, target_.confidenceLevel, target_.overflowed,\n                importedTarget_.has_value())) {\n          captureBlocked_ = true;\n          requestWarningSweep();\n          setStatus(Status::TargetNotReady);\n          return false;\n        }\n        stopTones();\n        captureBlocked_ = false;\n        setStatus(Status::Analyzing);\n        controlBusy_.store(true, std::memory_order_release);\n        requestMainThread(WorkAnalyze);\n        return true;\n      case 4:\n        if (currentPhase == tonetrace::WorkflowPhase::Frozen) return true;\n        if (currentPhase != tonetrace::WorkflowPhase::Preview ||\n            !hasProfile_.load(std::memory_order_acquire)) {\n          requestWarningSweep();\n          setStatus(Status::WrongWorkflowStage);\n          return false;\n        }\n        setWorkflowPhase(tonetrace::WorkflowPhase::Frozen);\n        setStatus(Status::Frozen);\n        stopTones();\n        return true;\n      default:\n        return false;\n    }\n  }\n\n  bool applyParameter(clap_id id, double requested) noexcept {''',
    'workflow state machine')

regex_once(
    'plugins/clap/tonetrace_clap.cpp',
    r'  bool applyParameter\(clap_id id, double requested\) noexcept \{.*?\n    return true;\n  \}\n\n  bool applyEvents',
    '''  bool applyParameter(clap_id id, double requested) noexcept {\n    const auto& descriptors = tonetrace::parameterDescriptors();\n    const std::size_t index = parameterIndex(id);\n    if (index >= descriptors.size()) return false;\n    if (descriptors[index].readOnly) {\n      markDirty(index);\n      return false;\n    }\n    const auto parameterId = descriptors[index].id;\n    if (isWorkflowCommandParameter(parameterId)) {\n      if (!std::isfinite(requested)) return false;\n      values_[index]->store(0.0, std::memory_order_release);\n      if (requested > 0.0) {\n        (void)executeWorkflowAction(workflowCommandNumber(parameterId));\n        markDirty(index);\n      }\n      return true;\n    }\n    const double previousValue = values_[index]->load(std::memory_order_acquire);\n    const double newValue = clampedValue(descriptors[index], requested);\n    const auto currentPhase = phase_.load(std::memory_order_acquire);\n    if (parameterId == tonetrace::ParameterId::MatchMode &&\n        (currentPhase == tonetrace::WorkflowPhase::CapturingReference ||\n         currentPhase == tonetrace::WorkflowPhase::CapturingTarget)) {\n      markDirty(index);\n      setupLockedNotice_ = true;\n      setStatus(Status::SetupLocked);\n      return false;\n    }\n    values_[index]->store(newValue, std::memory_order_release);\n    if (parameterId == tonetrace::ParameterId::Bypass) {\n      if (core_ != nullptr) core_->setBypassed(newValue >= 0.5);\n    } else if (parameterId == tonetrace::ParameterId::MatchMode) {\n      if (hasProfile_.load(std::memory_order_acquire)) {\n        if (newValue != previousValue) {\n          setStatus(Status::Analyzing);\n          requestMainThread(WorkRebuild);\n        } else {\n          setStatus(currentPhase == tonetrace::WorkflowPhase::Frozen\n                        ? Status::Frozen : Status::Preview);\n        }\n      }\n    } else if (parameterId == tonetrace::ParameterId::ToneNotifications ||\n               parameterId == tonetrace::ParameterId::ToneLevelDb ||\n               parameterId == tonetrace::ParameterId::EmergencyClipGuardDb) {\n    } else if (hasProfile_.load(std::memory_order_acquire)) {\n      requestMainThread(WorkRebuild);\n    } else if (parameterId == tonetrace::ParameterId::Resolution ||\n               parameterId == tonetrace::ParameterId::CorrectionGainDb ||\n               parameterId == tonetrace::ParameterId::RangeLowHz ||\n               parameterId == tonetrace::ParameterId::RangeHighHz) {\n      if (parameterId == tonetrace::ParameterId::Resolution) syncManualGainsSize();\n      requestMainThread(WorkRebuild);\n    }\n    return true;\n  }\n\n  bool applyEvents''',
    'direct parameter application')

replace_once(
    'plugins/clap/tonetrace_clap.cpp',
    '''    const bool nowReady = phase == tonetrace::WorkflowPhase::CapturingReference\n                              ? destination->readyForSave(sampleRate_)\n                              : destination->confidenceLevel >= 1;\n    if (nowReady) captureBlocked_ = false;\n    setStatus(setupLockedNotice_\n                  ? Status::SetupLocked\n                  : (captureBlocked_\n                         ? (phase == tonetrace::WorkflowPhase::CapturingReference\n                                ? Status::CannotSaveYet\n                                : Status::InvalidCapture)\n                                     : captureStatus(phase, newConfidence,\n                                                     *destination)));''',
    '''    const bool nowReady = phase == tonetrace::WorkflowPhase::CapturingReference\n                              ? tonetrace::referenceCaptureCanAdvance(\n                                    phase, destination->confidenceLevel,\n                                    destination->overflowed, false)\n                              : tonetrace::targetCaptureCanCorrect(\n                                    phase, destination->confidenceLevel,\n                                    destination->overflowed, false);\n    if (nowReady) captureBlocked_ = false;\n    setStatus(setupLockedNotice_\n                  ? Status::SetupLocked\n                  : (captureBlocked_\n                         ? (phase == tonetrace::WorkflowPhase::CapturingReference\n                                ? Status::CannotSaveYet : Status::TargetNotReady)\n                         : captureStatus(phase, newConfidence, *destination)));''',
    'shared live capture gate')

replace_once('plugins/clap/tonetrace_clap.cpp',
    '''    phase_.store(tonetrace::WorkflowPhase::Preview,\n                 std::memory_order_release);\n    setValue(tonetrace::ParameterId::Confidence,''',
    '''    setWorkflowPhase(tonetrace::WorkflowPhase::Preview);\n    setValue(tonetrace::ParameterId::Confidence,''', 'analysis success stage')
replace_once('plugins/clap/tonetrace_clap.cpp',
    '''    phase_.store(tonetrace::WorkflowPhase::CapturingTarget,\n                 std::memory_order_release);\n    setValue(tonetrace::ParameterId::Confidence, 0.0, true);''',
    '''    setWorkflowPhase(tonetrace::WorkflowPhase::CapturingTarget);\n    setValue(tonetrace::ParameterId::Confidence, 0.0, true);''', 'imported reference stage')
replace_once('plugins/clap/tonetrace_clap.cpp', '    setStatus(Status::ImportedReference);\n    setWorkflowStep(2);', '    setStatus(Status::ImportedReference);', 'remove imported reference legacy step')
replace_once('plugins/clap/tonetrace_clap.cpp',
    '''    importedTarget_ = capture;\n    phase_.store(tonetrace::WorkflowPhase::Preview,\n                 std::memory_order_release);\n    setStatus(Status::ImportedTarget);\n    controlBusy_.store(true, std::memory_order_release);\n    setWorkflowStep(3);\n    requestMainThread(WorkAnalyze);''',
    '''    importedTarget_ = capture;\n    setWorkflowPhase(tonetrace::WorkflowPhase::CapturingTarget);\n    setStatus(Status::ImportedTarget);\n    controlBusy_.store(true, std::memory_order_release);\n    requestMainThread(WorkAnalyze);''', 'imported target stage')
replace_once('plugins/clap/tonetrace_clap.cpp',
    '''    phase_.store(tonetrace::WorkflowPhase::Frozen,\n                 std::memory_order_release);\n    setValue(tonetrace::ParameterId::MatchMode,''',
    '''    setWorkflowPhase(tonetrace::WorkflowPhase::Frozen);\n    setValue(tonetrace::ParameterId::MatchMode,''', 'imported model stage')
replace_once('plugins/clap/tonetrace_clap.cpp', '    setStatus(Status::Frozen);\n    setWorkflowStep(4);', '    setStatus(Status::Frozen);', 'remove imported model legacy step')

replace_once('plugins/clap/tonetrace_clap.cpp',
    '''        phase == tonetrace::WorkflowPhase::CapturingTarget ||\n        (phase == tonetrace::WorkflowPhase::CapturingReference &&\n         reference_.readyForSave(sampleRate_));''',
    '''        phase == tonetrace::WorkflowPhase::CapturingTarget ||\n        tonetrace::referenceCaptureCanAdvance(\n            phase, reference_.confidenceLevel, reference_.overflowed, false);''', 'import target reference readiness')
replace_once('plugins/clap/tonetrace_clap.cpp',
    '''        phase_.load(std::memory_order_acquire) !=\n            tonetrace::WorkflowPhase::CapturingTarget ||\n        !reference_.readyForSave(sampleRate_)) {''',
    '''        phase_.load(std::memory_order_acquire) !=\n            tonetrace::WorkflowPhase::CapturingTarget ||\n        !tonetrace::referenceCaptureCanAdvance(\n            tonetrace::WorkflowPhase::CapturingReference,\n            reference_.confidenceLevel, reference_.overflowed, false)) {''', 'staged reference readiness')
replace_once('plugins/clap/tonetrace_clap.cpp', '    (void)applyParameter(id, requested);\n    consumeWorkflowCommandWhenIdle();', '    (void)applyParameter(id, requested);', 'editor direct workflow execution')

p = ROOT / 'plugins/clap/tonetrace_clap.cpp'
text = p.read_text(encoding='utf-8')
for old in ['    consumePendingWorkflowCommand();\n\n', '      consumePendingWorkflowCommand();\n', '''    // REAPER delivers parameter changes through param_flush while the\n    // transport is stopped and never calls process(). Consume a queued\n    // workflow command here so every step advances without requiring play,\n    // before pushing the resulting status/value updates to the host.\n    instance->consumePendingWorkflowCommand();\n''']:
    text = text.replace(old, '')
text = text.replace('descriptor.id != tonetrace::ParameterId::WorkflowAction', '!isWorkflowCommandParameter(descriptor.id)')
text = text.replace('descriptors[i].id == tonetrace::ParameterId::WorkflowAction', 'isWorkflowCommandParameter(descriptors[i].id)')
text = text.replace('descriptors[index].id == tonetrace::ParameterId::WorkflowAction', 'isWorkflowCommandParameter(descriptors[index].id)')
text = text.replace('''    setValue(tonetrace::ParameterId::WorkflowAction, 0.0);\n    lastWorkflowStep_.store(0, std::memory_order_release);''', '    setWorkflowPhase(tonetrace::WorkflowPhase::Ready);')
text = text.replace('      instance->pendingWorkflowCommand_.store(0, std::memory_order_release);\n', '')
text = text.replace('    instance->pendingWorkflowCommand_.store(0, std::memory_order_release);\n', '')
text = text.replace('      instance->setWorkflowStep(0);\n', '      instance->setWorkflowPhase(tonetrace::WorkflowPhase::Ready);\n')
text = text.replace('''        instance->phase_.store(tonetrace::WorkflowPhase::Frozen,\n                               std::memory_order_release);''', '        instance->setWorkflowPhase(tonetrace::WorkflowPhase::Frozen);')
text = text.replace('''        instance->phase_.store(tonetrace::WorkflowPhase::Ready,\n                               std::memory_order_release);''', '        instance->setWorkflowPhase(tonetrace::WorkflowPhase::Ready);')
p.write_text(text, encoding='utf-8')

regex_once('plugins/clap/tonetrace_clap.cpp',
    r'  static bool CLAP_ABI paramGetInfo\(const clap_plugin_t\*,.*?\n    return true;\n  \}\n\n  static bool CLAP_ABI paramGetValue',
    '''  static bool CLAP_ABI paramGetInfo(const clap_plugin_t*,\n                                    uint32_t index,\n                                    clap_param_info_t* info) noexcept {\n    const auto& descriptors = tonetrace::parameterDescriptors();\n    if (info == nullptr || index >= descriptors.size()) return false;\n    const auto& descriptor = descriptors[index];\n    std::memset(info, 0, sizeof(*info));\n    info->id = static_cast<clap_id>(descriptor.id);\n    if (descriptor.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;\n    if (isWorkflowCommandParameter(descriptor.id) || descriptor.id == tonetrace::ParameterId::WorkflowStage || descriptor.id == tonetrace::ParameterId::Status || descriptor.id == tonetrace::ParameterId::MatchMode) info->flags |= CLAP_PARAM_IS_ENUM;\n    if (descriptor.readOnly) {\n      info->flags |= CLAP_PARAM_IS_READONLY;\n      if (!isHostVisibleFeedback(descriptor.id)) info->flags |= CLAP_PARAM_IS_HIDDEN;\n    }\n    if (descriptor.automatable) info->flags |= CLAP_PARAM_IS_AUTOMATABLE;\n    if (descriptor.id == tonetrace::ParameterId::Bypass) info->flags |= CLAP_PARAM_IS_BYPASS;\n    std::snprintf(info->name, sizeof(info->name), "%s", descriptor.name);\n    const char* module = descriptor.readOnly ? "Status" : (isWorkflowCommandParameter(descriptor.id) ? "Workflow" : "Tone Trace");\n    std::snprintf(info->module, sizeof(info->module), "%s", module);\n    info->min_value = descriptor.minimum;\n    info->max_value = descriptor.maximum;\n    info->default_value = descriptor.defaultValue;\n    return true;\n  }\n\n  static bool CLAP_ABI paramGetValue''', 'CLAP parameter flags')

regex_once('plugins/clap/tonetrace_clap.cpp',
    r'  static bool CLAP_ABI paramValueToText\(const clap_plugin_t\* plugin,.*?\n    return true;\n  \}\n\n  static bool CLAP_ABI paramTextToValue',
    '''  static bool CLAP_ABI paramValueToText(const clap_plugin_t* plugin,\n                                        clap_id id, double value, char* output,\n                                        uint32_t capacity) noexcept {\n    const auto& descriptors = tonetrace::parameterDescriptors();\n    const std::size_t index = parameterIndex(id);\n    if (output == nullptr || capacity == 0 || index >= descriptors.size()) return false;\n    const auto parameter = descriptors[index].id;\n    if (isWorkflowCommandParameter(parameter)) {\n      std::snprintf(output, capacity, "%s", value > 0.0 ? "Requested" : "Ready");\n    } else if (parameter == tonetrace::ParameterId::WorkflowStage) {\n      std::snprintf(output, capacity, "%s", workflowStageText(static_cast<int>(std::lround(value))));\n    } else if (parameter == tonetrace::ParameterId::MatchMode) {\n      std::snprintf(output, capacity, "%s", modeText(static_cast<int>(std::lround(value))));\n    } else if (parameter == tonetrace::ParameterId::LastCommand) {\n      std::snprintf(output, capacity, "%s", workflowActionText(static_cast<int>(std::lround(value))));\n    } else if (parameter == tonetrace::ParameterId::Status) {\n      std::snprintf(output, capacity, "%s", statusText(static_cast<int>(std::lround(value))));\n    } else if (parameter == tonetrace::ParameterId::Confidence) {\n      const int level = static_cast<int>(std::lround(value * 3.0));\n      const auto* instance = self(plugin);\n      const bool acceptedAudio = instance != nullptr && instance->value(tonetrace::ParameterId::CaptureSeconds) > 0.0;\n      std::snprintf(output, capacity, "%s", level >= 3 ? "High" : level == 2 ? "Medium" : level == 1 ? "Low; usable with caution" : acceptedAudio ? "Not yet confident" : "No valid audio");\n    } else if (parameter == tonetrace::ParameterId::ToneLevelDb && value <= -59.5) {\n      std::snprintf(output, capacity, "%s", "Off");\n    } else if (parameter == tonetrace::ParameterId::CompleteMatch || parameter == tonetrace::ParameterId::ToneNotifications || parameter == tonetrace::ParameterId::Bypass) {\n      std::snprintf(output, capacity, "%s", value >= 0.5 ? "On" : "Off");\n    } else if (parameter == tonetrace::ParameterId::Resolution) {\n      std::snprintf(output, capacity, "%.0f %s", value, std::lround(value) == 1 ? "band" : "bands");\n    } else if (descriptors[index].stepped) {\n      std::snprintf(output, capacity, "%.0f %s", value, descriptors[index].unit);\n    } else {\n      std::snprintf(output, capacity, "%.3f %s", value, descriptors[index].unit);\n    }\n    return true;\n  }\n\n  static bool CLAP_ABI paramTextToValue''', 'parameter value text')

p = ROOT / 'plugins/clap/tonetrace_clap.cpp'
text = p.read_text(encoding='utf-8')
old = '''    const auto parameter = descriptors[index].id;\n    if (parameter == tonetrace::ParameterId::WorkflowAction) {\n      for (int step = 0; step <= 7; ++step) {\n        if (std::strcmp(text, workflowText(step)) == 0) {\n          *output = static_cast<double>(step);\n          return true;\n        }\n      }\n    } else if (parameter == tonetrace::ParameterId::MatchMode) {'''
new = '''    const auto parameter = descriptors[index].id;\n    if (isWorkflowCommandParameter(parameter)) {\n      if (std::strcmp(text, "Requested") == 0 || std::strcmp(text, "requested") == 0 || std::strcmp(text, "Run") == 0 || std::strcmp(text, "run") == 0 || std::strcmp(text, "On") == 0 || std::strcmp(text, "on") == 0) { *output = 1.0; return true; }\n      if (std::strcmp(text, "Ready") == 0 || std::strcmp(text, "ready") == 0 || std::strcmp(text, "Off") == 0 || std::strcmp(text, "off") == 0) { *output = 0.0; return true; }\n    } else if (parameter == tonetrace::ParameterId::MatchMode) {'''
if old not in text: raise SystemExit('workflow text parser block not found')
text = text.replace(old, new)
text = text.replace('''  std::atomic<tonetrace::WorkflowPhase> phase_{tonetrace::WorkflowPhase::Ready};\n  std::atomic<tonetrace::WorkflowPhase> phaseBeforeReset_{\n      tonetrace::WorkflowPhase::Ready};''', '  std::atomic<tonetrace::WorkflowPhase> phase_{tonetrace::WorkflowPhase::Ready};')
text = text.replace('  std::atomic<bool> resetArmed_{false};\n', '')
text = text.replace('  std::atomic<int> lastWorkflowStep_{0};\n', '')
text = text.replace('  std::atomic<int> pendingWorkflowCommand_{0};\n', '')
p.write_text(text, encoding='utf-8')

editor = ROOT / 'plugins/clap/tonetrace_win32_editor.cpp'
text = editor.read_text(encoding='utf-8')
for old, new in {
    'tonetrace::ParameterId::WorkflowAction),\n                1.0);':'tonetrace::ParameterId::CaptureReferenceCommand),\n                1.0);',
    'tonetrace::ParameterId::WorkflowAction),\n                2.0);':'tonetrace::ParameterId::LearnTargetCommand),\n                1.0);',
    'tonetrace::ParameterId::WorkflowAction),\n                3.0);':'tonetrace::ParameterId::CorrectTargetCommand),\n                1.0);',
    'tonetrace::ParameterId::WorkflowAction),\n                4.0);':'tonetrace::ParameterId::FreezeCorrectionCommand),\n                1.0);',
}.items():
    if text.count(old) != 1: raise SystemExit(f'editor workflow replacement count {text.count(old)} for {old!r}')
    text = text.replace(old, new)
editor.write_text(text, encoding='utf-8')

print('workflow architecture source rewrite completed')
