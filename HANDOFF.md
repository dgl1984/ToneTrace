# Tone Trace 1.2 source handoff

Prepared November 2, 2026. This is a source-only handoff intended for Windows maintenance and macOS porting. Start with `README.md`, `TESTING.md`, and
`plugins/README.md`.

## Canonical architecture

- `plugins/juce` is the only plug-in implementation. VST3 and CLAP share one
  processor, editor, state model, accessibility layer, and correction controller.
- `include/` and `src/` contain the single canonical copy of the frozen 1.0.5
  matching/realtime engine. The JUCE build consumes those files directly.
- `plugins/juce/tests/SourceContractTests.ps1` pins the frozen engine and important
  behavior contracts. Do not change those hashes casually.
- The old independent CLAP/Win32 wrapper, its private CLAP SDK copy, and its
  wrapper-only tests have been removed so there is no alternate implementation
  for a porter to mistake as current code.

## Platform status

Tone Trace EQ 1.2 supports Windows x64. Build metadata and package filenames use `1.2.0`, the value in `VERSION`.

The macOS universal preset now selects the same JUCE implementation rather than
a nonexistent/fallback wrapper. This makes it a valid starting point for porting,
but no macOS binary, host, validator, listening, or VoiceOver acceptance is claimed
by this source handoff. Linux is explicitly engine-only verification.

## Windows package verification

When verifying a Windows package, check the following items on the packaged build.

1. all registered Windows CTest suites pass;
2. `plugins/juce/tests/SourceContractTests.ps1` passes;
3. `tools/verify_release_notes.ps1` passes;
4. staged-package verification passes;
5. pluginval and clap-validator pass on the exact packaged binaries;
6. the full Reference -> Target -> Correct -> Preview -> Freeze workflow passes
   in the normal host configuration;
7. project save/reload, duplicate/moved instances, stop/restart, bypass, offline
   render, and import/export are checked;
8. NVDA/OSARA and Narrator keyboard/accessibility acceptance is completed.

## Accessibility/platform rule

Spoken announcements must use the platform accessibility system, never a speech
synthesizer. Windows has an isolated UI Automation path; Apple builds use JUCE's
native accessibility announcement path. Preserve keyboard operation, control names,
roles, focus order, spoken dB values, and exact-entry behavior while porting.

## Build hygiene

Generated builds, dependency checkouts, binaries, logs, screenshots, and distribution
ZIPs are not source. Keep them out of handoff archives. Do not add a second format- or
platform-specific processor/editor implementation; platform adaptations should stay
behind small adapters or build configuration wherever possible.
