from pathlib import Path

p = Path('tests/test_win32_ui_regression.cpp')
text = p.read_text(encoding='utf-8')

def one(old, new, label):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected 1, found {count}')
    text = text.replace(old, new)

one(
'''    if (position == 0 && command != 0) {
      input.events.clear();
      input.add(100, command);
    }''',
'''    if (position == 0 && command != 0) {
      // The workflow is no longer a traversed 0..7 slider. Drive the same
      // four explicit momentary host actions that REAPER/OSARA now exposes.
      static constexpr clap_id workflowActionIds[]{0, 281, 282, 283, 284};
      require(command >= 1 && command <= 4,
              "ui harness requested an unknown workflow action");
      input.events.clear();
      input.add(workflowActionIds[command], 1.0);
    }''',
'Win32 UI workflow event routing')

one(
'''              ", workflow=" +
              std::to_string(static_cast<int>(valueOf(100))) +''',
'''              ", stage=" +
              std::to_string(static_cast<int>(valueOf(280))) +''',
'Win32 UI workflow diagnostic')

p.write_text(text, encoding='utf-8')
print('Win32 visual regression harness now drives explicit workflow actions')
