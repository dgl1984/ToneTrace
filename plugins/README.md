# Tone Trace plug-in wrapper

Tone Trace EQ 1.0 ships one supported plug-in format: **CLAP**. It uses the shared `HeadlessPluginCore` for matching, validated state, and real-time correction rather than duplicating DSP inside the wrapper.

## CLAP implementation

The plug-in uses the official CLAP 1.2.10 interface and supports stereo audio, the complete 19-parameter control surface, project save and restore, zero reported latency during normal corrected playback, gated Reference/Target capture, confidence and status reporting, Preview, Freeze, Reset, import/export, matched or manual-only impulse-response export, optional tone notifications, standard 30-second accepted-audio capture, and 60-second Custom Max Capture.

On Windows it also ships the native Win32 editor designed for keyboard and screen-reader use. Each band uses one dedicated custom-fader HWND for keyboard, pointer, MSAA, and UI Automation access. Its public value is a unit-bearing dB string with up to three meaningful decimal places; UI Automation deliberately uses the string Value pattern so Narrator does not normalize the correction to a percentage. Band pages use the full editor area, prefer at most 10 bands per page, and rebalance odd totals instead of leaving a tiny final tab. Correction Resolution is available in a labeled Bands-page combo as well as through the host parameter view. Live changes rebuild complete **Bands N-M** tab names, and nearby center-frequency labels remain distinct. The default 30-band layout is 10 + 10 + 10; 60 bands is six predictable 10-band pages.

A mock CLAP host tests discovery, lifecycle, host-event and `params.flush` delivery, audio, accessibility text, fresh-instance defaults, partial streams, restoration, full-capture fallback, and malformed state. Windows regressions also protect the labeled native controls, complete tab names, distinct band-frequency names, band controls, focus across page rebuilds, and the balanced paging rule.

CLAP headers are vendored under `third_party/clap` with the upstream license notice. Nothing is downloaded during a normal build.

## State and real-time rules

- The audio callback allocates and locks nothing in the shared renderer.
- Analysis and correction-filter preparation run off the audio thread.
- Ready, Preview, and Frozen report zero samples of latency.
- Host state snapshots serialize a validated last-known-good profile as Frozen, or Ready when none exists. Loading never resumes learning.
- Raw captured audio is never stored in the project. A same-sample-rate deactivate/reactivate preserves an in-progress raw Reference or Target in wrapper memory so transport Stop cannot behave like Reset; a real sample-rate change clears raw capture.
- Invalid, clipped, incomplete, or damaged capture and project data cannot replace the last-known-good correction.
- The host exposes one persistent Workflow Step containing Capture Reference through Cancel Reset. Small directional fractional host moves are snapped to the adjacent enum step for REAPER/OSARA compatibility. Reset remains protected by Arm/Confirm/Cancel within that control; the native Options dialog uses a standard confirmation before requesting the same destructive reset.
- Capture uses the full analysis range; Range Low/High affect rendered correction only. Standard modes stop accepting audio at about 30 seconds; Custom Max Capture stops at about 60 seconds.
- The Emergency Clip Guard is a last-resort plug-in ceiling. Exported IRs remain linear and do not contain it.

## Host-specific release checks

Automated tests cannot establish every real host/OS behavior. Before publishing Windows binaries, verify REAPER discovery, project save/reload, duplicate instances, NVDA/OSARA navigation, native-editor controls, and the full Reference -> Target -> Correct -> Preview -> Freeze workflow with representative audio.

Builds stage portable artifacts only. Copying the CLAP file into a system or per-user plug-in directory remains a separate, explicit action.
