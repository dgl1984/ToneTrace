from pathlib import Path
p = Path('tests/test_clap.cpp')
text = p.read_text(encoding='utf-8')
old = '  constexpr std::size_t referenceFrames = 48000;\n'
new = '''  // Voice uses the strictest Low-confidence time threshold. Reference must\n  // satisfy the same gate as Target before this test can exercise Target's\n  // Capture Full fallback.\n  constexpr std::size_t referenceFrames = 48000 * 3U;\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'shared-gate fallback fixture: expected 1, found {count}')
p.write_text(text.replace(old, new), encoding='utf-8')
print('Capture Full fixture now satisfies the shared Reference gate')
