# Tone Trace EQ 1.0.2 release notes

Tone Trace EQ 1.0.2 is a focused Windows CLAP editor and usability update. It
preserves the 1.0.1 matching DSP, capture workflow, realtime renderer, saved-
state format, parameter IDs/order, and established keyboard path.

## Added

- Holding Shift while turning the mouse wheel over a band fader moves 6 dB per
  notch, matching Page Up / Page Down. The plain wheel remains a 1 dB step.
- Hovering the Match graph shows the frequency under the pointer in a small
  visual label. The existing readout continues to provide the same frequency
  and curve values and remains the accessible source of that information.
- A Windows UI regression harness now checks the established Match-page tab
  order, band keyboard and mouse interaction, native-control roles, and layout
  at 100%, 125%, and 150% display scales.

## Fixed

- A new plug-in instance now starts in **Voice** Match Mode. Existing projects
  still restore their saved mode.
- The natural-language summary edit now exposes **Curve Description** as its
  native accessible name instead of inheriting an unrelated nearby label.
- Manual band edits now work before any Reference/Target profile has been
  learned, so the Bands pages can function immediately as a standalone graphic
  EQ as documented.
- The declared minimum editor size is now 740 x 500 at 100% display scale. The
  width preserves the existing 64 px minimum for all seven Match-page value
  fields instead of squeezing them narrower, while the height leaves useful
  pointer travel on the vertical band faders.
- In Trace Curve mode the graph's +/- dB range label no longer overlaps the
  TRACE badge.

## Deliberately unchanged

This update does not change the Tone Trace matching DSP, capture workflow,
stopped-transport command handling, tone-worker behavior, saved-state format,
parameter IDs/order, or the existing REAPER/NVDA focus and keyboard path. The
only parameter-default change is the intentional Full Mix-to-Voice starting
mode described above.

## Release asset

The release has one binary asset:

`ToneTrace_EQ_1.0.2_Windows_x64.zip`

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

## Validation before publication

The 1.0.2 package should not be published until the exact Windows build passes:

- all registered CTest tests, including the Win32 UI regression harness;
- a REAPER/NVDA smoke test confirming the description field is announced as
  **Curve Description** and the established keyboard behavior is unchanged;
- a fresh-instance check confirming Match Mode starts in Voice and a saved
  Full Mix project still restores as Full Mix;
- a pre-capture band edit confirming Up changes a band to +1.0 dB and `0`
  returns it to neutral;
- mouse or precision-touchpad click, drag, wheel, Shift+wheel, and double-click
  checks on several band faders;
- a visual check at default size and the declared minimum at 100%, 125%, and
  150% display scaling;
- package-content and build-manifest verification.
