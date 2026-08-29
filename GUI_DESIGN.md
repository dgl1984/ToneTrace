# Tone Trace EQ native GUI + accessibility design

Status: implemented for Tone Trace EQ 1.0. The Win32 editor remains a thin
accessible shell over the regression-tested CLAP parameter surface and shared
engine.

## Principles

1. **The CLAP parameter surface stays authoritative.** The GUI is a thin native
   shell. Every value it shows is read through `clap_plugin_params_t`; every
   edit it makes goes through the same set-value path the host uses. There is no
   second source of truth.
2. **Painted is not accessible.** Everything drawn on the canvas is visual only.
   The screen-reader surface is a set of native Win32 controls, most importantly
   a **readonly description box** that describes the curves in words.
3. **Keyboard is first-class.** Tab order follows workflow, arrows move the
   trace cursor, one explicit "Describe curves" command fills the description
   box, and `EVENT_OBJECT_VALUECHANGE` on that box lets NVDA announce it once.
4. **Silent by default.** Real-time painted elements (the live trace, meters)
   never announce continuously, matching the OptiLab meter doctrine.

## Canvas: the tone trace, drawn literally

The engine's `ProfileSnapshot` (reference capture, target capture, correction
model) is drawn on a log-frequency (20 Hz-20 kHz) by dB graph with a dark
background and readable light grid. The vertical display range adapts to the
material instead of hard-clipping every curve at +/-12 dB, and dedicated top
and bottom margins keep frequency labels and the legend out of the curves. Edge
labels remain inside the graph so neighboring panels cannot cover them.
Three curves:

- **Reference** curve: one hue, thin stroke. Identifies what the source tone
  *is*.
- **Target** curve: a second hue, thin stroke. Identifies what the tone should
  *become*.
- **Correction** curve: thick, high-contrast stroke derived from the model.
  It is the visual centerpiece, matching the professional convention that the
  result curve is the thickest line (Pro-Q yellow, Ozone white).

Line **thickness encodes prominence/role**; line **hue encodes identity**. This
is the plugin's own encoding, not a copy of any single product.

Canvas interactions:

- Mouse hover moves a vertical readout cursor; the exact frequency/dB of each
  curve at that point is shown in the status area (sighted).
- The pointer becomes a crosshair over the Match plot, and the hover cursor is
  visibly repainted as it moves instead of changing only the text readout.
- Arrow keys move the same cursor (blind + keyboard).
- The readonly Curve Description box contains a natural-language summary, e.g.: *"Reference rises from 100 Hz to 4 kHz with a 3 dB presence
  bump; target is 6 dB darker below 200 Hz; correction cuts 3 dB at 100 Hz,
  boosts 2 dB at 2 kHz, strongest in the low-mid region."*

## Accessibility surface (native controls)

| Control | Role | Purpose |
| --- | --- | --- |
| Curve description label | STATIC | Native accessible name for the description edit; never painted-only |
| Curve description box | READONLY multiline EDIT | Natural-language description of all curves; sole non-visual curve reading |
| Trace/readout box | READONLY EDIT; wraps visually on band pages | Exact Hz/dB on Match; concise band-page guidance and focused-band detail |
| Capture Reference / Learn Target / Correct / Freeze | BUTTONs | Workflow steps (mirror the CLAP WorkflowAction param); Learn Target saves the Reference first, then begins Target capture |
| Mode | COMBOBOX (named options) | Voice default; Full Mix / Voice / Drums / Bass+Synth / Custom |
| Range low / high, strength, resolution, correction ceiling, etc. | exact-value EDITs | Continuous params, committed on kill-focus |
| Phase / status | READONLY EDIT | "Capturing Reference", "Frozen", etc. |
| Legend rows | per-curve STATIC+color swatch | Names each curve for tabbing |

Rules from the OptiLab study applied here:

- Enumerated params → COMBOBOX with named items built from `value_to_text`.
- Continuous params → exact-value EDIT, committed on `EN_KILLFOCUS`, parsed
  with `text_to_value`, reverted to last valid value on parse failure.
- Every control has a real accessible name. Labels are native STATIC controls
  adjacent to what they name in Z-order and retain their complete text; painted
  captions are never treated as an accessibility surface.
- The painted canvas creates **no** accessible object; it is not a tab stop.
- The description box and readout announce via `NotifyWinEvent` on demand only.
- Band pages use the full editor width and prefer at most 10 bands per page. A
  non-multiple is rebalanced so there is no nearly empty final tab. The native
  tab control uses natural caption widths so its own scroll arrows never cover
  a visible tab label.
- Each band remains a native readonly EDIT with the same concise accessible
  text and the same single tab stop. Its custom paint presents that control as a
  vertical graphic-EQ fader with a visible 0 dB center mark, movable thumb,
  frequency label above, and exact dB readout below. Mouse click/drag edits the
  same underlying band; double-click centers it. The visual affordance therefore
  improves pointer use without creating a second accessibility surface or adding
  screen-reader verbosity.
- The mouse wheel adjusts a hovered/focused band in the same 1 dB steps as the
  arrow keys. Hover, focus, positive movement, and negative movement use
  distinct visual states.
- Workflow actions remain native push buttons but use owner-drawn numbering,
  grouping, hover, pressed, focus, and current-step states. Their HWNDs,
  accessible names, roles, commands, and tab order do not change.
- The action row allocates extra width to the longer workflow and Copy Curve
  Description captions while the short Export/Import actions donate unused
  space. Full visible captions therefore remain consistent with the native
  accessible names instead of being abbreviated or ellipsized.
- The learned band grid is stable. Correction Range Low/High never move or
  renumber band frequencies; they mask the effective correction symmetrically.
  Bands outside either boundary show/announce 0 dB while their learned values
  and manual trims remain stored for immediate restoration if the range reopens.
- Band-page guidance is deliberately concise so the complete keyboard help fits
  without clipping or forcing a long announcement. `0` centers the focused band
  at 0 dB; `N` remains as a compatible alias.
- Trace Curve is a push-like toggle beside the always-visible readout rather
  than a low-contrast checkbox floating in unused page space.
- The visible **Learn Target** caption stays short. Pointer users get a tooltip
  explaining that it saves the captured Reference and then begins Target
  capture, while the host generic Workflow Step keeps the explicit
  **Save Reference and Learn Target** wording. A committed Reference can be
  exported before a Target match is completed.
- Tooltip registration uses the Windows `TTTOOLINFOW_V2_SIZE` contract. This
  keeps all eight action/control tooltips available in hosts with either legacy
  Common Controls or a version 6 manifest.
- Static labels and readonly dark-panel text use explicit high-contrast colors;
  exact-value boxes retain a light edit background.

## Architecture (mirrors the reference implementation)

```
clap_plugin_t ── CLAP_EXT_GUI ──> ToneTraceWin32Editor
                                     │ holds clap_plugin_params_t* (read/write)
                                     │ + function-pointer bridge to instance:
                                     │   getSnapshot(), getParamInfo(), setParam()
                                     ▼
                              ToneTraceClap instance (owns HeadlessPluginCore)
```

- `guiCreate` builds the editor and hands it the params extension plus bridge
  callbacks (same shape as the reference editor). No audio-thread access.
- Editor redraws on a 30 Hz timer only while shown and realtime; the timer is
  stopped while offline/hidden so NVDA is never flooded.
- Resizable; the preferred size, minimum size, fonts, controls, and painted
  details all use the same host-provided `px()` scale. A scale change after the
  editor is attached rebuilds its fonts and lays the controls out again. The
  canvas keeps a stable aspect so curves do not distort on resize.

## Explicit non-goals

- No editing of the curve directly on the canvas in v1 (params control it).
- No state stored in the GUI; project state remains owned by the core.
- The GUI must not change DSP behaviour, automation contract, or state format.
- The whole editor compiles behind a build flag so the headless build stays
  bit-identical where a host never opens a window.
