# Tone Trace EQ User Manual

If you already know how to use an EQ, you already understand the problem Tone Trace is trying to solve. With a traditional EQ, you hear a tonal difference and decide which bands to move. Tone Trace lets two recordings describe that difference for you.

Give it a **Reference**—the sound you want—and a **Target**—the sound you want to correct. Tone Trace learns the tonal relationship between them and builds an EQ correction that moves the Target toward the Reference.

The unusual part is what happens next. Tone Trace does not treat the generated match as a sealed result. The correction remains something you can audition, reshape, narrow to the frequencies that matter, edit like a graphic EQ, or reinterpret with another Match Mode without recording the pair again. Once learned, the correction can remain in a live signal path with zero reported plug-in latency, or be exported as an impulse response for use elsewhere.

This manual starts with the practical workflow. The more technical details are kept for the sections where they help explain a control or behavior rather than being required knowledge up front.

Version covered: **1.1.0 for Windows**, available as VST3 and CLAP.

## The one distinction to get right

Tone Trace uses these names in a specific way:

- **Reference** = the sound you want to match.
- **Target** = the sound you want to correct.

If a dull microphone should sound more like a brighter microphone, the brighter recording is the **Reference** and the dull recording is the **Target**. Reversing the captures reverses the EQ.

## Quick start

1. Insert **Tone Trace EQ** on the track or bus that will receive the audio you are capturing.
2. Confirm the **Match Mode**. A new instance starts in **Voice**; choose a
   different mode when the material calls for it.
3. Choose **Capture Reference** and play the sound you want the Target to resemble.
4. Choose **Learn Target**. This first saves the Reference, then starts Target capture. In a host generic parameter view, the same step is named **Save Reference and Learn Target**. If you only want to save a reusable Reference, it is ready to export at this point.
5. Play the sound you want to correct.
6. Choose **Correct Target** when the Target is usable.
7. Listen in Preview. Adjust the correction if needed.
8. Choose **Freeze Correction** when you are satisfied.

Freeze ends learning, not editing. You can continue working with the finished correction afterward.

## 1. Installation

### Windows release package

A built Windows package contains:

- `plugins\clap\Tone Trace EQ.clap`
- `plugins\vst3\Tone Trace EQ.vst3` (copy this entire bundle directory)
- documentation under `docs\`
- `LICENSE` and dependency notices under `licenses\`

It intentionally does not contain an installer, developer tools, tests, or debug symbols. Per-file hashes are in `docs\BUILD_MANIFEST.txt`.

Typical system-wide plug-in locations are:

- CLAP: `C:\Program Files\Common Files\CLAP\`
- VST3: `C:\Program Files\Common Files\VST3\`

Copy either or both plug-in formats into their appropriate folders, then rescan plug-ins in your DAW if necessary.

Both Windows formats use the same editor. The plug-in's global parameters are also available through the host's generic parameter view.

### Plug-in format

The 1.1.0 Windows package contains **VST3 and CLAP**, and both use the same Tone Trace processor and editor. Existing 1.0.5 CLAP projects can be opened by 1.1.0. Keep a backup before updating important projects, because a project saved by 1.1.0 should not be expected to reopen correctly in 1.0.5. VST3 is a separate plug-in identity, so an existing CLAP instance does not silently turn into VST3.

## 2. What Tone Trace is doing

Tone Trace compares the overall tonal shape of the Reference and Target. It is not trying to recognize the microphone, instrument, room, or preset by name; it is listening for the frequency balance that consistently separates one capture from the other.

That is why the two recordings do not need to be sample-aligned. They do, however, need to represent comparable material. If one voice recording contains mostly deep vowels and the other mostly bright consonants, some of that difference belongs to the performance rather than the microphone. The Match Modes help Tone Trace decide how much fine detail to trust for different kinds of material.

Tone Trace also separates **tone** from **level**. A louder Reference does not automatically become a broadband boost. The learned correction describes the tonal shape; **Correction Gain** is the separate control for output level.

After Correct, the retained captures remain useful. Changing Match Mode asks Tone Trace to interpret the same pair differently. Changing Correction Resolution changes the manual editing grid rather than forcing another recording. Limiting Correction Range Low or High stops the effective correction outside the selected part of the spectrum without erasing the learned relationship underneath it.

The correction itself is a normal causal, minimum-phase EQ response, so it can run without lookahead or reported plug-in latency after learning. The same response can also be written as an impulse response. You do not need to know any of that to make a good match, but it explains why a finished correction can work both live and as an exported IR.

Tone Trace is an EQ matcher, not a compressor or mastering limiter. A large EQ boost can still create a large peak, so leave sensible headroom.

## 3. Choosing useful material and a starting mode

The quality of the match depends more on the material you give Tone Trace than on chasing a particular confidence number. A technically stable capture can still describe the wrong thing very accurately.

The Reference and Target do not need to be sample-aligned or contain the exact same performance, but they should represent reasonably comparable sounds. Tone Trace can measure a tonal difference; it cannot know whether that difference came from a microphone, an instrument, a room, a performer, or the way something was played.

### Voice and vocals

Using the same speaker or singer for both recordings is ideal. If that is not possible, use people whose voices are reasonably similar and have them perform at similar intensities. Comparing a quiet, dark delivery with a forceful, bright delivery can make part of the performance difference look like a difference between microphones or signal chains.

Use enough material to include a representative spread of vowels, consonants, pitch, and tone rather than one unusually dark or bright phrase. For example, if you are trying to make one narration microphone resemble another, a few representative sentences from the same reader will usually tell Tone Trace more than one sustained vowel or one short phrase.

### Instruments

Compare similar playing whenever possible. Intensity, register, articulation, and playing technique can change the tone of an instrument dramatically.

A muted trumpet, for example, can sound almost like a different instrument compared with an intense open trumpet performance. The same problem can occur when comparing a softly played guitar with an aggressive one, different piano registers, different pickups, or substantially different bowing or picking techniques. If the goal is to match microphones, rooms, or processing chains, try to keep the instrument and the way it is being played as comparable as possible.

### Full mixes

Choose sections with reasonably similar instrumentation and overall tonal character. A sparse verse and a dense, bright chorus may be part of the same song, but much of their spectral difference belongs to the arrangement rather than the recording or processing chain.

If you are trying to match one mix, mastering chain, recording period, or transfer to another, longer representative sections usually tell Tone Trace more than one unusual passage. When in doubt, try another part of the material before assuming the correction itself is wrong.

Avoid captures that are mostly silence or badly clipped. Tone Trace rejects very low-level material from the accepted capture and refuses profiles with excessive clipping or invalid samples.

### Choose the mode for the material

Choosing an appropriate **Match Mode before capture** gives Tone Trace the best starting assumptions about the kind of material it is hearing. A new instance starts in **Voice**. The modes are not simply different numbers of EQ bands. They change how Tone Trace evaluates repeated spectral information, smoothing, stability, and detail.

- **Voice** — start here for spoken narration, podcast voices, sung vocals, and microphone-to-microphone vocal matching. Voice also includes an automatic safeguard that backs away from unusually narrow detail when that detail would create an excessively ringy correction.
- **Full Mix** — the general starting point for complete songs, mixed program material, and pitched instruments that do not fit one of the specialized modes. Guitar, piano, brass, strings, and complete mixes will often make more sense here than in Voice or Drums.
- **Drums** — start here for drum kits, percussion, loops, and other material where attacks and repeated percussive energy dominate the comparison.
- **Bass or Synth** — start here for bass guitar, synth bass, strongly synthesized material, and bass-heavy sources whose useful spectral relationship may be quite different from speech or a complete mix.
- **Custom Max Capture** — useful for unusually varied material, difficult restoration work, or comparisons that benefit from a longer observation window. It can retain up to about 60 seconds of accepted audio per Reference or Target instead of the standard 30 seconds. It is not an automatic higher-quality mode; representative material matters more than filling the buffer.

Starting with the most appropriate mode is preferable, but choosing one does not lock you in. After **Correct Target** or **Freeze Correction**, you can switch Match Modes and compare different interpretations of the retained Reference and Target without recording them again.

There is one reason the starting choice still matters: Tone Trace does not save the raw recordings in the project. A later mode change can reinterpret the retained spectral captures, but it cannot go back and recapture the original performance under different live-capture rules. For the most representative result, start with the mode that best describes the material and use post-capture mode switching as a comparison tool.

## 4. The workflow

The workflow is deliberately ordered:

**Capture Reference → Learn Target → Correct Target → Freeze Correction**

In the host generic parameter view, **Learn Target** is exposed by its more explicit workflow name, **Save Reference and Learn Target**.

The four everyday workflow commands—Capture Reference, Learn Target, Correct Target, and Freeze Correction—are available directly in the Windows editor. The host's **Workflow Step** parameter exposes those same actions plus the three Reset actions.

### Capture Reference

Choose **Capture Reference**, then play the sound you want the Target to resemble.

During capture, Tone Trace may report:

- `Capturing Reference; no valid audio`
- `Capturing Reference; audio detected`
- `Capturing Reference; collecting`
- `Capturing Reference; unstable audio`
- low, medium, or high confidence
- `Reference capture ready`

**Capture Time counts accepted audio, not simply wall-clock playback time.** If the input is silent or below the capture gate, the timer may barely move even though the transport is running.

Standard modes hold up to about **30 seconds of accepted audio** per Reference or Target. **Custom Max Capture** extends that ceiling to about **60 seconds**. These are maximum accepted-audio capacities, not recommended capture lengths. You do **not** need to fill the buffer before continuing. `Capture full; continue workflow` is not an error; it means Tone Trace has stopped adding samples to that capture and you should continue to the next step.

Reference readiness and Capture Confidence are related but not identical. Once Tone Trace reports that the **Reference capture is ready**, you can continue to Learn Target even if Confidence has not yet reached Low. Confidence remains useful for judging how representative and stable the material is; it is not a requirement to fill the entire Reference buffer.

You can stop playback after capturing the Reference while you prepare or load the Target. Stopping the transport does not discard the captured Reference.

### Learn Target

Choose **Learn Target** in the editor. This does two things in order: it saves the current Reference, then begins a fresh Target capture. In the host generic parameter view, the same Workflow Step is named **Save Reference and Learn Target**. If you return to Learn Target after Preview or Freeze, Tone Trace keeps the valid Reference and starts a fresh Target capture rather than forcing you to capture the Reference again.

Because the Reference is committed first, you can use **Export → Reference Curve (`.tts`)** immediately afterward if your only goal is to create a reusable Reference rather than complete a match.

If the Reference does not yet contain enough accepted material to be usable, the command is rejected, the workflow returns to Reference capture, and the status becomes `Cannot save yet; keep capturing`.

Now play the sound you actually want to correct.

The Target uses the same status ladder as the Reference. Under normal conditions, Tone Trace requires at least a low-confidence Target before it will calculate a correction. More representative material is usually better, but you do not need to wait for High if the capture is already stable and representative.

There is one deliberate fallback: if the mode's **accepted-audio** buffer becomes full while confidence is still zero—about 30 seconds in the standard modes or 60 seconds in Custom Max Capture—the status says `Capture full; continue workflow` and **Correct Target is allowed**. Confidence remains at zero rather than pretending the capture became more reliable. Use Preview to judge that result carefully; if it sounds wrong, recapture the Target or start a new Reference.

### Correct Target

Choose **Correct Target**. Tone Trace analyzes the two captures and builds the correction. While this is happening, Status reports `Analyzing`.

When the model is ready, Status becomes `Preview correction` and the correction is active.

If the Target is not usable yet, the command is rejected and Tone Trace returns you to the Target-learning step. A descending warning sweep may accompany the rejection when tone notifications are enabled.

### Freeze Correction

Choose **Freeze Correction** when you are satisfied with the result.

Freeze stops capture and learning. It does **not** make the EQ controls untouchable: global correction settings and manual trace-band adjustments can still rebuild the rendered correction from the saved captures and model.

Frozen playback reports zero samples of plug-in latency.

### Repeating part of a match

You normally do not need Reset just to redo part of a match. Choosing **Capture Reference** starts a new Reference/Target capture chain. Returning to **Learn Target** after Preview or Freeze keeps the valid Reference and starts a fresh Target capture. While that replacement capture or correction is being prepared, Tone Trace keeps the previous known-good correction available until a new correction has been successfully built.

### Reset

Reset is intentionally two-step and is exposed through the host's **Workflow Step** parameter:

1. **Arm Reset**
2. **Confirm Reset**

Use **Cancel Reset** to return to the previous state without deleting the profile. The editor keeps Reset out of the main workflow row: **Options... → Reset Tone Trace...** opens a standard confirmation dialog. The host generic parameter view still keeps Arm Reset, Confirm Reset, and Cancel Reset inside the single persistent Workflow Step control.

## 5. Confidence, stability, and capture status

### Capture Confidence

**Capture Confidence** is a read-only reliability indicator for the material accepted so far. Tone Trace reports four practical levels: none, low, medium, and high. Some hosts display the same information as values between 0 and 1.

Confidence is not a promise that the capture represents everything the source can do. A highly repetitive few seconds can become statistically stable without being representative of the whole voice, instrument, or mix.

### Curve Drift

**Curve Drift** is a read-only stability indicator. Lower values mean the current spectral estimate is changing less from one update to the next. Treat it as a comparative stability meter rather than a literal measurement of EQ error.

### Useful status messages

| Status | Meaning |
| --- | --- |
| `Ready` | No active capture or usable live profile. |
| `... no valid audio` | Nothing above the capture gate has been accepted yet. |
| `... audio detected` | Useful input has started arriving. |
| `... collecting` | Tone Trace is gathering accepted material. |
| `... unstable audio` | The running estimate is still moving significantly. |
| low / medium / high confidence | Increasing reliability of the accepted material. |
| `Reference capture ready` | The Reference can be saved. |
| `Target capture ready` | The Target is usable for matching. |
| `Cannot save yet; keep capturing` | Reference capture is still insufficient. |
| `Analyzing` | Tone Trace is calculating or rebuilding a profile. |
| `Preview correction` | A correction is active but not frozen. |
| `Frozen correction` | Learning is stopped; the saved correction is active. |
| `Capture full; continue workflow` | The accepted-audio buffer is full: about 30 seconds in standard modes or 60 seconds in Custom Max Capture. Continue to the next workflow step. |
| `Invalid or contaminated capture` | A requested operation could not use the current audio or imported data. |
| `Correction update still completing` | Tone Trace is still finishing the previous correction change. This should be brief; wait for it to settle before making another correction-changing edit. |
| `Frozen; setup changed; relearn recommended` | The current profile is preserved, but a setup change means a new capture may be more appropriate. |
| `Setup locked; reset or restart capture` | A protected setup control was changed during capture and was refused. |

## 6. Match modes

Section 3 gives practical examples for choosing material and a starting mode. This section is the shorter reference.

| Mode | Best starting use |
| --- | --- |
| **Full Mix** | General music, pitched instruments, and mixed program material. |
| **Voice** | Speech and vocals. |
| **Drums** | Percussive material. |
| **Bass or Synth** | Bass-heavy and synthesized sources. |
| **Custom Max Capture** | Unusually varied material, difficult restoration, or work that benefits from up to about 60 seconds of accepted audio. |

The modes change how repeated spectral energy, smoothing, stability, and local detail are evaluated. They are not quality levels, and one mode is not simply a higher-resolution version of another.

Choose the starting mode before capture. Tone Trace blocks Match Mode changes while Reference or Target capture is actively running so one capture cannot change rules halfway through.

Once a correction has been calculated, **Match Mode becomes a post-capture comparison control**. You can switch among the modes in Preview or Frozen and Tone Trace reinterprets the retained Reference and Target spectra; no new capture or Correct Target pass is required. Switching back restores that mode's interpretation of the same retained pair. Correction Resolution, range, strength, gain, and your manual band trims remain your post-match choices rather than being reset by a mode change.

There is one deliberate limitation: the raw recordings are not stored in the project. A post-capture mode switch therefore reinterprets the retained spectral captures; it does not replay the original audio through a different capture gate. This keeps project state compact and avoids storing recorded audio inside the plug-in.

**Voice mode includes an automatic detail safeguard.** Tone Trace first tries the normal detailed Voice match. If that correction would create excessive ringing, it progressively reduces only the narrow resonant detail while preserving the broad tonal match. Normal Voice matches are left unchanged. This is automatic; there is no extra mode or safety control to manage.

## 7. Main controls

The Capture page provides the capture workflow, Match Mode, import/export, Status, and Curve Description. The Correction page provides Correction Strength, Maximum Correction, Correction Gain, Q / Sharpness, low/high correction range, Emergency Clip Guard, resolution, and global options. **Correction Resolution** has its own labeled control on every Bands page. It also remains available through the host's generic parameter view, along with less frequently changed global options such as **Full Correction Range**, tone-notification settings, Bypass, and the Reset workflow.

On a numeric slider, press **Enter** to open exact entry. Type the value and press **Enter** to apply, **Tab** to apply and move onward, or **Escape** to cancel. Invalid values are rejected. Use **Ctrl+Tab** and **Ctrl+Shift+Tab** to switch pages.

### Maximum Correction

Range: **1 to 60 dB**. Default: **18 dB**.

Limits the size of the learned correction. It is a tonal safety limit, not a peak limiter.

Eighteen dB is a sensible starting ceiling for ordinary matching. Larger values are intended for deliberate restoration or unusually severe tonal differences.

### Full Correction Range

Default: **Off**.

With this switch **Off**, **Maximum Correction** sets the largest learned boost or cut Tone Trace may apply. Turn **Full Correction Range** **On** when you deliberately want Tone Trace to go beyond that ceiling and use the full correction range available to it. This is mainly useful for difficult restoration work or other cases where an unusually large correction is intentional.

This switch does **not** perform a different or more accurate analysis. If Maximum Correction is already set to 60 dB, turning it on will not make the match more complete. It simply permits larger EQ moves.

Full Correction Range is also **not** output protection. Extreme boosts can require substantial headroom, so use it deliberately and manage the level afterward with Correction Gain or your preferred downstream limiter.

### Correction Strength

Range: **-1.0 to +1.0**. Default: **+1.0**.

- `+1.0` = full learned correction.
- `0.0` = no learned tonal correction.
- Values between 0 and 1 reduce the match.
- Negative values reverse the correction direction.

Negative strength is useful for checking the inverse relationship or for special effects, but normal matching uses positive values. If a match is basically right but feels too strong, reducing Strength is often more useful than rebuilding the capture.

### Correction Resolution

Range: **1 to 120 bands**. Default: **30**.

Controls the number of editable trace bands. Tone Trace keeps a finer version of the captured relationship underneath; this control determines the manual editing grid you work with.

You can change Correction Resolution **after a match has already been calculated**. Tone Trace rebuilds the editable grid from the retained high-resolution capture/model data, so moving from 30 to 60 bands does not require another Reference capture, Target capture, or Correct Target pass. More bands give you finer manual editing points; they do not make the underlying analysis twice as accurate.

Conversely, lowering Correction Resolution does **not** automatically smooth the learned match. It gives you fewer, broader-spaced points for manual editing. If the automatic correction itself sounds too detailed or ringy, use **Correction Q / Sharpness**, try another Match Mode, or improve the source comparison rather than reducing the band count and expecting the learned curve to change.

On Windows, Tone Trace prefers no more than **10 bands per page**. Exact multiples therefore produce predictable tabs such as **Bands 1-10**, **Bands 11-20**, and **Bands 21-30**. Every tab keeps the **Bands** prefix, even when enough pages are present to require the tab strip's scroll arrows. If the total would leave a tiny final page, Tone Trace redistributes the bands as evenly as possible instead. For example, 31 bands becomes four pages of 8, 8, 8, and 7 rather than 10, 10, 10, and 1.

### Correction Range Low / High

Restricts where the correction is rendered. Capture still analyzes the supported full range, so you can narrow or reopen either end later without recapturing or running Correct Target again.

The editable band frequencies **do not move** when either range limit changes. Instead, the range acts as a non-destructive mask over the existing grid:

- Bands whose center frequencies fall below **Correction Range Low** contribute **0 dB** to the active correction.
- Bands whose center frequencies fall above **Correction Range High** behave the same way and contribute **0 dB**.
- The learned match and any manual trims in those masked bands remain stored. Moving the boundary back restores them immediately.

This is useful when the captured material contains little or no meaningful content at one end of the spectrum. For example, if a source has no useful content below about 60 Hz, raising Correction Range Low can prevent Tone Trace from spending large amounts of gain trying to match that absent sub-bass. The same principle applies at the high-frequency end.

### Correction Q / Sharpness

Range: **0.5 to 1.5**. Default: **1.0**.

Changes how strongly local detail stands out in the rendered correction.

- Below 1.0 = smoother.
- 1.0 = learned shape.
- Above 1.0 = sharper local contrast.

This is the first control to try when the broad tonal direction is useful but the finished correction sounds too narrow, boxy, hollow, or resonant. Moving from 1.0 toward 0.8 or 0.7 can soften local detail without throwing away the entire match.

### Correction Gain

Range: **-24 to +12 dB**. Default: **0 dB**.

A global output trim added to the correction. Use it for level management; Tone Trace does not automatically hide the gain created by EQ boosts.

### Emergency Clip Guard

Range: **-12 to +20 dBFS**. Default: **+6 dBFS**.

This is a blunt, low-resource last-resort clip guard. It exists to stop an extreme correction from running away; it is not supposed to replace a good limiter. If you are leaning on it regularly, fix the gain structure or put a limiter you trust after Tone Trace.

The guard is plug-in-only. Exported impulse responses remain linear and do not contain it.

### Tone Notifications

Turns Tone Trace's generated feedback tones on or off, including confidence tones, warning sweeps, page sweeps, and Trace Curve navigation tones. The intended default is **On**.

### Confidence Tone Volume

Range: **-60 to -12 dB**. Controls notification and trace-tone level. **-60 dB** is exposed as **Off** in the accessible host text.

### Bypass

Default: **Off**.

Bypass crossfades Tone Trace to the **dry input**. It does not mute the track. While bypassed, the correction, notification tones, and Emergency Clip Guard are not applied to the output.

## 8. The editor and accessibility

Tone Trace is meant to be fully usable whether you work visually, by keyboard, or with a screen reader. The graph is useful if you want it, but you never need to see or understand the graph to operate the plug-in. Keyboard focus follows the controls you are actually working with, and screen readers receive the same names and values that are shown visually.

The read-only text box containing the natural-language curve summary has the
accessible label **Curve Description**. Its displayed text is the value,
not the control name. Reference and Target descriptions use **higher** and
**lower** because they describe measured spectral relationships. **Boost** and
**cut** are reserved for the Correction Tone Trace actually applies. When
Maximum Correction limits a larger learned move, the description reports both
the learned and applied values.

The host's generic parameter view exposes the global workflow, matching controls, safety controls, and live status. Correction Resolution is deliberately available there and in the Bands pages. The editor additionally provides the trace-band editor, curve descriptions, Import/Export commands, a compact **Options...** dialog for less-frequently changed controls, and a labeled multiline **Status** panel. Status changes are announced when they matter without turning fast-moving Capture Time and Curve Drift updates into constant screen-reader chatter.

### Reading the Match graph

The graph draws three curves:

- **Reference** — the sound you want to match, shown as a thin warm-colored line.
- **Target** — the sound being corrected, shown as a thin blue line.
- **Correction** — the EQ Tone Trace will apply, shown as the thick light line.

Reference and Target are displayed relative to their own broad level so the graph emphasizes **tonal shape rather than loudness difference**. The Correction curve is shown in actual correction dB.

The graph's vertical range adapts to the captured material instead of forcing every trace into a fixed +/-12 dB window. A small `+/- N dB` label shows the current visual range. This affects only the drawing; it does not change the learned match. Exact frequency and dB values remain available in the readout and Curve Description box, so the graph is never required for operation.

Moving the pointer across the graph places a dotted cursor line, and a small label beside it names the frequency under the pointer. The same frequency — together with the Reference, Target, and Correction values at that point — always appears in the readout above the graph, so the hover label is a convenience for sighted pointer use and never the only source of the value. On a new instance, the readout identifies itself as **Curve Readout** and briefly explains that moving over the graph or using Trace Curve will expose exact values.

### Trace-band keyboard controls

| Key | Action |
| --- | --- |
| **Left / Right** | Previous / next band |
| **Up / Down** | Raise / lower final band level by 1 dB |
| **Page Up / Page Down** | Raise / lower by 6 dB |
| **Home / End** | Jump to the allowed band extremes |
| **0** (or **N**) | Center the selected band at 0 dB |
| **T** or **F2** | Toggle Trace Curve mode |
| **Enter** | Edit the focused band using an exact value |

Each band is drawn visually as a vertical graphic-EQ fader, with its frequency above it, a clear 0 dB center mark, and the exact final dB value below the fader. Sighted users can click or drag a fader, use the mouse wheel for 1 dB steps (hold **Shift** while scrolling for 6 dB steps, matching Page Up / Page Down), or double-click to center that band at 0 dB. Hover, focus, positive gain, and negative gain have distinct visual feedback. The same control exposes its band/frequency name and an editable dB value to accessibility tools, so NVDA and Narrator can announce the same number shown on screen rather than turning the fader into a percentage.

Band values use one public format everywhere: the painted readout, exact-value editor, accessibility value, and curve readout retain up to three meaningful decimal places. Unnecessary trailing zeroes beyond the first decimal are omitted. For example, `-6.2 dB` stays `-6.2 dB`, while `-6.234 dB` remains available as `-6.234 dB`. A 1 dB adjustment preserves the fractional base, so `-6.234 dB` becomes `-5.234 dB`.

The band pages also work before you capture anything. In that state they are a
standalone 30-band graphic EQ on the normal 20 Hz–20 kHz grid; manual changes
affect the audio immediately and can be returned to 0 dB in the same ways. Once
a match exists, those controls become trims around the learned correction.

The displayed value is the **final tonal correction** at that band after Maximum Correction, Q / Sharpness, Correction Strength, and any manual trim. **Correction Gain** remains a separate global level trim and is not folded into every band value.

Band pages use a preferred maximum of 10 bands each and are balanced when necessary to prevent a nearly empty last tab. The default 30-band resolution is therefore three pages: **Bands 1-10**, **Bands 11-20**, and **Bands 21-30**. A 60-band trace uses six predictable 10-band pages, while totals such as 31 are redistributed rather than leaving a one-band final page. Changing Correction Resolution through either the Bands-page control or the host parameter view rebuilds the pages and their visible and accessible tab labels immediately.

Each band keeps the same frequency name visually and through screen readers. If two nearby centers would round to the same short kHz label, Tone Trace shows the extra precision needed to distinguish them rather than presenting two identically named controls.

### Trace Curve mode

**Trace Curve** is a toggle button in the Windows editor. When enabled, moving between bands plays a tone at the selected band's center frequency. This makes it possible to browse the curve by ear.

Trace tones remain available while Frozen because they are navigation aids, not capture-status notifications.

### Page sweeps

Selecting a band page can play a short frequency sweep across that page. Notification tones and trace tones are injected after the capture tap, so they do not contaminate the Reference or Target capture.

During offline rendering, Tone Trace suppresses its generated tones so they are not printed into the render.

## 9. Import and export

Import and Export are available from the Windows editor.

### File types

| Extension | Contains |
| --- | --- |
| `.tts` | A saved Reference or Target spectrum capture |
| `.ttm` | A Tone Trace correction model |
| `.wav` | A direct minimum-phase impulse response |

### Export

The editor can export:

- Reference Curve (`.tts`)
- Target Curve (`.tts`)
- Correction Model (`.ttm`)
- an impulse response (`.wav`) at the current project sample rate

A Reference does **not** require a completed match before it can be exported. After a live Reference is usable, choosing **Learn Target** commits it; **Export → Reference Curve** can then save it even while Tone Trace is waiting for Target material. This is useful for building a library of microphone, voice, room, or processing-chain References.

The exported IR is mono 32-bit floating point and contains the linear EQ correction only. It does not contain the Emergency Clip Guard.

The IR reflects the correction that is active when you export it, including the currently selected Match Mode and rendering choices such as Correction Strength, Q / Sharpness, Correction Gain, Correction Range, and manual band trims. If you change those settings afterward, export a new IR to capture the new version of the correction.

You can also export an IR before learning a match when you have created a curve with the band controls or Correction Gain. Tone Trace presents an accessible standard Windows warning that the IR contains a manually created curve and offers **OK** or **Cancel**, with **OK** as the focused default action. Once a learned match exists, export proceeds normally without this warning. If no match exists and neither the bands nor Correction Gain has changed the flat response, export retains the **No matching curve is available to export yet** error. Strength, Sharpness, and other match-only controls do not make an otherwise flat unmatched instance exportable.

### Import Reference

Importing a Reference curve establishes the tonal destination and moves Tone Trace to Target learning. Record or import the Target next.

### Import Target

A Target import requires a usable Reference. Once both exist, Tone Trace can analyze the correction.

### Import Correction Model

Importing a `.ttm` model bypasses live capture and creates a Frozen correction from that model. Current rendering controls still determine how much of the model is applied.

### Different sample rates

Tone Trace stores curves and models using absolute frequency values. Material from another sample rate can be used when its frequency range overlaps the current session's usable range. Frequencies above the current Nyquist limit are not synthesized.

## 10. Impulse-response export

The editor exports an IR at the current project sample rate. The IR is rendered directly from the learned correction rather than being a recording of Tone Trace's output.

A Tone Trace IR can be loaded into a convolution host such as ReaVerb. The IR represents the linear correction only, so downstream headroom management remains your responsibility. For ReaVerb setup that reproduces the Tone Trace correction by itself, see the troubleshooting section.

## 11. Project save and restore

Once you have a correction, the project saves the correction itself, not just the knob positions. Tone Trace stores the Reference and Target spectra, the learned correction model, manual band edits, correction settings, and the diagnostics needed to put the correction back together. You do **not** need the original `.tts`, `.ttm`, or exported IR files for a saved project to get its correction back.

The raw captured audio is the one thing Tone Trace does not store in the project. It keeps the compact information learned from that audio instead.

A valid saved correction reopens as **Frozen** so loading a project does not unexpectedly start learning from whatever happens to be playing. An incomplete or unusable intermediate capture returns to a safe Ready state rather than pretending the capture can continue where it left off.

Host undo and autosave state requests do not intentionally interrupt a live capture.

## 12. Troubleshooting

### Status stays on "no valid audio"

Check the signal reaching the plug-in. Tone Trace only increments Capture Time for audio that passes its input gate. A playing transport by itself does not mean audio is being accepted.

If meters elsewhere in the DAW move but Tone Trace still reports no valid audio, verify that the audio is actually routed through the track or bus containing Tone Trace and is not arriving only on another path.

### Capture Time moves very slowly or stops

Capture Time is **accepted** time. Quiet gaps can be rejected. The 30-second standard limit and 60-second Custom Max Capture limit are maximum capacities, not targets you need to reach. If the status eventually becomes `Capture full`, the current capture has reached its mode-specific accepted-audio limit. Continue the workflow rather than waiting for the timer to move again.

### I hear a descending sweep and the workflow moves backward

That is the warning tone for a rejected command. Read **Status**. Common cases are trying to save the Reference before it is usable or trying to calculate a correction before the Target is usable. A Target that has actually reached `Capture full; continue workflow` is allowed to proceed even if confidence remains zero.

### I need to stop between Reference and Target

That is fine. Once the Reference is usable, you can stop playback, prepare or load the Target, resume playback, and continue with **Learn Target**. Stopping the transport does not clear the captured Reference.

### The match sounds like the opposite of what I wanted

Check the capture order. **Reference is the sound you want. Target is the sound you are correcting.** Reversing them creates the inverse correction.

### The finished match sounds boxy, hollow, echoey, or ringy

Start with the material rather than the controls. Tone Trace can only compare the spectral information in the Reference and Target, so a difference in performance, room sound, playing style, or arrangement can become part of the learned correction.

For **voice**, the same speaker or singer is ideal. If different people must be used, choose voices that are reasonably similar and compare performances at similar intensities. A quiet, dark narration compared with a forceful, bright narration can make part of the delivery sound like a microphone difference.

For **instruments**, compare similar playing styles, intensities, registers, and tone. A muted trumpet can sound almost like a different instrument compared with an intense open trumpet. If those two performances are used as the pair, Tone Trace has no way to know that the large tonal difference came from the player rather than the microphone or processing chain.

For **full mixes**, try sections with reasonably similar instrumentation and tonal character. A sparse verse and a dense chorus can describe the arrangement more strongly than the processing you are trying to match.

If possible, try another representative section of the source material first.

If the source comparison is good but the result still sounds unnatural, try another **Match Mode**. Full Mix, Voice, Drums, Bass or Synth, and Custom Max Capture use different matching assumptions; they do more than change the editable band layout. After Correct or Freeze, you can compare them using the retained Reference and Target without recording the pair again.

If none of the modes gives a natural result, lower **Correction Q / Sharpness** from 1.0. Values such as 0.8 or 0.7 make local detail less prominent while keeping the broad tonal direction of the match. If necessary, also reduce **Correction Strength** or manually flatten the particular part of the curve that sounds wrong.

Changing **Correction Resolution** is not a substitute for this step. Resolution changes the editable band grid; it does not smooth or recalculate the underlying automatic match.

For speech and vocals, **Voice** is normally the best starting mode because it also includes additional protection against unusually narrow corrections that could produce audible ringing.

### The match is valid but simply feels too strong

Reduce **Correction Strength** while listening in Preview or Frozen. This keeps the learned tonal relationship but applies less of it. If one frequency area is the real problem, use the manual band editor instead of weakening the entire correction.

If unusually large boosts or cuts are involved, also check **Maximum Correction** and whether **Full Correction Range** is enabled.

### An exported IR does not sound like Tone Trace in ReaVerb

Make sure ReaVerb is reproducing the IR by itself rather than mixing it with the original signal:

- Set **Dry** to **-inf dB**.
- Set **Wet** to **0.0 dB**.

If Dry is left audible, ReaVerb mixes the uncorrected signal with the corrected signal, so the result will not match what you heard in Tone Trace.

Also make sure the IR was exported after the Match Mode and correction settings you wanted were selected. An older IR does not change when you later adjust Tone Trace; export a new one for the new correction.

The exported IR contains the linear EQ correction only. It does not contain the Emergency Clip Guard or other nonlinear output protection.

### Output clips after a large correction

Reduce Correction Gain, reduce Correction Strength or Maximum Correction, turn off Full Correction Range if it is enabled, leave more upstream headroom, or use a suitable limiter after Tone Trace. The Emergency Clip Guard is only a hard backstop.

### Bypass makes the effect disappear but audio remains

That is correct. Bypass passes the dry input; it does not mute the track.

### I changed Match Mode during capture and it was refused

That is intentional while capture is actively running. Finish the Reference/Target pair under one starting mode. After Tone Trace has calculated a correction, you can switch Match Mode freely in Preview or Frozen to compare other interpretations without recapturing.

### My saved project reopened Frozen

That is intentional when a valid profile existed. Tone Trace does not resume learning automatically when a project loads.

## 13. Parameter reference

| Parameter | Range | Default | Access |
| --- | --- | --- | --- |
| Workflow Step | 0–7 | 0 | writable |
| Status | 0–30 | 0 | read-only |
| Last Command | 0–7 | 0 | read-only |
| Match Mode | 0–4 | Voice | writable |
| Maximum Correction | 1–60 dB | 18 dB | writable |
| Full Correction Range | Off/On | Off | writable |
| Correction Strength | -1 to +1 | +1 | writable |
| Correction Resolution | 1–120 bands | 30 | writable |
| Correction Range Low | 10–18000 Hz | 10 Hz | writable |
| Correction Range High | 20–30000 Hz | 30000 Hz | writable |
| Correction Q / Sharpness | 0.5–1.5 | 1.0 | writable |
| Correction Gain | -24 to +12 dB | 0 dB | writable |
| Emergency Clip Guard | -12 to +20 dBFS | +6 dBFS | writable |
| Confidence Tone Volume | -60 to -12 dB | -12 dB | writable |
| Capture Confidence | 0–1 | 0 | read-only |
| Curve Drift | 0–60 | 60 | read-only |
| Capture Time | 0–3600 s host range | 0 | read-only; 30 s standard / 60 s Custom Max Capture |
| Tone Notifications | Off/On | On | writable |
| Bypass | Off/On | Off | writable |

### Workflow Step values

| Value | Command |
| --- | --- |
| 0 | No action |
| 1 | Capture Reference |
| 2 | Save Reference and Learn Target |
| 3 | Correct Target |
| 4 | Freeze Correction |
| 5 | Arm Reset |
| 6 | Confirm Reset |
| 7 | Cancel Reset |

---

## Support and acknowledgements

Tone Trace is free. If it helps your music, restoration, production, broadcast, or other audio work, donations are welcome and appreciated, but they are never required. I would rather people actually use Tone Trace than put it behind a checkout page.

If you like it and want to support continued development: [Donate via PayPal](http://paypal.me/dgl1984)

Tone Trace was inspired by **Spectrum Matcher**, which planted the seed for comparing the tonal balance of one recording against another. As Tone Trace developed, its capture workflow, editing, accessibility, live-use goals, and import/export tools took it in directions of its own. Spectrum Matcher deserves credit for helping start the idea.

Development also benefited from studying **Surge XT** and other mature open-source audio projects. Surge helped show where JUCE already did the right thing, where accessibility or host integration needed extra care, and where a simpler approach was better than adding more machinery.
