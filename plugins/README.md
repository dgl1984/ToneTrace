# Tone Trace plug-in implementation

`plugins/juce` is the only supported plug-in implementation. VST3 and CLAP share
the same processor, editor, state handling, accessibility controls, and frozen
1.0.5 matching/realtime engine from the repository root. There is no alternate
CLAP wrapper or platform-specific plug-in implementation to keep in sync.

Tone Trace EQ 1.2 supports Windows x64. The macOS preset uses the same JUCE implementation. Native host checks, plug-in validation, listening tests, and VoiceOver checks are part of porting it to macOS.

The port retains all 19 released global parameters, CLAP numeric IDs, and CLAP
identity `com.lanesaudio.tonetrace-eq`. JUCE supplies VST3 integration and the
accessible editor. The pinned CLAP adapter is patched at configure time into
the build directory for parameter IDs, flags, text precision, state loading,
host rescan notification, stream validation, and stopped-transport tone reset.
Upstream dependency checkouts are left untouched.

## State and processing

Project state contains validated correction data and manual bands, not raw live
capture. Loading restores Frozen or Ready, never resumes learning. The CLAP
loader accepts released state versions 1–3 and converts them to the JUCE state;
save with 1.2 is one-way, so retain project backups for rollback to 1.0.5.
VST3 uses its own host identity and cannot replace CLAP instances automatically.

Same-rate stop/restart retains raw capture. Sample-rate changes clear raw capture
and rerender retained profiles. Host reset clears audio history without deleting
the correction. Background workers perform analysis and filter preparation;
the audio path uses the released realtime renderer with a separate smoothed
Correction Gain, bypass crossfade, and clip guard. Reported latency remains zero.

## Verification

CTest covers the original engine/realtime suites plus capture, controller,
catalog, keyboard, state, output bypass, notification tones, and layout tests.
The snapshot utility renders the real JUCE editor at representative sizes.
Run pluginval against VST3 and clap-validator against CLAP after building.

For release validation, check both packaged formats in REAPER: discovery, playback,
Reference -> Target -> Correct -> Preview -> Freeze, imported Reference with live
Target, manual bands, save/reload, duplicate/moved instances, stop/restart,
bypass, offline export, and import/export. Check Tab order, exact entry, band
arrows, Trace Curve, page switching, and spoken dB values with NVDA/OSARA and
Narrator. These human host/screen-reader checks are not replaced by validators.

The build stages portable artifacts only; it does not install them into a DAW.

### Reference-only spectrum report

Capture a short Reference until it is ready, then stop playback before Capture
Full. Confirm Curve Description contains the Reference spectrum, including
measured frequency features, and Copy Curve Description copies the same text.
Leave Workflow Step at Capture Reference. Resume with different audio and
confirm the report refreshes; stop again and confirm it remains readable.
Repeat with a full capture, then choose Learn Target and confirm the Reference
report remains available. Check keyboard reading and NVDA in both formats.
