# Tone Trace JUCE editor

The Windows VST3 and CLAP formats share one editor and the unchanged released
1.0.5 DSP engine. Default size is 960x780; minimum is 720x640. Compact layouts
reduce spacing and row height without shrinking the type. Bypass is the same
accessible control on every page, with its existing focus order. The header
also shows workflow status and output gain. The recommended ready workflow
action receives visual emphasis; other commands retain their accessible names
and explanatory behavior.

The Capture page contains workflow actions, Match Mode, Options, transfers,
Status, Curve Description, and Copy Curve Description. Correction contains
numeric and safety controls. Bands use balanced groups of at most ten faders,
with frequency labels, a neutral-position mark, and exact dB values. Resolution
remains available on Bands pages. Bypass is always in the header. When tabs
would be too narrow, All pages opens the complete page menu; Down on the
selected tab opens the same menu. Left/Right and Ctrl+Tab still reach every page.
Invalid exact-entry messages have a dedicated row, never an overlay on controls.

The graph spans 10 Hz to 30 kHz with labeled frequency and dB axes. Its legend
identifies Reference (warm solid), Target (blue dashed), and Correction + gain
(thicker mint line). The shared auto scale is explicitly labeled. Reference and
Target show levels relative to each spectrum's mean; the pointer readout uses
the same normalization, with nearest measured spectrum values. Correction is
output dB including Correction Gain in both the plot and pointer readout. Trace
Curve continues to report/edit tonal band values separately. The graph is a
visual duplicate, outside the accessibility tree.

Graph paths and completed descriptions are cached until profile revision or
render inputs change. Pointer movement reuses the cached profile and paths.
Telemetry still updates Status and contextual reporting, without rebuilding
unchanged curves. Reference reports read an immutable published capture prefix;
ongoing audio can append while a report copies earlier samples. Reader pins
protect against reset and storage replacement. Capture storage is provisioned
on the control thread when capture is requested, not for idle/frozen instances.

Curve Description describes a ready Reference, then the finalized Target while
matching runs, then the completed Reference/Target/Correction relationship.
Reports describe measurements rather than artistic intent. The field retains
its name, copying behavior and keyboard position. Status changes are throttled
while focused. Band values expose a string Value interface with dB units;
adjacent frequency names remain distinct.

Left/Right navigates bands, Up/Down changes 1 dB, Page Up/Down changes 6 dB,
Home/End selects limits, and 0/N neutralizes. Enter opens exact entry. T/F2
toggles Trace Curve. Ctrl+Tab/Ctrl+Shift+Tab changes pages. Ordinary controls and
text entry retain their keys. Wheel steps are 1 dB or 6 dB with Shift;
double-click neutralizes. Notification tones are suppressed when disabled,
bypassed, or offline. Frozen navigation and stopped-transport policy are retained.

Tests render the actual editor at minimum/default/wide/maximum sizes in empty,
learned, and bypassed states, including nonzero gain, 120 bands, and invalid entry. They check focusable bounds,
names, order and overlap. These software renders do not replace packaged-binary
DAW, screen-reader, listening, or physical-DPI acceptance.
