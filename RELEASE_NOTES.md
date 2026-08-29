# Tone Trace EQ 1.0.2 release notes

Tone Trace EQ 1.0.2 makes band editing faster, improves screen-reader support,
and tightens the Windows editor layout.

## What's new

- New plug-in instances now start in **Voice** Match Mode.
- Bands can be edited before recording a Reference or Target, so Tone Trace can
  be used immediately as a standalone graphic EQ.
- The mouse wheel moves a band by 1 dB per notch. Shift+wheel moves it by 6 dB,
  matching Page Up and Page Down.
- Hovering over the Match graph shows the frequency beneath the pointer.
- The curve summary now has the correct **Curve Description** screen-reader
  label.
- The minimum editor size is now 740 x 500 at 100% display scaling, giving the
  value fields and band faders more usable room.
- The Trace Curve range label no longer overlaps the TRACE badge.
- New Windows regression tests cover keyboard order, mouse interaction,
  accessible band controls, and layouts at 100%, 125%, and 150% scaling.

## Download

Download `ToneTrace_EQ_1.0.2_Windows_x64.zip` from this release. It contains the
compiled Windows x64 CLAP plug-in, documentation, licenses, and a per-file
SHA-256 build manifest.

## Install on Windows

1. Extract the ZIP.
2. Copy `plugins/clap/Tone Trace EQ.clap` to
   `C:\Program Files\Common Files\CLAP\`.
3. Rescan CLAP plug-ins in your host.

Administrator permission is normally required for the system-wide CLAP folder.
Removing the copied `.clap` file uninstalls Tone Trace.
