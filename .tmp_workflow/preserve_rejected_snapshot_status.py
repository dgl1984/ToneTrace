from pathlib import Path

p = Path('plugins/clap/tonetrace_clap.cpp')
text = p.read_text(encoding='utf-8')

def one(old, new, label):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected 1, found {count}')
    text = text.replace(old, new)

one(
'''    const int oldConfidence = destination->confidenceLevel;
    destination->append(input, channels, count, sampleRate_,
                        matchMode(value(tonetrace::ParameterId::MatchMode)));''',
'''    if (phase == tonetrace::WorkflowPhase::CapturingTarget &&
        targetCaptureRejected_.load(std::memory_order_acquire)) {
      // Validation rejected this exact Target snapshot. Freeze it until the
      // user explicitly chooses Learn Target again (or starts over with
      // Capture Reference); incidental audio/transport activity must not make
      // a rejected capture look live or erase the actionable Status message.
      setStatus(Status::InvalidCapture);
      return;
    }
    const int oldConfidence = destination->confidenceLevel;
    destination->append(input, channels, count, sampleRate_,
                        matchMode(value(tonetrace::ParameterId::MatchMode)));''',
'freeze rejected Target capture')

one(
'''    candidate.referenceDiagnostics = reference_.profileDiagnostics();
    candidate.targetDiagnostics = target_.profileDiagnostics();
    const auto validation = core_->commitCandidate(candidate);
    if (!validation.accepted) {
      setStatus(validation.issue == tonetrace::ProfileIssue::RendererBusy
                    ? Status::RendererBusy
                    : Status::InvalidCapture);''',
'''    candidate.referenceDiagnostics = reference_.profileDiagnostics();
    candidate.targetDiagnostics = target_.profileDiagnostics();
    const auto validation = core_->commitCandidate(candidate);
    if (!validation.accepted) {
      if (validation.issue != tonetrace::ProfileIssue::RendererBusy) {
        targetCaptureRejected_.store(true, std::memory_order_release);
      }
      setStatus(validation.issue == tonetrace::ProfileIssue::RendererBusy
                    ? Status::RendererBusy
                    : Status::InvalidCapture);''',
'freeze Target after validation rejection')

one(
'''    setWorkflowPhase(tonetrace::WorkflowPhase::Preview);
    setValue(tonetrace::ParameterId::Confidence,''',
'''    targetCaptureRejected_.store(false, std::memory_order_release);
    setWorkflowPhase(tonetrace::WorkflowPhase::Preview);
    setValue(tonetrace::ParameterId::Confidence,''',
'clear rejected Target after successful analysis')

for old, new, label in [
    ('''        target_.reset();
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingReference);''',
     '''        target_.reset();
        targetCaptureRejected_.store(false, std::memory_order_release);
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingReference);''',
     'clear rejection on Capture Reference'),
    ('''          target_.reset();
          setStatus(Status::CapturingTarget);''',
     '''          target_.reset();
          targetCaptureRejected_.store(false, std::memory_order_release);
          setStatus(Status::CapturingTarget);''',
     'clear rejection on repeated Learn Target'),
    ('''        target_.reset();
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingTarget);''',
     '''        target_.reset();
        targetCaptureRejected_.store(false, std::memory_order_release);
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingTarget);''',
     'clear rejection entering Target')]:
    one(old, new, label)

one(
'''    case Status::InvalidCapture: return "Invalid or contaminated capture";''',
'''    case Status::InvalidCapture:
      return "Invalid or contaminated capture; choose Learn Target to recapture";''',
'give rejected capture an explicit recovery action')

one(
'''  std::atomic<bool> controlBusy_{false};
  std::atomic<int> pendingEditorWorkflowAction_{0};
  std::atomic<bool> hasProfile_{false};''',
'''  std::atomic<bool> controlBusy_{false};
  std::atomic<int> pendingEditorWorkflowAction_{0};
  std::atomic<bool> targetCaptureRejected_{false};
  std::atomic<bool> hasProfile_{false};''',
'rejected Target state')

p.write_text(text, encoding='utf-8')
print('rejected Target capture freezes until explicit recapture')
