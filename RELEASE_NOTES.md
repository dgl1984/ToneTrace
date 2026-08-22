# Tone Trace EQ 1.0.1 release notes

Tone Trace EQ 1.0.1 is a Windows CLAP maintenance release. It keeps the 1.0.0
state, keyboard, and screen-reader contracts while correcting band-fader
dragging for Windows precision touchpads and improving Voice-mode safety.

## Fixed

- Band faders now respond reliably to tap-and-drag gestures from Windows
  precision touchpads.
- The editor's window, text, controls, and minimum size now scale together in
  hosts that provide a Windows display scale.
- The first audio block can no longer overlap initial correction preparation,
  and a failed or oversized realtime correction leaves the last good state
  alone.
- Enter now applies typed Match-page values, and Copy Curve Description copies
  the complete text.
- Automated testing now covers captured-pointer dragging in the Windows editor
  control, host-provided scaling before and after attachment, and the first
  audio-block handoff.
- The 20 kHz label now remains fully visible at the right edge of the Match
  graph.
- Voice mode now catches very short ringing that could slip past its existing
  safeguard. Normal Voice matches and all other modes remain unchanged.
- Temporary recording memory is released when the host deactivates Tone Trace.
  Saved corrections still restore normally when it is activated again.

## Release asset

The release has one binary asset:

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

- Complete 64-bit Windows release build.
- Automated tests for matching, real-time playback, saved projects, layout,
  and host communication.
- Windows editor-control tests for captured-pointer dragging, scaled sizing and
  fonts, typed-value commits, and pointer release.
- Automated layout checks across Match and the Bands tabs, including changing
  band counts and balanced page ranges.
- Package-content verification after ZIP creation.
