# Tone Trace EQ 1.0.3 release notes

Tone Trace EQ 1.0.3 makes band values consistent across the visible editor,
exact-value entry, NVDA, and Narrator, and adds impulse-response export for
curves created manually without a learned match.

## What's new

- Every band value now uses the same unit-bearing dB text everywhere it is
  displayed, edited, or spoken, preserving up to three meaningful decimal
  places.
- One- and six-dB keyboard adjustments preserve an existing fractional value.
- Narrator now receives actual dB values instead of percentages and announces
  the updated value after Up or Down; NVDA retains its direct dB value path.
- Band-page tab labels refresh immediately when Correction Resolution adds or
  removes pages.
- **Emergency Clip Guard** now appears immediately after **Correction Gain** in
  the visible layout and keyboard order.
- A manually created curve can be exported as an impulse response before any
  match has been learned. Tone Trace asks for confirmation first; a completely
  flat unmatched instance retains the existing no-match error.

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
