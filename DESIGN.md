# Independent engine design boundary

## Separation

This codebase implements the requested behavior from a neutral product
specification. No prior JSFX implementation or third-party UI library is part of
the build, repository, tests, or data format.

## Model first

Tone Trace capture produces a portable correction model. A node contains a
frequency in Hz, a correction in dB, and a confidence value. It is not tied to a
host, plug-in API, FFT length, or sample rate.

This separation gives the project three consumers of the same match result:

1. a live plug-in renderer;
2. direct minimum-phase impulse responses for convolution hosts such as ReaVerb;
3. offline renders and automated regression tests.

The host project state will retain the model and editable settings. Exported IRs
are deliberately final audio assets rather than the only saved representation.

## Current analysis path

1. Reject frames substantially below the capture's useful peak level.
2. Analyze accepted frames with an 85 ms Hann-windowed FFT and 50 percent hop.
3. Average left and right channel power without summing the channels, avoiding
   cancellation from stereo phase differences.
4. Accumulate a mode-dependent generalized power statistic. Voice uses a
   1.5-power mean: recurring speech energy matters more than quiet frames,
   without allowing one loud pitched phrase to dominate the capture.
5. Estimate the standard error of that statistic and convert it to
   per-frequency confidence.
6. Sample the result on an ERB-spaced perceptual grid.
7. Remove broad level difference so matching cannot create an alarming global
   volume jump; Correction Gain remains a separate control.
8. Subtract Target from Reference and build the broad tonal correction.
9. Build a separate fine-difference layer. Only stable, locally supported
   detail may modify the broad curve, allowing a narrow source resonance to be
   attenuated without converting a one-off room event into permanent EQ.
10. In Voice mode, test the full-detail result for suspicious dense narrow
    structure. Benign curves pass unchanged. A suspicious curve is compared
    against the broad-only minimum-phase result using sign-symmetric late-IR
    energy measured at a fixed 48 kHz diagnostic rate, then only the
    resonance-restoration layer is progressively reduced
    until the excess tail is acceptable. Other modes retain their historical
    full-detail matching behavior.
11. Recenter broad level, confidence-shape the result, and enforce the
    correction ceiling. The conservative default is 18 dB, while an explicit
    opt-in value up to 60 dB supports extreme restoration curves. It limits
    tonal correction but is not output protection.

The plug-in exposes that choice rather than silently imposing the default. A
normal 18 dB starting point serves routine material; a clearly named
Full Correction Range setting bypasses the user-set Maximum Correction ceiling and permits the entire supported learned curve up to the
60 dB numerical guard. The guard protects finite model and IR arithmetic. It is
not a limiter, a recommended boost, or permission to hide downstream gain.

Correction Range Low and High are non-destructive render masks over the learned
band grid. They never move or renumber manual bands. Auto-match and manual trim
contributions outside either boundary are 0 dB, while their stored values remain
available immediately if the range is reopened. Low and high limits are
symmetrical by design.

## Live capture status boundary

Capture confidence describes the statistical reliability of material already
heard; it does not prove that the passage represents future vowels, pitches,
instruments, or room excitations. The plug-in must therefore keep confidence
and curve drift as separate status inputs. Current random-window tests support
an early usable indication at 0.40 confidence, a stricter good-confidence
indication at 0.50, and a later provisional stable indication that also
requires minimum elapsed time and sustained low curve drift. Stability must
return to learning if new material moves the curve. Freeze remains manual.

## Deliberate dynamics boundary

Tone Trace does not perform hidden gain reduction or pretend to be a mastering
limiter. The plug-in does provide an explicit `Emergency Clip Guard` as a final
hard ceiling for catastrophic profile/output cases. It defaults to +6 dBFS and
is user adjustable from -12 to +20 dBFS. This nonlinear safety control is
applied after correction and notification tones, and is bypassed with the
plug-in. Direct IR exports remain linear and therefore cannot include it.
Routine headroom and transparent peak control remain the user's responsibility.

## Binding accessible workflow

The generic host parameter interface is authoritative. The command sequence is
Capture Reference; Save Reference and Learn Target; Correct Target; Freeze
Correction. Save Reference and Learn Target ends Reference capture before Target
capture begins. If the user requests a Reference-only `.tts` export at that point,
the Win32 editor lazily analyzes the now-immutable retained Reference on the main
thread; the ordinary matching path is left untouched. A rejected Save returns to Capture Reference, a rejected Correct
returns to Learn Target, and a rejected Freeze returns to Correct Target so no
command can trap the user in an unusable state. Reset requires Arm followed by
Confirm and can be cancelled.

Serialized project state never contains live learning. A valid last-known-good
profile is serialized Frozen; otherwise it is Ready, and loading never resumes
capture. A generic CLAP state-save callback cannot distinguish an explicit user
save from host undo/autosave bookkeeping, so taking a snapshot must not mutate
or cancel the live instance. The explicit Freeze command ends live learning.
Capture and analysis may take all needed time, while Frozen reports zero
latency and performs no capture analysis. Invalid or contaminated work must
never replace the last-known-good profile.

Capture always analyzes the full supported 10 Hz-30 kHz range. Range Low and
Range High restrict only the rendered correction delta. Most post-capture
controls rerender the retained high-resolution model without rematching. Match
Mode is the intentional exception: after Correct/Freeze it rematches the retained
Reference and Target spectra under the newly selected mode, so users can compare
mode-specific smoothing, point density and Voice safety without recording the pair
again. Raw capture audio is not serialized, so a restored project performs this
comparison from the retained spectra rather than replaying a different capture
gate over the original recording.

## Current rendering path

The minimum-phase renderer samples the correction model at the requested sample
rate, converts log magnitude to a causal real cepstrum, and reconstructs a
minimum-phase impulse. This avoids pre-ringing and makes direct IR export native
at every supported rate rather than relying on resampling a 48 kHz master.
An exported IR never extrapolates correction beyond the highest frequency that
the original captures could analyze.

The exported 32-bit float coefficients are the canonical internal kernel, not
a lower-precision copy of a hidden double-precision IR. Saving and reloading a
direct IR therefore preserves every coefficient exactly. Convolving the same
source with the internal kernel and the saved IR must produce a zero-sample
null in the reference implementation. A partitioned real-time wrapper may
accumulate insignificant floating-point ordering differences, but it must pass
an equivalent null threshold and preserve the same latency and gain.

The first plug-in prototype can use this same kernel so a direct IR and the live
processor agree. A compact IIR renderer may be evaluated later, but it must pass
the same audio and CPU tests before replacing or supplementing the FIR path.

## Quality gate before plug-in wrappers

- A self-match must remain essentially flat.
- A known colored fixture must become substantially closer to its reference and
  meet the recorded RMS spectral-error threshold.
- Models must survive save and reload without changing their response.
- IRs must render without invalid samples at 44.1, 48, 88.2, 96, 176.4, and
  192 kHz.
- Real voice and music fixtures must meet or beat the accepted Tone Trace JSFX
  in blind listening before this engine is treated as a replacement.
