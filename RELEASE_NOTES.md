# Tone Trace EQ 1.0.4 release notes

Tone Trace EQ 1.0.4 improves the Reference-to-Target workflow in REAPER, adds a longer Custom Max Capture option, and makes the Windows editor easier to read and navigate without changing the released matching engine.

## What is new

- **Custom Max Capture** can retain up to about 60 seconds of accepted audio per Reference or Target. Standard modes retain their 30-second limit. These limits are maximum capacities, not capture lengths you must reach.
- The Windows editor now has a compact **Options...** dialog for Full Correction Range, tone settings, Bypass, and Reset.
- The Match page now has an improved multiline **Status** display that keeps workflow state, Capture Time, confidence, Curve Drift, and the last action together.

## Workflow and accessibility improvements

- Fixed edge cases where REAPER users with OSARA could not use the FX parameters dialog to move to the next step in the match process.
- Stopping playback no longer causes Tone Trace to forget a captured Reference or Target. This makes it practical to stop after the Reference, prepare the Target, resume playback, and continue the match.
- A usable Reference can continue to Learn Target without filling the entire capture buffer.
- Returning to **Learn Target** after Preview or Freeze keeps the valid Reference and starts a fresh Target capture.
- Tone Trace keeps the previous known-good correction available while a replacement capture or correction is being prepared.
- Keyboard navigation through the multiline Status display has been tightened, and fast Capture Time / Curve Drift updates are restrained while the field has focus so screen readers are not flooded with telemetry changes.
- The Curve Readout now identifies itself and explains how to inspect exact values when a new instance opens instead of appearing as a blank read-only field.
- Reliability has been improved when changing Correction Resolution or moving between capture and correction stages.

## Curve descriptions

- **Curve Description** now uses clearer, frequency-specific language. Reference and Target measurements use **higher** and **lower** to describe their spectral relationship. **Boost** and **cut** are reserved for the actual EQ correction Tone Trace applies.
- When **Maximum Correction** limits a larger learned correction, the description reports both the learned and applied values.

## Download

Download `ToneTrace_EQ_1.0.4_Windows_x64.zip` from this release. It contains the compiled Windows x64 CLAP plug-in, documentation, licenses, and a per-file SHA-256 build manifest.

## Install on Windows

1. Extract the ZIP.
2. Copy `plugins/clap/Tone Trace EQ.clap` to `C:\Program Files\Common Files\CLAP\`.
3. Rescan CLAP plug-ins in your host.

Administrator permission is normally required for the system-wide CLAP folder. Removing the copied `.clap` file uninstalls Tone Trace.
