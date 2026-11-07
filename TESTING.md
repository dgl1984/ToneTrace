# Building and testing Tone Trace 1.3 from source

`VERSION` is the machine-readable package version (`1.3.0`). The public release
name is `1.3`. The source tree contains one plug-in implementation under
`plugins/juce`; there is no legacy wrapper fallback.

## Source layout

- `include/`, `src/` — canonical frozen matching/realtime engine used by both
  tools/tests and the plug-in. The source-contract check pins these files by SHA-256.
- `tests/`, `tools/` — engine, realtime, layout, and evaluation utilities.
- `plugins/juce/` — shared JUCE VST3/CLAP processor, editor, state, accessibility,
  correction controller, tests, visual snapshot harness, and CLAP compatibility patch.
- `cmake/` — release manifest writer and staged-package verification.
- `.github/workflows/ci.yml` — Windows release verification and Linux engine verification.
- `build_all_windows.bat` — one-command Windows release build and packaging.

Generated builds, downloaded dependencies, binaries, local logs, and distribution
ZIPs are intentionally not part of the source handoff.

## Dependencies

CMake fetches pinned JUCE and clap-juce-extensions sources on first configure.
For an offline build, provide complete local checkouts using `JUCE_DIR` and
`CLAP_JUCE_EXTENSIONS_DIR`.

## Windows release verification

Requirements: Windows 10/11 x64, CMake 3.25+, Git, Visual Studio 2022 Build
Tools with Desktop development with C++, and a Windows SDK. Run:

```bat
build_all_windows.bat
```

The builder configures a clean x64 Release build, builds VST3 and CLAP plus the
verification targets, runs all registered CTest suites, runs the source-contract
check, stages only release files, writes the build manifest, verifies the staged
package, creates the ZIP, and prints its SHA-256. It never installs a plug-in.

A current Windows build should register 13 tests: three repository-level engine/
realtime/layout tests plus ten JUCE product/state/accessibility/regression tests.

Manual equivalent:

```bat
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File plugins\juce\tests\SourceContractTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_release_notes.ps1
```

## macOS development

The `macos-universal-release` preset configures the same JUCE implementation for
arm64 and x86_64. For a macOS port, validate the generated formats in
native hosts, exercise state migration and the complete capture/correct/freeze
workflow, and perform VoiceOver keyboard/accessibility checks.

```sh
cmake --preset macos-universal-release
cmake --build --preset macos-universal-release --parallel
ctest --preset macos-universal-release --output-on-failure
```

### Correction automation and VoiceOver regression checks

The restored correction automation flags are shared by the Windows plug-ins and
Victor Tsaran's macOS AU, VST3, and CLAP port. `BUILD_MACOS.sh` and the macOS CI
job already run `ToneTraceProductCatalogTests` and `ToneTraceProcessorStateTests`,
which check the exposed controls and the structural controls that remain excluded.
No separate macOS parameter implementation or accessibility rewrite is required.

For manual verification on a Mac with VoiceOver:

- In each available format, verify Maximum Correction, Strength, Range Low/High,
  Q / Sharpness, and Full Correction Range appear in the host parameter interface.
  In REAPER with OSARA, verify they can be reached and adjusted through its effects
  parameter interface as well as through the plug-in editor.
- Confirm the editor retains its spoken control names, roles, values, keyboard
  focus order, page/band navigation, and status announcements. Check adjustment
  and exact-value entry, then close and reopen the editor and restore a project.
- With a learned correction, adjust the restored controls during playback and
  verify that the correction updates without restarting capture. These changes
  use background rebuilds and are not sample-accurate; let static settings settle
  before offline rendering.
- Confirm Match Mode, Resolution, and Workflow Step remain outside host automation
  while their existing editor controls remain accessible with VoiceOver.

Passing automated tests or AU validation does not establish VoiceOver behavior;
record the macOS version, host, format, and manual results before claiming it was
verified on a Mac. Preserve the existing accessibility handlers and keyboard
behavior when making follow-up changes.

## Linux verification

Linux intentionally verifies the portable engine/tool layer only. It does not
compile a second plug-in wrapper and should not be interpreted as plug-in release
coverage.

```sh
cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release --output-on-failure
```

## Release acceptance

Windows CI runs pluginval 1.0.4 at strictness 7 (seed 42712, including GUI tests)
and clap-validator 0.4.1 build 152b982 against the staged VST3 and CLAP. Downloads
are pinned by URL and SHA-256. Both validators run even if the other fails, and
their output and invocation records are uploaded as `windows-validator-reports`.
Crashes, timeouts, failed tests, and unexpected warnings fail CI. The CLAP denormal
timing warning remains visible in the logs and job summary but is advisory on
shared runners. Unsupported-feature skips are reported. Validation is serial.

To run the same checks against a local staged package, use PowerShell:

```powershell
./tools/validate_windows_package.ps1 -Validator pluginval -StageDirectory dist/ToneTrace_EQ_1.3.0_Windows_x64
./tools/validate_windows_package.ps1 -Validator clap-validator -StageDirectory dist/ToneTrace_EQ_1.3.0_Windows_x64
```

Complete real-host listening, project save/reload, duplicate/moved instance checks,
offline render checks, and screen-reader acceptance for a release. See
`RELEASING.md` and `plugins/README.md`.
