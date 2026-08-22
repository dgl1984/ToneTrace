# Changelog

All notable Tone Trace EQ changes are recorded here. Versions follow semantic
versioning, and release dates use `YYYY-MM-DD`.

## [1.0.1] - 2026-08-21

### Fixed

- Fixed band sliders not moving reliably with laptop trackpads.
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
