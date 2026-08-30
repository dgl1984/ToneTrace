# Tone Trace EQ 1.0.3 release notes

Tone Trace EQ 1.0.3 makes the band controls more consistent and adds
impulse-response export for curves you create by hand. The same dB value is now
reported everywhere, whether you are reading the editor, entering an exact
value, using NVDA, or using Narrator.

## What's new

- Band values now report the same dB value everywhere: in the visible control,
  exact-value field, NVDA, and Narrator. Values retain up to three meaningful
  decimal places.
- One- and six-dB keyboard adjustments preserve an existing fractional value.
- Narrator no longer substitutes percentages and now announces changes made
  with Up or Down.
- Band-page tab labels refresh immediately when Correction Resolution adds or
  removes pages.
- **Emergency Clip Guard** now appears immediately after **Correction Gain** in
  the visible layout and keyboard order.
- You can now export an impulse response from a curve created with the band
  controls or Correction Gain, even if Tone Trace has not learned a match. Tone
  Trace asks for confirmation before exporting a manually created curve.
  Learned matches export normally without this warning, while a completely
  flat unmatched instance still reports that no curve is available.

## Download

Download `ToneTrace_EQ_1.0.3_Windows_x64.zip` from this release. It contains the
compiled Windows x64 CLAP plug-in, documentation, licenses, and a per-file
SHA-256 build manifest.

## Install on Windows

1. Extract the ZIP.
2. Copy `plugins/clap/Tone Trace EQ.clap` to
   `C:\Program Files\Common Files\CLAP\`.
3. Rescan CLAP plug-ins in your host.

Administrator permission is normally required for the system-wide CLAP folder.
Removing the copied `.clap` file uninstalls Tone Trace.
