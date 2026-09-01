# Changelog

All notable Tone Trace EQ changes are recorded here. Versions follow semantic
versioning, and release dates use `YYYY-MM-DD`.

## [1.0.4] - 2026-09-01

### Added

- **Custom Max Capture** can now retain up to about 60 seconds of accepted
  audio per Reference or Target. Standard modes retain their 30-second limit.
- Added a compact **Options...** dialog to the Windows editor for Full
  Correction Range, tone settings, Bypass, and Reset.
- Added an improved multiline **Status** display that keeps workflow state,
  Capture Time, confidence, Curve Drift, and the last action together.

### Improved

- **Curve Description** now uses clearer, frequency-specific language.
  Reference and Target measurements are described as higher or lower, while
  boost and cut terminology is reserved for the EQ correction Tone Trace
  actually applies.
- Curve descriptions now report when **Maximum Correction** has limited a
  larger learned correction.
- Returning to **Learn Target** after Preview or Freeze now allows the Target
  to be captured again without throwing away the existing Reference.
- Tone Trace keeps the previous known-good correction available while a new
  capture or correction is being prepared.

### Fixed

- Fixed edge cases where REAPER users with OSARA could not use the FX
  parameters dialog to move to the next step in the match process.
- Stopping playback no longer causes Tone Trace to forget a captured Reference
  or Target.
- Fixed cases where Tone Trace could unnecessarily require a full capture
  before allowing the workflow to continue.
- Improved keyboard navigation through the new Status display and reduced
  unnecessary repeated status announcements for screen-reader users.
- The Curve Readout now explains its purpose when a new instance opens instead
  of appearing as a blank read-only field.
- Improved reliability when changing Correction Resolution or moving between
  capture and correction stages.

## [1.0.3] - 2026-08-30

### Added

- Impulse-response export now supports curves created manually with the band
  controls or Correction Gain before a match has been learned. Tone Trace asks
  for confirmation before exporting a manual curve. A learned match exports
  without the warning, while a completely flat unmatched instance retains the
  existing error.
- Correction Resolution is now available through a labeled native control on
  the Bands pages while remaining available through the host parameter view.

### Fixed

- Band values now report the same dB value in the painted control, exact-value
  editor, readout, NVDA, and Narrator, retaining up to three meaningful decimal
  places instead of losing detail after 0.1 dB.
- Narrator no longer reports band values as percentages and now announces the
  updated dB value after Up or Down.
- One- and six-dB keyboard adjustments preserve the existing fractional value.
- Band-page tabs now keep complete **Bands N-M** names at every resolution,
  redraw immediately when Resolution changes, and refresh cached screen-reader
  labels at the same time.
- Nearby high-resolution bands no longer share the same rounded frequency
  label.
- Match Mode now has a visible native label, and Bands pages use the lower area
  previously reserved for hidden Match-page controls.
- Emergency Clip Guard now appears immediately after Correction Gain, with its
  visible label aligned to the correct value field and keyboard position.

## [1.0.2] - 2026-08-29

### Added

- Holding Shift while turning the mouse wheel over a band fader now moves 6 dB
  per notch, the same coarse step as Page Up / Page Down; the plain wheel keeps
  its 1 dB step.
- Hovering the Match graph now shows the frequency under the cursor in a small
  label beside the cursor line, matching the existing readout below the graph.
- Added a Windows UI regression harness for the known-good keyboard order,
  pointer gestures, native band controls, and minimum-size layout at 100%,
  125%, and 150% display scales.

### Fixed

- New plug-in instances now start in Voice Match Mode. Saved projects continue
  to restore the mode stored in their project state.
- The read-only curve summary now has a real native **Curve Description** label
  instead of allowing NVDA to borrow an unrelated neighboring label.
- Bands can now be edited as a standalone graphic EQ before a Reference/Target
  match exists; the native editor no longer masks those manual values at 0 dB.
- The editor's declared minimum is now 740 x 500 at 100% scale. This keeps the
  seven Match-page value fields at their existing 64 px minimum width and leaves
  about 66 px of usable painted fader travel on Bands pages.
- In Trace Curve mode the graph's +/- dB range label no longer overdraws the
  TRACE badge in the top corner.

## [1.0.1] - 2026-08-22

### Fixed

- Fixed band sliders not moving reliably with laptop trackpads.
- Made the window and its text scale together, so hosts using Windows display
  scaling do not crowd or clip the controls.
- Kept the 20 kHz label inside the Match graph so the Curve Description panel
  cannot cover it.
- Tightened the handoff between the first audio block and a newly prepared
  correction, and kept an unusually long or failed correction from disturbing
  the last one that worked.
- Pressing Enter now applies a value typed into one of the Match-page boxes.
- Copy Curve Description now copies the whole description instead of relying
  on a fixed-size buffer.
- Improved Voice mode's protection against very short, ringy corrections while
  leaving normal Voice matches and every other mode unchanged.
- Released temporary recording memory when a host deactivates the plug-in;
  saved corrections still restore normally when it is activated again.

## [1.0.0] - 2026-08-21

### Added

- First stable CLAP release with the complete Reference → Target → Correct →
  Freeze workflow.
- Native Windows editor with NVDA/OSARA-friendly controls and concise band
  navigation.
- Editable graphic-EQ band pages, curve descriptions, trace tones, and curve,
  model, and impulse-response import/export.
- Validated project-state restore, zero reported correction latency, and
  real-time-safe correction swaps.
- Reproducible Windows x64 release builder and automated engine, renderer,
  layout, state, and CLAP-host tests.

### Release polish

- Added visible Match-graph hover tracking, pointer cursor feedback, mouse-wheel
  band adjustment, and clearer hover/focus/active visuals.
- Added numbered, visually grouped workflow buttons without changing their
  native button roles, accessible names, parameter routing, or tab order.
- Kept every action caption fully visible at the design size and made native
  tooltips register reliably with both legacy and current Windows Common
  Controls hosts.
- Reduced the public release to one ZIP containing the CLAP plug-in,
  documentation, licenses, and an internal build manifest.
