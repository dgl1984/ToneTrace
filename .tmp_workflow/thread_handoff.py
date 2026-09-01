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
'''  bool applyParameter(clap_id id, double requested) noexcept {\n    const auto& descriptors = tonetrace::parameterDescriptors();''',
'''  void consumePendingEditorWorkflowAction() noexcept {\n    const int action = pendingEditorWorkflowAction_.exchange(\n        0, std::memory_order_acq_rel);\n    if (action != 0) (void)executeWorkflowAction(action);\n  }\n\n  bool applyParameter(clap_id id, double requested,\n                      bool executeWorkflowNow = true) noexcept {\n    const auto& descriptors = tonetrace::parameterDescriptors();''',
'insert editor workflow handoff')

one(
'''    if (isWorkflowCommandParameter(parameterId)) {\n      if (!std::isfinite(requested)) return false;\n      values_[index]->store(0.0, std::memory_order_release);\n      if (requested > 0.0) {\n        (void)executeWorkflowAction(workflowCommandNumber(parameterId));\n        markDirty(index);\n      }\n      return true;\n    }''',
'''    if (isWorkflowCommandParameter(parameterId)) {\n      if (!std::isfinite(requested)) return false;\n      // Workflow parameters are momentary actions. Their authoritative value\n      // is always Ready (0), so the same action can be requested again without\n      // an intervening edge or host resynchronization trick. Any positive host\n      // value means "press"; fractional values from generic controls are valid.\n      values_[index]->store(0.0, std::memory_order_release);\n      if (requested > 0.0) {\n        const int action = workflowCommandNumber(parameterId);\n        if (executeWorkflowNow) {\n          (void)executeWorkflowAction(action);\n        } else {\n          int empty = 0;\n          if (!pendingEditorWorkflowAction_.compare_exchange_strong(\n                  empty, action, std::memory_order_release,\n                  std::memory_order_relaxed)) {\n            setStatus(Status::RendererBusy);\n            return false;\n          }\n        }\n        markDirty(index);\n      }\n      return true;\n    }''',
'queue editor workflow request')

one(
'''    bool doublePrecision = false;\n    if (!prepareWorkingAudio(process, input, output, channels, doublePrecision)) {\n      return CLAP_PROCESS_ERROR;\n    }\n\n    uint32_t cursor = 0;''',
'''    bool doublePrecision = false;\n    if (!prepareWorkingAudio(process, input, output, channels, doublePrecision)) {\n      return CLAP_PROCESS_ERROR;\n    }\n\n    // Native-editor requests cross to the audio thread here while realtime\n    // processing is active. Host parameter events execute directly on the\n    // thread/context that delivered them, so there is no shared sticky slider.\n    consumePendingEditorWorkflowAction();\n\n    uint32_t cursor = 0;''',
'consume editor action at audio boundary')

one(
'''    (void)instance->applyEvents(input, 0, count);\n    instance->pushDirtyValues(output, 0);''',
'''    (void)instance->applyEvents(input, 0, count);\n    // params.flush is the control-thread path used by REAPER when no process\n    // block is arriving (stopped/paused transport). It is also the safe handoff\n    // point for a native-editor action queued while the host reports the plugin\n    // as processing but is not actually delivering audio blocks.\n    instance->consumePendingEditorWorkflowAction();\n    instance->pushDirtyValues(output, 0);''',
'consume editor action in param flush')

one(
'''  void applyEditorParameter(clap_id id, double requested) noexcept {\n    const std::size_t index = parameterIndex(id);\n    if (index >= tonetrace::parameterDescriptors().size()) return;\n    (void)applyParameter(id, requested);\n  }''',
'''  void applyEditorParameter(clap_id id, double requested) noexcept {\n    const std::size_t index = parameterIndex(id);\n    const auto& descriptors = tonetrace::parameterDescriptors();\n    if (index >= descriptors.size()) return;\n    const bool workflow = isWorkflowCommandParameter(descriptors[index].id);\n    (void)applyParameter(id, requested, !workflow);\n    if (!workflow || requested <= 0.0) return;\n    if (!processing_.load(std::memory_order_acquire)) {\n      consumePendingEditorWorkflowAction();\n    } else {\n      // While playing, process() consumes the request at the next block. While\n      // paused, REAPER services request_flush via params.flush, which consumes\n      // it without requiring another audio block.\n      requestHostParameterFlush();\n    }\n  }''',
'editor workflow routing')

one(
'''  std::atomic<bool> controlBusy_{false};\n  std::atomic<bool> hasProfile_{false};''',
'''  std::atomic<bool> controlBusy_{false};\n  std::atomic<int> pendingEditorWorkflowAction_{0};\n  std::atomic<bool> hasProfile_{false};''',
'editor workflow atomic field')

# Activation/deactivation must never carry an editor request into a new lifetime.
one(
'''      instance->stopTones();\n      instance->setWorkflowPhase(tonetrace::WorkflowPhase::Ready);''',
'''      instance->stopTones();\n      instance->pendingEditorWorkflowAction_.store(0, std::memory_order_release);\n      instance->setWorkflowPhase(tonetrace::WorkflowPhase::Ready);''',
'clear editor request on activation')
one(
'''    instance->stopTones();\n    // A host may deliver a previously requested main-thread callback after''',
'''    instance->stopTones();\n    instance->pendingEditorWorkflowAction_.store(0, std::memory_order_release);\n    // A host may deliver a previously requested main-thread callback after''',
'clear editor request on deactivation')

p.write_text(text, encoding='utf-8')
print('thread-safe editor workflow handoff applied')
