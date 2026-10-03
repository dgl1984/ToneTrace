# Tone Trace JUCE editor

The Windows VST3 and CLAP formats share one editor. Released 1.0.5 behavior is
the reference; the framework and layout have changed, not the matching features.
The legacy Win32 editor remains in `plugins/clap` for reference.

The Capture page contains the four workflow actions, Match Mode, Options,
import/export, Status, Curve Description, and Copy Curve Description. The
Correction page contains the global numeric and safety controls. Bands pages
use balanced groups of at most ten faders, each with a frequency label and an
exact dB value. Resolution is also available on each Bands page.

The graph, Curve Readout, and Trace Curve toggle remain available across pages.
Reference (warm) means the desired sound; Target (blue) means the sound being
corrected; Correction (thick light line) shows the tonal EQ. The graph is visual
only. Curve Description uses the released description engine; Curve Readout
provides exact frequency and curve values for pointer or keyboard navigation.

JUCE controls expose accessible names, roles, and values. Bands intentionally
provide a string Value interface with dB units rather than RangeValue, avoiding
percentage substitution. Values retain up to three meaningful decimals.
Adjacent frequency labels gain precision when rounding would duplicate a name.
Status telemetry is throttled while focused.

Left/Right navigates bands, including page boundaries; Up/Down changes the
selected band's final value by 1 dB, Page Up/Down by 6 dB, Home/End selects its
limits, and 0/N neutralizes it. Enter opens exact entry. T/F2 toggles Trace Curve.
Ctrl+Tab and Ctrl+Shift+Tab switch pages. Text entry and ordinary controls retain
their own editing/navigation keys. Wheel steps are 1 dB, or 6 dB with Shift;
double-click neutralizes a band. Range masking preserves the learned band grid.

Import/export and reset confirmations are accessible JUCE dialogs. Notification
and navigation tones are suppressed when disabled, bypassed, or offline. The
stopped-transport fallback preserves the released band-tone/page-sweep policy.

Automated layout and keyboard checks use the actual editor. Final host and
screen-reader verification must also exercise the packaged binaries.
