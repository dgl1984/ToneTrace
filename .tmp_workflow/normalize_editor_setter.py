from pathlib import Path
p = Path('plugins/clap/tonetrace_clap.cpp')
text = p.read_text(encoding='utf-8')
old = '''  void applyEditorParameter(clap_id id, double requested) noexcept {\n    const std::size_t index = parameterIndex(id);\n    if (index >= tonetrace::parameterDescriptors().size()) return;\n    (void)applyParameter(id, requested);\n\n    // applyParameter() updates the plug-in immediately, but a native-editor\n    // change is not a host input event. Queue the authoritative resulting\n    // value (including any workflow fallback such as 2 -> 1) for the host and\n    // ask for a parameter flush. This replaces the old accidental dependency\n    // on correction rebuilds/rescans for keeping REAPER/OSARA in sync.\n    markDirty(index);\n    requestHostParameterFlush();\n  }'''
new = '''  void applyEditorParameter(clap_id id, double requested) noexcept {\n    const std::size_t index = parameterIndex(id);\n    if (index >= tonetrace::parameterDescriptors().size()) return;\n    (void)applyParameter(id, requested);\n  }'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'editor setter normalization: expected 1, found {count}')
p.write_text(text.replace(old, new), encoding='utf-8')
print('obsolete sticky-workflow editor synchronization removed')
