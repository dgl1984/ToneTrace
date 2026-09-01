from pathlib import Path

p = Path('tests/test_clap.cpp')
text = p.read_text(encoding='utf-8')
old = '''    require(restored.parameter(kStatus) == 8.0 &&
                restored.tail->get(restored.plugin) > 0 &&
                rejectedProfileWarning > 0.01,
            "profile-damaging clipped audio was not rejected and reported audibly");'''
new = '''    require(restored.parameter(kStatus) == 8.0 &&
                restored.tail->get(restored.plugin) > 0 &&
                rejectedProfileWarning > 0.01,
            "profile-damaging clipped audio was not rejected and reported audibly: status=" +
                std::to_string(restored.parameter(kStatus)) +
                ", stage=" + std::to_string(restored.parameter(kWorkflowStage)) +
                ", tail=" + std::to_string(restored.tail->get(restored.plugin)) +
                ", warningEnergy=" + std::to_string(rejectedProfileWarning));'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'clipped rejection assertion: expected 1, found {count}')
p.write_text(text.replace(old, new), encoding='utf-8')
print('clipped-capture regression now reports status/stage/tail/tone energy')
