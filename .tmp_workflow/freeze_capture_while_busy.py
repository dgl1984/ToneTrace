from pathlib import Path
p = Path('plugins/clap/tonetrace_clap.cpp')
text = p.read_text(encoding='utf-8')
old = '''  void captureSlice(const float* const* input,\n                    std::size_t channels,\n                    std::size_t count) noexcept {\n    const auto phase = phase_.load(std::memory_order_acquire);\n    CaptureBuffer* destination = nullptr;'''
new = '''  void captureSlice(const float* const* input,\n                    std::size_t channels,\n                    std::size_t count) noexcept {\n    // Once a control operation has accepted the current capture, that buffer\n    // becomes immutable until the main-thread work completes. In particular,\n    // Correct Target deliberately leaves Workflow Stage at Learning Target\n    // while analysis is pending; continuing capture here would both race the\n    // analysis snapshot and overwrite the authoritative Analyzing status with\n    // another live-capture status in the same process block.\n    if (controlBusy_.load(std::memory_order_acquire)) return;\n\n    const auto phase = phase_.load(std::memory_order_acquire);\n    CaptureBuffer* destination = nullptr;'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'capture busy boundary: expected 1, found {count}')
p.write_text(text.replace(old, new), encoding='utf-8')
print('capture freezes while accepted control work owns the snapshot')
