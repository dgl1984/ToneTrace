from pathlib import Path
p = Path('plugins/clap/tonetrace_win32_editor.cpp')
text = p.read_text(encoding='utf-8')
text = text.replace('lastWorkflowStep_', 'lastWorkflowStage_')
old = '''  const int workflowStep = static_cast<int>(std::lround(\n      paramValue(tonetrace::ParameterId::WorkflowAction)));\n  if (workflowStep != lastWorkflowStage_) {\n    lastWorkflowStage_ = workflowStep;'''
new = '''  const int workflowStage = static_cast<int>(std::lround(\n      paramValue(tonetrace::ParameterId::WorkflowStage)));\n  if (workflowStage != lastWorkflowStage_) {\n    lastWorkflowStage_ = workflowStage;'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'workflow stage display replacement: expected 1, found {count}')
text = text.replace(old, new)
p.write_text(text, encoding='utf-8')
print('native workflow highlighting now follows authoritative Stage')
