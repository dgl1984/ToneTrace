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
'''    destination->append(input, channels, count, sampleRate_,
                        matchMode(value(tonetrace::ParameterId::MatchMode)));
    const int newConfidence = destination->confidenceLevel;''',
'''    destination->append(input, channels, count, sampleRate_,
                        matchMode(value(tonetrace::ParameterId::MatchMode)));
    const int newConfidence = destination->confidenceLevel;
    if (phase == tonetrace::WorkflowPhase::CapturingTarget) {
      const auto rejectedFrames =
          rejectedTargetFrames_.load(std::memory_order_acquire);
      if (rejectedFrames != kNoRejectedCaptureFrames) {
        if (destination->validFrames <= rejectedFrames) {
          // This is still the exact Target snapshot that failed validation.
          // Silence, transport ticks, and other host housekeeping must not
          // overwrite the actionable rejection message with live telemetry.
          setStatus(Status::InvalidCapture);
          return;
        }
        // New valid Target audio makes a retry meaningful. Resume normal live
        // capture reporting and let a later Correct Target request revalidate.
        rejectedTargetFrames_.store(kNoRejectedCaptureFrames,
                                    std::memory_order_release);
      }
    }''',
'preserve rejected target status')

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
        rejectedTargetFrames_.store(target_.validFrames,
                                    std::memory_order_release);
      }
      setStatus(validation.issue == tonetrace::ProfileIssue::RendererBusy
                    ? Status::RendererBusy
                    : Status::InvalidCapture);''',
'record rejected target snapshot')

one(
'''    setWorkflowPhase(tonetrace::WorkflowPhase::Preview);
    setValue(tonetrace::ParameterId::Confidence,''',
'''    rejectedTargetFrames_.store(kNoRejectedCaptureFrames,
                                std::memory_order_release);
    setWorkflowPhase(tonetrace::WorkflowPhase::Preview);
    setValue(tonetrace::ParameterId::Confidence,''',
'clear rejected snapshot after successful analysis')

# A deliberate new capture invalidates any previous rejection revision.
for old, new, label in [
    ('''        target_.reset();
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingReference);''',
     '''        target_.reset();
        rejectedTargetFrames_.store(kNoRejectedCaptureFrames,
                                    std::memory_order_release);
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingReference);''',
     'clear rejection on Capture Reference'),
    ('''          target_.reset();
          setStatus(Status::CapturingTarget);''',
     '''          target_.reset();
          rejectedTargetFrames_.store(kNoRejectedCaptureFrames,
                                      std::memory_order_release);
          setStatus(Status::CapturingTarget);''',
     'clear rejection on repeated Learn Target'),
    ('''        target_.reset();
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingTarget);''',
     '''        target_.reset();
        rejectedTargetFrames_.store(kNoRejectedCaptureFrames,
                                    std::memory_order_release);
        setWorkflowPhase(tonetrace::WorkflowPhase::CapturingTarget);''',
     'clear rejection entering Target')]:
    one(old, new, label)

one(
'''  std::atomic<bool> controlBusy_{false};
  std::atomic<int> pendingEditorWorkflowAction_{0};
  std::atomic<bool> hasProfile_{false};''',
'''  static constexpr std::uint64_t kNoRejectedCaptureFrames =
      std::numeric_limits<std::uint64_t>::max();
  std::atomic<bool> controlBusy_{false};
  std::atomic<int> pendingEditorWorkflowAction_{0};
  std::atomic<std::uint64_t> rejectedTargetFrames_{kNoRejectedCaptureFrames};
  std::atomic<bool> hasProfile_{false};''',
'rejected capture revision state')

p.write_text(text, encoding='utf-8')
print('analysis rejection remains visible until Target capture actually changes')
