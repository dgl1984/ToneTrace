from pathlib import Path
p = Path('tests/test_realtime.cpp')
text = p.read_text(encoding='utf-8')
old = '  std::cout << "universal parameter and workflow-readiness contract: passed\n";'
new = r'  std::cout << "universal parameter and workflow-readiness contract: passed\n";'
count = text.count(old)
if count != 1:
    raise SystemExit(f'test newline normalization: expected 1, found {count}')
p.write_text(text.replace(old, new), encoding='utf-8')
print('generated C++ test newline escaped correctly')
