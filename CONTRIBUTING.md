# Contributing to Tone Trace EQ

Bug reports and focused pull requests are welcome. By contributing, you agree
that your contribution is distributed under the repository's `LICENSE`.

## Report a bug

Include:

- operating system and architecture;
- host name and version;
- exact Tone Trace version;
- the workflow step and minimal reproduction;
- whether the custom editor or host parameter view was used;
- for accessibility issues, the screen reader and bridge (for example NVDA and
  OSARA) plus the spoken result you expected and received.

Do not attach private audio unless you have permission to share it. A synthetic
or short redacted fixture is preferable.

## Build and test

For the Windows build, dependencies, and release package, see [Build dependencies and packaging](#build-dependencies-and-packaging).

Portable developer verification with a C++20 compiler:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Pull-request requirements

- Keep DSP and state logic in the shared engine/core. The JUCE plug-in is the
  single host integration path; do not add parallel wrappers that duplicate
  matching, state, editor, or accessibility logic.
- Do not allocate, lock, perform file I/O, or analyze on the audio thread.
- Preserve parameter IDs and serialized-state compatibility.
- Preserve the released 1.0.5 behavior and CLAP parameter contract in both formats.
- Visual editor changes must preserve accessible control roles, accessible names,
  tab order, keyboard behavior, and concise announcements.
- Add or update tests and documentation for user-visible behavior.
- Run all registered tests before requesting review.

Formatting-only rewrites should be separate from functional changes so audio,
state, and accessibility behavior remains reviewable.

## Build dependencies and packaging

The Windows package already contains built VST3 and CLAP plug-ins; this section
is for building from a source checkout. Windows builds require JUCE 8.0.12 and clap-juce-extensions commit
`55525c9858d4b25687be7759a5e0f70eccef218e`. CMake fetches these on the first build;
Git and network access are required. For an offline build, supply `JUCE_DIR`
and `CLAP_JUCE_EXTENSIONS_DIR` pointing to complete local checkouts (including
the adapter's CLAP and clap-helpers submodules). Dependency versions and local
compatibility adjustments are described in [plugins/README.md](plugins/README.md).

### Windows

Requirements are CMake 3.25 or newer, Git, Visual Studio 2022 Build Tools with **Desktop development with C++**, and a Windows SDK. Run:

```bat
build_all_windows.bat
```

The builder starts clean, compiles the plug-in plus its verification targets,
runs every registered CTest, stages both plug-ins and documentation, writes an
internal SHA-256 build manifest, checks that the stage contains exactly one CLAP
and one complete VST3 bundle, creates the ZIP, and lists the archive to verify
readability. It also prints the ZIP SHA-256. It does not install or replace
plug-ins on the machine.

### Other platforms

`CMakePresets.json` contains a macOS universal preset that uses the same JUCE
processor/editor source as Windows. The Linux preset is deliberately engine-only
verification; it does not build a substitute plug-in wrapper. No macOS/Linux
binary release is claimed until those builds receive host, validator, listening,
and accessibility acceptance on the target platform.

There is one supported plug-in implementation in this repository: `plugins/juce`.
VST3 and CLAP are produced from that shared implementation. This keeps future
platform work on one processor, editor, state path, and accessibility surface.

## Reusable developer tools

Tone Trace's source contains more than the plug-in itself. A fair amount of reusable infrastructure grew around accessibility, regression testing, visual validation, and repeatable builds. If any of it makes another audio plug-in easier to test or more accessible, please study it and adapt the ideas to your own project. Some of the most useful parts of Tone Trace may be things users never see.

In particular, the JUCE tree includes keyboard- and screen-reader-oriented controls, focus and paging tests, project-state round-trip tests, source-contract checks, and a visual snapshot harness that renders the real editor offscreen at representative sizes. The accessibility code is included as working examples rather than a framework, and the visual harness is deliberately based on JUCE rather than Win32 so it can remain useful to future non-Windows work.

We benefited from being able to inspect mature open-source audio projects while building Tone Trace. Keeping these pieces readable is one way of returning that favor.

## Design and release documentation

- [Matching engine and state design](DESIGN.md)
- [Editor and accessibility design](GUI_DESIGN.md)
- [Plug-in implementation](plugins/README.md)
- [Testing](TESTING.md)
- [Release procedure](RELEASING.md)
- [Security reporting](SECURITY.md)

## Dependency licensing

JUCE, the CLAP adapter, and bundled third-party code retain their own licenses.
The package includes dependency notices under `licenses/`. The project license
does not replace the separate JUCE licensing terms. The intended licensing basis
for the official port is JUCE Starter, subject to its eligibility rules. See
[JUCE 8 terms](https://juce.com/legal/juce-8-licence/); anyone rebuilding or
distributing a derivative must meet the applicable JUCE terms themselves.
