# Using Tone Trace EQ

Capture the sound you want to match, capture the sound you want to change, then listen to the correction and shape it to suit the job. You can keep working with the result after you freeze it.

This guide covers **Tone Trace 1.3 for Windows**, in VST3 and CLAP formats. It starts with installation and a first match, then explains material selection, editing, and reuse. The keyboard instructions sit alongside the controls they operate.

1. [Getting started](#getting-started)
2. [Finding your way around](#finding-your-way-around)
3. [Your first match](#your-first-match)
4. [Choosing material and a mode](#choosing-material-and-a-mode)
5. [Adjusting the correction](#adjusting-the-correction)
6. [Editing individual bands](#editing-individual-bands)
7. [Reading the curves](#reading-the-curves)
8. [Understanding capture status](#understanding-capture-status)
9. [Saving and reusing curves](#saving-and-reusing-curves)
10. [Saving your project](#saving-your-project)
11. [Automation and processing cost](#automation-and-processing-cost)
12. [Troubleshooting](#troubleshooting)
13. [Parameter reference](#parameter-reference)
14. [Credits](#credits)
15. [Contact and support](#contact-and-support)

## Getting started

### Install the format your host uses

The Windows package supports Windows 10/11 x64. Extract it, then copy either or both formats:

- **CLAP:** `plugins\clap\Tone Trace EQ.clap` goes in `C:\Program Files\Common Files\CLAP\`.
- **VST3:** the complete `plugins\vst3\Tone Trace EQ.vst3` folder goes in `C:\Program Files\Common Files\VST3\`. Keep the bundle together.

Rescan plug-ins in your host if necessary. Both formats provide the same editor and processing. The package also includes documentation, licenses, and per-file hashes in `docs\BUILD_MANIFEST.txt`.

### Updating an existing project

Keep a backup before replacing an older version. Tone Trace 1.3 can read 1.0.5 CLAP states, but projects saved with 1.2 or 1.3 cannot be reopened with 1.0.5. VST3 and CLAP are separate plug-in identities: installing VST3 does not replace a CLAP instance in a project.

If an older project automates matching settings, read [Automation and processing cost](#automation-and-processing-cost) before rendering it. The controls still exist, but some are no longer advertised as automatable.

## Finding your way around

The editor has three kinds of pages:

- **Capture and transfer:** Match Mode, the four workflow buttons, Status, Curve Description, Import, Export, and Options.
- **Correction controls:** the settings used to shape and level the correction.
- **Bands:** individual frequency adjustments, with a Correction Resolution control on each page.

The graph remains visible across pages, as does Bypass. You can operate the plug-in without using the graph. Global controls are also available in your host's generic parameter interface, including REAPER with OSARA; the editor provides the individual bands, text reports, and import/export commands.

### Keyboard navigation

Use **Tab** and **Shift+Tab** to move between controls. **Ctrl+Tab** and **Ctrl+Shift+Tab** change pages. When the page selector has focus, **Left/Right** changes pages, **Home/End** selects the first or last page, and **Down** opens the page menu.

If all page buttons do not fit, **All pages...** opens the complete list. The page selector occupies one Tab stop, so you do not have to tab through every page to reach its controls.

### Entering values

On a global numeric slider, arrows make a small adjustment and **Page Up/Down** makes a larger one. **Home** selects the minimum and **End** the maximum. The vertical band faders use different navigation keys; see [Editing individual bands](#editing-individual-bands).

Press **Enter** on a numeric slider or band to enter an exact value. Press **Enter** to apply it, **Tab** to apply and move onward, or **Escape** to cancel. Invalid values are rejected.

### Status and feedback

The labeled **Status** field gives the workflow state and capture measurements. Read it when you need to know whether a capture is ready or why a command was refused. Routine updates do not automatically speak every change. Spoken announcements go through the screen reader's accessibility system.

Tone notifications are enabled by default. **Options...** on Capture and transfer includes **Tone Notifications** and **Confidence Tone Volume**, along with Full Correction Range, Bypass, and Reset. You can turn the generated tones off or lower their level.

## Your first match

Start with two recordings whose tonal difference you want to correct. For a first microphone comparison, two recordings of the same speaker with similar delivery are a useful choice.

**Reference means the sound you want. Target means the sound you are correcting.** If the dull recording should resemble the brighter one, the brighter recording is the Reference.

Route the recordings through the same Tone Trace instance, one at a time. A simple setup is to place them one after the other on one track and insert Tone Trace on that track. After capture, play the Target through the plug-in to hear the correction.

### 1. Choose the mode

On **Capture and transfer**, choose **Match Mode** before you start recording. A new instance starts in **Voice**, suitable for speech and vocals. **Full Mix** is a starting point for complete mixes and many pitched instruments. The other choices are described under [Choosing material and a mode](#choosing-material-and-a-mode).

### 2. Capture the Reference

Choose **Learn Reference**, then play the Reference recording. Read Status as audio is collected. Once it says **Reference capture ready**, you can continue.

You do not need to fill the buffer. Standard modes hold up to about 30 seconds of accepted audio; that is a capacity, not a required recording length. Silence and very quiet gaps may not count toward Capture Time.

You can stop playback now to prepare the Target. Stopping the transport does not discard the Reference.

### 3. Learn the Target

Choose **Learn Target**, then play the Target recording. This button first saves the Reference and then starts Target capture. In the host's generic parameter view, the same command is named **Save Reference and Learn Target**.

Listen for the three rising tone sweeps as Tone Trace reaches low, medium, and then high confidence. Wait for that high-confidence sweep before choosing **Correct Target**. If tones are turned off, watch **Capture Confidence** for **High** instead.

### 4. Correct and listen

Choose **Correct Target**. Status reports **Analyzing** while Tone Trace builds the match, then **Preview correction** when it is active.

Play the Target and listen. Compare with Bypass at a similar listening level. If the tonal direction is useful but the change is too strong, lower **Correction Strength**. Use **Correction Gain** to manage the output level. [Adjusting the correction](#adjusting-the-correction) explains the other controls.

### 5. Freeze the correction

Choose **Freeze Correction** when you are satisfied. The correction stays active and you can still edit it. Preview and Frozen playback use no lookahead and report zero samples of plug-in latency.

Save your project to retain the correction and your edits. You can also [export a Reference, model, or impulse response](#saving-and-reusing-curves).

### Redoing a capture

To replace only the Target, return to **Learn Target**. Tone Trace keeps the valid Reference and starts a fresh Target capture. **Learn Reference** starts a new Reference/Target pair.

While a replacement is being prepared, the previous valid correction remains available until the new one is successfully built. You normally do not need Reset just to redo a capture.

### Starting over with Reset

Choose **Options... → Reset Tone Trace...** and confirm the dialog. In the host's generic parameter view, use **Workflow Step → Arm Reset**, then **Confirm Reset**. **Cancel Reset** returns to the previous state without deleting the profile.

## Choosing material and a mode

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

### Changing modes after a match

Match Mode is locked during active capture so the capture rules cannot change halfway through. After Correct or Freeze, you can switch modes and compare their interpretations of the same saved spectra. Your strength, range, gain, resolution, and manual trims remain in place.

This reuses the spectral information; it does not replay the original recordings through a different capture process. Choose the starting mode to suit the material, then use later mode changes to compare results.

## Adjusting the correction

Once a match is active, open **Correction controls**. Start with the change you hear: too much overall correction, too much narrow detail, an unwanted frequency range, or a level difference.

Tone Trace separates tonal shape from overall level. A louder Reference does not automatically become a broadband boost. Correction Gain controls the output level separately.

### Correction Strength

Range: **-1.0 to +1.0**. Default: **+1.0**.

- `+1.0` = full learned correction.
- `0.0` = no learned tonal correction.
- Values between 0 and 1 reduce the match.
- Negative values reverse the correction direction.

Negative strength is useful for checking the inverse relationship or for special effects, but normal matching uses positive values. If a match is basically right but feels too strong, reducing Strength is often more useful than rebuilding the capture.

### Correction Q / Sharpness

Range: **0.5 to 1.5**. Default: **1.0**.

Changes how strongly local detail stands out in the rendered correction.

- Below 1.0 = smoother.
- 1.0 = learned shape.
- Above 1.0 = sharper local contrast.

This is the first control to try when the broad tonal direction is useful but the finished correction sounds too narrow, boxy, hollow, or resonant. Moving from 1.0 toward 0.8 or 0.7 can soften local detail without throwing away the entire match.

### Correction Range Low / High

Restricts where the correction is rendered. Capture still analyzes the supported full range, so you can narrow or reopen either end later without recapturing or running Correct Target again.

The editable band frequencies **do not move** when either range limit changes. Instead, the range acts as a non-destructive mask over the existing grid:

- Bands whose center frequencies fall below **Correction Range Low** contribute **0 dB** to the active correction.
- Bands whose center frequencies fall above **Correction Range High** behave the same way and contribute **0 dB**.
- The learned match and any manual trims in those masked bands remain stored. Moving the boundary back restores them when the updated correction is ready.

This is useful when the captured material contains little or no meaningful content at one end of the spectrum. For example, if a source has no useful content below about 60 Hz, raising Correction Range Low can prevent Tone Trace from spending large amounts of gain trying to match that absent sub-bass. The same principle applies at the high-frequency end.

### Maximum Correction

Range: **1 to 60 dB**. Default: **18 dB**.

Limits the size of the learned correction. It is a tonal safety limit, not a peak limiter.

Eighteen dB is a sensible starting ceiling for ordinary matching. Larger values are intended for deliberate restoration or unusually severe tonal differences.

### Full Correction Range

Default: **Off**.

With this switch **Off**, **Maximum Correction** sets the largest learned boost or cut Tone Trace may apply. Turn **Full Correction Range** **On** when you deliberately want Tone Trace to go beyond that ceiling and use its full **60 dB** correction range. This is mainly useful for difficult restoration work or other cases where an unusually large correction is intentional.

This switch does **not** perform a different or more accurate analysis. If Maximum Correction is already set to 60 dB, turning it on will not make the match more complete. It simply permits larger EQ moves.

Full Correction Range is also **not** output protection. Extreme boosts can require substantial headroom, so use it deliberately and manage the level afterward with Correction Gain or your preferred downstream limiter.

### Correction Gain

Range: **-24 to +12 dB**. Default: **0 dB**.

A global output trim added to the correction. Use it for level management; Tone Trace does not automatically hide the gain created by EQ boosts.

### Emergency Clip Guard

Range: **-12 to +20 dBFS**. Default: **+6 dBFS**.

This is a blunt, low-resource last-resort clip guard. It exists to stop an extreme correction from running away; it is not supposed to replace a good limiter. If you are leaning on it regularly, fix the gain structure or put a limiter you trust after Tone Trace.

The guard is plug-in-only. Exported impulse responses remain linear and do not contain it.

### Bypass

Default: **Off**.

Bypass crossfades Tone Trace to the **dry input**. It does not mute the track. While bypassed, the correction, notification tones, and Emergency Clip Guard are not applied to the output.

## Editing individual bands

Use the Bands pages when one part of the correction needs an adjustment. Each fader is named by frequency and displays its final tonal correction in dB. **Correction Gain** remains separate, so a band's value excludes that global trim.

The bands also work before you capture anything. A new instance starts with 30 bands on a 20 Hz–20 kHz grid, allowing you to build an EQ curve by hand.

### Correction Resolution

Range: **1 to 120 bands**. Default: **30**.

Controls the number of editable trace bands. Tone Trace keeps a finer version of the captured relationship underneath; this control determines the manual editing grid you work with.

You can change Correction Resolution **after a match has already been calculated**. Tone Trace rebuilds the editable grid from the retained high-resolution capture/model data, so moving from 30 to 60 bands does not require another Reference capture, Target capture, or Correct Target pass. More bands give you finer manual editing points; they do not make the underlying analysis twice as accurate.

Conversely, lowering Correction Resolution does **not** automatically smooth the learned match. It gives you fewer, broader-spaced points for manual editing. If the automatic correction itself sounds too detailed or ringy, use **Correction Q / Sharpness**, try another Match Mode, or improve the source comparison rather than reducing the band count and expecting the learned curve to change.

### Trace-band keyboard controls

| Key | Action |
| --- | --- |
| **Left / Right** | Previous / next band |
| **Up / Down** | Raise / lower final band level by 1 dB |
| **Page Up / Page Down** | Raise / lower by 6 dB |
| **Home / End** | Maximum boost / maximum cut for the selected band |
| **0** (or **N**) | Center the selected band at 0 dB |
| **T** or **F2** | Toggle Trace Curve mode |
| **Enter** | Edit the focused band using an exact value |

Each band has a vertical fader, with its frequency above it and its value in dB below. Click or drag to adjust it, scroll for 1 dB steps, or hold **Shift** while scrolling for 6 dB steps. Double-click to center the band at 0 dB. Screen readers announce the band's frequency and dB value.

Values retain up to three decimal places in the display, exact entry, and screen-reader output. Keyboard adjustments preserve that precision: raising `-6.234 dB` by 1 dB gives `-5.234 dB`.

Band pages adapt to the window width, with up to ten bands per page. Increasing the resolution adds pages; narrowing the window can also increase the page count. Page labels always identify the bands they contain. Use the page selector or its full menu to reach any band.

Closely spaced bands show enough frequency detail to tell them apart, both visually and through screen readers.

### Trace Curve mode

**Trace Curve** is a toggle button in the Windows editor. When enabled, moving between bands plays a tone at the selected band's center frequency. This makes it possible to browse the curve by ear.

Trace tones remain available while Frozen because they are navigation aids, not capture-status notifications.

### Page sweeps

Selecting a band page can play a short frequency sweep across that page. Notification tones and trace tones are injected after the capture tap, so they do not contaminate the Reference or Target capture.

During offline rendering, Tone Trace suppresses its generated tones so they are not printed into the render.

### Notification settings

**Tone Notifications** turns generated feedback tones on or off, including confidence tones, warning sweeps, page sweeps, and Trace Curve navigation tones. Default: **On**.

**Confidence Tone Volume** controls notification and trace-tone level, from **-60 to -12 dB**. **-60 dB** is shown as **Off** in the accessible host text. Both settings are in **Options...** on Capture and transfer.

## Reading the curves

### Curve Description

On **Capture and transfer**, Curve Description is a labeled, read-only text report. **Copy Curve Description** copies it to the clipboard.

Once a Reference is usable, Tone Trace prepares a report of its spectrum and refreshes it periodically as more audio is accepted. You can stop playback and read or copy the report without starting the Target. The Reference report remains available during Target capture.

Correct Target also requests a Target report while the match is being analyzed. Reports are prepared in the background, so a new one may take a moment to appear. A quick analysis can reach the completed comparison before you have time to read the Target report.

When the correction is ready, the field describes the Reference, Target, and applied correction together. Reference and Target descriptions use **higher** and **lower** for spectral shape; **boost** and **cut** describe the applied EQ. When Maximum Correction limits a learned move, the report gives both the learned and applied values.

The report describes what was measured. It does not decide whether the recording sounds good or whether a peak needs fixing.

### Reading the Match graph

The graph draws three curves:

- **Reference** — the sound you want to match, shown as a thin warm-colored line.
- **Target** — the sound being corrected, shown as a thin dashed blue line.
- **Correction + gain** — the EQ response including Correction Gain, shown as the thick light line.

Reference and Target are displayed relative to their own broad level so the graph emphasizes **tonal shape rather than loudness difference**. The Correction + gain curve shows correction dB including the global output trim.

The graph's vertical range adapts to the curves. The dB axis shows the current levels and the legend says **Auto scale (dB)**. This changes the drawing scale, not the learned match. Exact values are available in Curve Readout.

Moving the pointer across the graph places a dashed cursor line. **Curve Readout** gives the frequency, Reference and Target levels relative to their respective means, and Correction + gain at that point. Trace Curve also updates the readout. A new instance briefly explains these ways of inspecting the curve.

## Understanding capture status

### Accepted audio and capture limits

Capture Time counts audio accepted by Tone Trace, not elapsed playback time. Very quiet material and gaps can be rejected. Standard modes hold about **30 seconds** per Reference or Target; **Custom Max Capture** holds about **60 seconds**.

At the limit, Status says **Capture full; continue workflow** and the capture stops adding samples. Continue to the next step. A full Target is allowed to proceed even if confidence is still zero; the confidence reading stays honest, and Preview is where you decide whether the result is useful.

Reference readiness and confidence are different checks. A ready Reference can be saved even if its confidence has not reached Low. If Learn Target is refused with **Cannot save yet; keep capturing**, return to Reference playback and gather more useful audio. If Correct Target is refused, continue Target capture.

### Capture Confidence

**Capture Confidence** is a read-only reliability indicator for the material accepted so far. Tone Trace reports four practical levels: none, low, medium, and high. Some hosts display the same information as values between 0 and 1.

The rising sweeps mark low, medium, and high confidence; the high-confidence sweep rises highest and lasts longest. If confidence falls, a descending sweep signals the change. Listen for the high-confidence cue rather than simply counting every sweep you hear.

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

## Saving and reusing curves

Use **Import** and **Export** on Capture and transfer to reuse spectra and corrections outside the current project.

### File types

| Extension | Contains |
| --- | --- |
| `.tts` | A saved Reference or Target spectrum capture |
| `.ttm` | A Tone Trace correction model |
| `.wav` | A direct minimum-phase impulse response |

### Export a curve or impulse response

The editor can export:

- Reference Curve (`.tts`)
- Target Curve (`.tts`)
- Correction Model (`.ttm`)
- an impulse response (`.wav`) at the current project sample rate

A Reference does **not** require a completed match before it can be exported. After a live Reference is usable, choosing **Learn Target** commits it; **Export → Reference Curve** can then save it even while Tone Trace is waiting for Target material. This is useful for building a library of microphone, voice, room, or processing-chain References.

The exported IR is mono 32-bit floating point and contains the linear EQ correction only. It does not contain the Emergency Clip Guard.

The IR reflects the correction that is active when you export it, including the currently selected Match Mode and rendering choices such as Correction Strength, Q / Sharpness, Correction Gain, Correction Range, and manual band trims. If you change those settings afterward, export a new IR to capture the new version of the correction.

You can also export an IR before learning a match when you have created a curve with the band controls or Correction Gain. Tone Trace asks you to confirm that you want to export a manually created curve. Once a learned match exists, export proceeds normally without this warning. If no match exists and neither the bands nor Correction Gain has changed the flat response, export retains the **No matching curve is available to export yet** error. Strength, Sharpness, and other match-only controls do not make an otherwise flat unmatched instance exportable.

### Import Reference

Importing a Reference curve establishes the tonal destination and moves Tone Trace to Target learning. Record or import the Target next. Curve Description reports the imported Reference in the same objective language used for a live Reference.

### Import Target

A Target import requires a usable Reference. Once both exist, Tone Trace analyzes the correction. During that analysis, Tone Trace prepares a Target report for Curve Description. When the match is ready, the field shows the completed comparison.

### Import Correction Model

Importing a `.ttm` model bypasses live capture and creates a Frozen correction from that model. Current rendering controls still determine how much of the model is applied.

### Different sample rates

Tone Trace stores curves and models using absolute frequency values. Material from another sample rate can be used when its frequency range overlaps the current session's usable range. Frequencies above the current Nyquist limit are not synthesized.

### Using an exported impulse response

An impulse response (IR) stores the EQ response in a WAV file for use in a convolution processor, such as ReaVerb. Tone Trace renders it directly from the current correction at the project sample rate, rather than recording its audio output. The response is minimum-phase.

For a ReaVerb setup that reproduces the correction by itself, see [An exported IR does not sound like Tone Trace in ReaVerb](#an-exported-ir-does-not-sound-like-tone-trace-in-reaverb).

## Saving your project

Once you have a correction, the project saves the correction itself, not just the knob positions. Tone Trace stores the Reference and Target spectra, the learned correction model, manual band edits, correction settings, and the diagnostics needed to put the correction back together. You do **not** need the original `.tts`, `.ttm`, or exported IR files for a saved project to get its correction back.

The raw captured audio is the one thing Tone Trace does not store in the project. It keeps the compact information learned from that audio instead.

A valid saved correction reopens as **Frozen** so loading a project does not unexpectedly start learning from whatever happens to be playing. An incomplete or unusable intermediate capture returns to a safe Ready state rather than pretending the capture can continue where it left off.

Host undo and autosave state requests do not intentionally interrupt a live capture.

## Automation and processing cost

You can automate **Correction Gain**, **Emergency Clip Guard**, **Confidence Tone Volume**, **Tone Notifications**, and **Bypass** in your host. Tone Trace smooths gain changes and reads automation once per audio block, so a change can land slightly after the point you drew.

**Maximum Correction**, **Strength**, **frequency range (Low and High)**, **Q / Sharpness**, and **Full Correction Range** can also be automated and adjusted through REAPER/OSARA. Tone Trace recalculates the correction when you change them, without restarting capture. You may hear a short delay before the new setting takes effect. If you make several changes quickly, Tone Trace may apply only the latest setting. These controls do not provide sample-accurate automation. For offline rendering, use static correction settings and wait for the correction to finish updating before rendering or exporting.

**Match Mode**, **Resolution**, and **Workflow Step** remain non-automatable. Set them by hand: they change capture interpretation, resample manual band edits, or run capture/analysis/reset commands. Check existing automation lanes for these controls when opening older projects.

Bypass lets you hear the dry input and switch straight back to the correction. Tone Trace keeps processing while bypassed. To free up CPU, deactivate the plug-in or freeze the track in your host.

If you're running several instances, each one sets aside memory when you start a capture. A standard stereo capture uses about 23 MB at 48 kHz or 92 MB at 192 kHz. Custom Max Capture doubles those figures. Tone Trace can keep that memory for the next capture until the host prepares the idle plug-in again or removes it. Analysis and reports can temporarily use more.

## Troubleshooting

### Status stays on "no valid audio"

Check the signal reaching the plug-in. Tone Trace only increments Capture Time for audio that passes its input gate. A playing transport by itself does not mean audio is being accepted.

If meters elsewhere in the DAW move but Tone Trace still reports no valid audio, verify that the audio is actually routed through the track or bus containing Tone Trace and is not arriving only on another path.

### Capture Time moves very slowly or stops

Capture Time is **accepted** time. Quiet gaps can be rejected. The 30-second standard limit and 60-second Custom Max Capture limit are maximum capacities, not targets you need to reach. If the status eventually becomes `Capture full`, the current capture has reached its mode-specific accepted-audio limit. Continue the workflow rather than waiting for the timer to move again.

### I hear a descending sweep and the workflow moves backward

That is the warning tone for a rejected command. Read **Status**. Common cases are trying to save the Reference before it is usable or trying to calculate a correction before the Target is usable. A Target that has actually reached `Capture full; continue workflow` is allowed to proceed even if confidence remains zero.

### I need to stop between Reference and Target

That is fine. Once the Reference is usable, you can stop playback, prepare or load the Target, choose **Learn Target**, and then resume playback. Stopping the transport does not clear the captured Reference.

### The match sounds like the opposite of what I wanted

Check the capture order. **Reference is the sound you want. Target is the sound you are correcting.** Reversing them creates the inverse correction.

### The finished match sounds boxy, hollow, echoey, or ringy

First check that the recordings make a useful comparison. Differences in performance, room sound, or arrangement can become part of the correction. Try another representative passage; [Choosing material and a mode](#choosing-material-and-a-mode) gives examples.

If the comparison is sound, try another Match Mode using the retained captures. You can also lower **Correction Q / Sharpness** toward 0.8 or 0.7 to soften narrow detail. Reduce Strength if the whole match is too strong, or edit an individual band if one area is the problem.

Lowering Correction Resolution does not smooth the automatic match. It changes the number of manual editing points.

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

## Parameter reference

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
| 1 | Learn Reference |
| 2 | Save Reference and Learn Target |
| 3 | Correct Target |
| 4 | Freeze Correction |
| 5 | Arm Reset |
| 6 | Confirm Reset |
| 7 | Cancel Reset |

## Credits

**[Victor Tsaran (vick08)](https://github.com/vick08)** contributed the macOS port, including universal Audio Unit, VST3, and CLAP builds for Apple Silicon and Intel Macs, installer packaging, and macOS build and validation checks in GitHub Actions.

Tone Trace was inspired by **[Spectrum Matcher](https://geraintluff.github.io/jsfx/)**, a REAPER JSFX by **Geraint Luff** that analyzes the spectral difference between an input and a reference and can apply an EQ correction from that comparison. Using Spectrum Matcher planted the seed for Tone Trace’s original match-EQ concept. Tone Trace later grew in its own direction because I wanted a more accessible capture workflow, an editable correction model, and tools for importing/exporting curves created by Tone Trace.

Development also benefited from studying **Surge XT** and other mature open-source audio projects. Surge was especially useful as a practical reference for where JUCE already provides solid behavior, where accessibility or host integration needs explicit attention, and where our own early solutions were becoming more complicated than they needed to be. That kind of open source visibility helped Tone Trace, and it is one reason the project's own accessibility and regression-testing infrastructure is kept readable for other developers to study and adapt.

### Design and AI-written code

Finally, if a program does more than say “Hello, world!” and/or looks well-designed visually, every line responsible for such elevated standards was coded by AI—but the design is mine.

## Contact and support

Questions, bug reports, and accessibility feedback: `info@lanesaudio.com`. Include the plug-in format, host and version, operating system, and what you were doing when the problem occurred. For screen-reader issues, include the screen reader and what you expected to hear.

Tone Trace is free for audio work. If it helps your music, restoration, production, or broadcasting, donations are welcome but never required. I would rather people actually use Tone Trace than put it behind a checkout page.

[Donate via PayPal](http://paypal.me/dgl1984)
