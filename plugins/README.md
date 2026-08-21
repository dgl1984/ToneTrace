# Tone Trace plug-in wrapper

Tone Trace EQ 1.0 ships one supported plug-in format: **CLAP**. It uses the shared `HeadlessPluginCore` for matching, validated state, and real-time correction rather than duplicating DSP inside the wrapper.

## CLAP implementation

The CLAP wrapper uses the official CLAP 1.2.10 ABI and supports stereo float/double audio, the complete 19-parameter global surface, state, zero reported latency in normal correction states, gated Reference/Target capture, confidence and status reporting, analysis, Preview, Freeze, Reset, import/export, IR export, and optional tone notifications.

On Windows it also ships the native Win32 editor designed for keyboard and screen-reader use. The per-band trace surface keeps native readonly edits for exact dB/NVDA reporting, but custom-paints those same controls as vertical graphic-EQ faders for sighted users and supports mouse dragging. Band pages use the full editor width, prefer at most 10 bands per page, and rebalance odd totals instead of leaving a tiny final tab. The default 30-band layout is 10 + 10 + 10; 60 bands is six predictable 10-band pages.

A mock CLAP host tests discovery, lifecycle, host-event and `params.flush` delivery, audio, accessibility text, partial streams, restoration, full-capture fallback, and malformed state. A separate layout regression test protects the balanced paging rule.

CLAP headers are vendored under `third_party/clap` with the upstream license notice. Nothing is downloaded during a normal build.

## State and real-time rules

- The audio callback allocates and locks nothing in the shared renderer.
- Analysis and kernel preparation run off the audio thread.
- Ready, Preview, and Frozen report zero samples of latency.
- Host state snapshots serialize a validated last-known-good profile as Frozen, or Ready when none exists. Loading never resumes learning.
- Raw capture buffers are never serialized.
- Invalid, clipped, non-finite, incomplete, or hostile candidate/state data cannot replace the last-known-good correction.
- Reset is destructive only after Arm Reset followed by Confirm Reset.
- Capture uses the full analysis range; Range Low/High affect rendered correction only.
- The Emergency Clip Guard is a last-resort plug-in ceiling. Exported IRs remain linear and do not contain it.

## Host-specific release checks

Automated tests cannot establish every real host/OS behavior. Before publishing Windows binaries, verify REAPER discovery, project save/reload, duplicate instances, NVDA/OSARA navigation, native-editor controls, and the full Reference -> Target -> Correct -> Preview -> Freeze workflow with representative audio.

Builds stage portable artifacts only. Copying the CLAP file into a system or per-user plug-in directory remains a separate, explicit action.
