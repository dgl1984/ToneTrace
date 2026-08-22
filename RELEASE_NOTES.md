# Tone Trace EQ 1.0.1 release notes

Tone Trace EQ 1.0.1 is a Windows CLAP maintenance release. It keeps the 1.0.0
state, keyboard, and screen-reader contracts while correcting band-fader
dragging for Windows precision touchpads and improving Voice-mode safety.

## Fixed

- Band faders now respond reliably to tap-and-drag gestures from Windows
  precision touchpads.
- Automated testing now covers that touchpad interaction in the real Windows
  editor control.
- Voice mode now catches very short ringing that could slip past its existing
  safeguard. Normal Voice matches and all other modes remain unchanged.
- Temporary recording memory is released when the host deactivates Tone Trace.
  Saved corrections still restore normally when it is activated again.

## Release asset

Publish exactly one binary asset:

`ToneTrace_EQ_1.0.1_Windows_x64.zip`

The ZIP contains:

- `plugins/clap/Tone Trace EQ.clap` — the plug-in binary;
- `docs/` — README, user manual, design notes, changelog, release notes, and the
  per-file SHA-256 build manifest;
- `LICENSE` — Tone Trace's source-available license;
- `licenses/clap/LICENSE` — the vendored CLAP SDK license.

It intentionally contains no test executables, command-line tools, project
files, debug symbols, or installer. The builder does not install the plug-in.

## Install on Windows

1. Extract the ZIP.
2. Copy `plugins/clap/Tone Trace EQ.clap` to
   `C:\Program Files\Common Files\CLAP\`.
3. Rescan CLAP plug-ins in the host.

Administrator permission is normally required for the system-wide CLAP folder.
Removing the copied `.clap` file uninstalls Tone Trace.

## What to expect

- CLAP instrument/effect hosts receive a stereo audio effect with 19 global
  parameters, project-state support, and zero reported latency after learning.
- The custom native editor is Windows-only. Its graph and faders are visual
  supplements to the same native control and CLAP parameter surface used by
  keyboard and screen-reader users.
- Large learned boosts still require headroom. The Emergency Clip Guard is a
  safety ceiling, not a mastering limiter, and exported IRs do not contain it.

## Validation performed

- Release x64 compilation with Visual Studio 2022 and the static MSVC runtime.
- Engine, real-time renderer, UI-layout, state, and mock CLAP-host tests.
- Real Win32 editor regression coverage for captured precision-touchpad
  dragging.
- Native-editor render and mouse-interaction checks across Match and every
  Bands tab, including complete action captions, hover/focus feedback,
  tooltips, graph readout tracking, and 1 dB mouse-wheel adjustment.
- Package-content verification after ZIP creation.

Before publication, complete the real-host checklist in
`plugins/README.md`, especially REAPER discovery, save/reload, duplicate
instances, NVDA/OSARA navigation, and the full capture workflow.
