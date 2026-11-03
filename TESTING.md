# Building and testing Tone Trace 1.2 from source

`VERSION` is the machine-readable package version (`1.2.0`). The public release
name is `1.2`. The source tree contains one plug-in implementation under
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

Automated tests do not replace pluginval/clap-validator, real-host listening,
project save/reload, duplicate/moved instance checks, offline render checks, or
screen-reader acceptance. See `RELEASING.md` and `plugins/README.md`.
