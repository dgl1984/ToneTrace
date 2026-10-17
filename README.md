# Tone Trace EQ

**A match EQ you can keep working with.**

If you already know what an EQ does, Tone Trace starts from familiar ground. A normal EQ gives you bands and asks you to decide what needs more or less. A match EQ can take a different route: give it one sound you like and one sound you want to change, and it can learn the tonal difference for you.

Tone Trace takes that idea further. It sits somewhere between a match EQ, a hands-on graphic EQ, and a reusable correction tool. None of those ideas is unusual by itself; the useful part is that they stay connected. You capture a **Reference**—the sound you want—and a **Target**—the sound you want to correct. Tone Trace listens to both, builds the match, and then gives that result back to you as something you can continue to work with rather than a one-shot calculation.

That distinction matters in practice. You can audition the match, change how Tone Trace interprets the same captures, narrow the frequency range, choose a finer or broader set of editable bands, and make your own adjustments without recording the source again. When the correction is finished, it can stay in a live signal path with zero reported plug-in latency and no lookahead, or it can be exported as an impulse response for use elsewhere.

The result is useful anywhere two recordings ought to live in the same tonal world: matching microphones, bringing voice recordings from different chains closer together, restoring old material toward a cleaned-up reference, evening out production changes between sessions, or simply getting to a difficult EQ starting point faster than drawing the curve by hand.

Tone Trace EQ is a free match EQ from **Lanes Audio** and ships as VST3 and CLAP plug-ins on Windows.

## Download and install

The 1.1.0 release is **Windows 10/11 x64**. The single package is
`ToneTrace_EQ_1.1.0_Windows_x64.zip`. Extract it and copy
`plugins\clap\Tone Trace EQ.clap` to:

`C:\Program Files\Common Files\CLAP\`

For VST3, copy the entire `plugins\vst3\Tone Trace EQ.vst3` folder to
`C:\Program Files\Common Files\VST3\`. Use either format in a project, then rescan
plug-ins in the host. The ZIP also contains the user documentation, release notes, licenses, and `docs\BUILD_MANIFEST.txt` with a SHA-256 hash for every packaged file. It contains no installer, developer tools, tests, or debug
symbols, and it changes nothing until you copy a plug-in.

This package is the JUCE port of the 1.0.5 feature set, using its unchanged matching
and realtime engine. The CLAP ID and numeric parameter IDs are retained, and the
port reads 1.0.5 CLAP states. Keep a backup
before updating projects: states saved by 1.1 are not readable by 1.0.5. VST3
is a separate host plug-in identity, so existing CLAP instances stay CLAP.

## The one rule to remember

Tone Trace uses two simple names throughout the plug-in:

- **Reference = the sound you want.**
- **Target = the sound you are correcting.**

If a dull microphone recording should sound more like a brighter microphone, capture the brighter recording as the Reference and the dull recording as the Target. Tone Trace learns the tonal difference and applies it in the direction of the Reference.

## Your first match

1. Insert **Tone Trace EQ** on the track or bus that will receive both captures.
2. **Match Mode starts in Voice.** Leave it there for speech, vocals, or
   microphone matching; choose **Full Mix** for complete songs and mixed
   program material.
3. Choose **Capture Reference** and play the sound you want to match.
4. Choose **Learn Target**. That button first saves the Reference you just captured, then begins Target capture. If all you wanted was a reusable Reference, it is already available for export at this point.
5. Play the sound you want to correct.
6. Choose **Correct Target** when the Target is ready.
7. Listen. If the broad match is right but you want to shape it further, use the correction controls or the graphic-EQ bands.
8. Choose **Freeze Correction** when you are satisfied.

A match does not become untouchable after Freeze. Tone Trace keeps the captured relationship available, so you can continue refining the correction without starting the capture process over. You also do not need to fill the 30-second standard capture buffer before moving on; that figure is a maximum accepted-audio capacity, not a recommended capture length. Once the Reference is usable, you may stop playback to prepare the Target without losing the Reference.

For the complete walkthrough, control descriptions, troubleshooting, and import/export instructions, see **[MANUAL.md](MANUAL.md)**.

## After the match

A traditional match EQ often feels finished once it has drawn its curve. Tone Trace treats that curve more like the beginning of an EQ session.

**Match Mode** can be changed after Correct or Freeze, so the same Reference and Target can be heard through different matching approaches without another capture. **Correction Resolution** changes how many bands you have available for manual work without throwing away the higher-resolution learned relationship. **Correction Range Low/High** lets you stop Tone Trace from correcting parts of the spectrum that do not contain useful material. The band pages themselves behave like a graphic EQ: they can be adjusted with the mouse or keyboard and a band can be returned to 0 dB directly.

On Windows, Correction Resolution is available both from the host parameter
view and from a labeled control on the Bands pages. Those pages retain complete
names such as **Bands 1-10** at every resolution, and nearby frequency labels
remain distinct.

For speech and microphone work, Tone Trace also watches for a particular failure that can happen when two different performances contain very different formants or resonances. Voice mode keeps useful detail when it is well supported, but backs away from unusually narrow detail when that detail would turn into a ringy or phasey-sounding correction. This happens automatically; there is no extra safety mode to manage.

For unusually severe restoration work, the normal **Maximum Correction** limit can be raised or bypassed with **Full Correction Range**. Tone Trace will allow large corrections when they are genuinely called for, but it does not pretend that large boosts are free: leave appropriate headroom and use your preferred dynamics processing afterward when needed.

**Custom Max Capture** extends the accepted-audio ceiling from the standard 30 seconds to about 60 seconds per Reference or Target. It is intended for unusually varied material, difficult restoration, or comparisons that benefit from a longer observation window—not as an automatic quality setting. Representative material still matters more than simply filling the buffer.

## Live use and impulse responses

Once Tone Trace has learned the correction, Preview and Frozen operation report **zero samples of plug-in latency** and use no lookahead. That makes a learned match practical in monitoring, broadcast, voice, and other real-time paths instead of limiting it to offline analysis.

A learned correction can be exported as a minimum-phase impulse response at common production sample rates. You can also export a curve created by hand with the band controls or Correction Gain before learning a match. Tone Trace asks for confirmation when exporting a manually created curve. A completely flat unmatched instance still has nothing to export.

## Accessibility

The shared JUCE editor provides Capture and transfer, Correction controls, and
Bands pages. Its graph has an exact Curve Readout, a readable Curve Description,
and Copy Curve Description. Trace Curve is available by its toggle or T/F2.
Band Left/Right moves focus; Up/Down changes gain. Enter opens exact value entry.

Values retain up to three meaningful
decimal places and use a dB string instead of a percentage range. Host and screen-reader combinations can still expose integration differences, so accessibility feedback is welcome.

With NVDA, band navigation is intentionally concise. The read-only curve text
is explicitly labeled **Curve Description**. Reference and Target measurements
use higher/lower wording, while boost/cut is reserved for the Correction Tone
Trace actually applies. The Capture page has a labeled
multiline **Status** field plus an **Options...** button for less-frequently
changed controls. Global controls and the single persistent **Workflow Step**
remain available through the host's generic parameter interface, including
REAPER with OSARA. Optional tone notifications provide additional nonvisual
feedback and are enabled by default.

Accessibility feedback is especially welcome at `info@lanesaudio.com`.

## Formats

The Windows 1.1 port builds **VST3 and CLAP** from one JUCE processor and editor.
Version 1.1.0 has one project-built release asset:
`ToneTrace_EQ_1.1.0_Windows_x64.zip`. Per-file SHA-256 hashes are stored inside
that ZIP in `docs/BUILD_MANIFEST.txt`.

## Tone Trace is an EQ matcher, not a limiter

A learned curve can contain substantial boosts, so leave sensible headroom. **Correction Gain** is available for level management, and the plug-in includes an **Emergency Clip Guard** as a last-resort hard ceiling. That guard is deliberately not presented as a transparent mastering limiter.

Exported IRs remain linear and do not include the Emergency Clip Guard. If the finished signal needs dynamics control, use the limiter you prefer downstream.

## Documentation

- **[MANUAL.md](MANUAL.md)** — using Tone Trace, start to finish.
- **[DESIGN.md](DESIGN.md)** — matching engine, model, state, and DSP design.
- **[GUI_DESIGN.md](GUI_DESIGN.md)** — shared editor and accessibility design.
- **[plugins/README.md](plugins/README.md)** — wrapper implementation and host-boundary details.
- **[CHANGELOG.md](CHANGELOG.md)** — versioned user-visible changes.
- **[RELEASE_NOTES.md](RELEASE_NOTES.md)** — release summary, asset contents, and installation.
- **[RELEASING.md](RELEASING.md)** — deterministic build, verification, tag, and upload procedure.
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — build, testing, and accessibility invariants for changes.
- **[SECURITY.md](SECURITY.md)** — private vulnerability-reporting policy.

## Building and testing

Windows builds require JUCE 8.0.12 and clap-juce-extensions commit
`55525c9858d4b25687be7759a5e0f70eccef218e`. CMake fetches these on the first build;
Git and network access are required. For an offline build, supply `JUCE_DIR`
and `CLAP_JUCE_EXTENSIONS_DIR` pointing to complete local checkouts (including
the adapter's CLAP and clap-helpers submodules). Dependency versions and local
compatibility adjustments are described in [plugins/README.md](plugins/README.md).

### Windows

Requirements are CMake 3.25 or newer, Git, Visual Studio 2022 Build Tools with **Desktop development with C++**, and a Windows SDK. Run:

```bat
build_all_windows.bat
```

The builder starts clean, compiles the plug-in plus its verification targets,
runs every registered CTest, stages both plug-ins and documentation, writes
an internal SHA-256 build manifest, creates the ZIP, and verifies the archive.
It does not install or replace plug-ins on the machine.

### Other platforms

`CMakePresets.json` contains macOS universal and Linux verification presets for
developers. They retain the legacy CLAP wrapper by default. The JUCE port and
this release package are validated on Windows x64 only; no macOS/Linux binary
release is claimed.

### Developer path

For engine and Linux CLAP development on a system with a C++20 compiler and GNU Make:

```sh
make
make test
```

The Makefile builds the legacy CLAP wrapper. Use the Windows CMake build for the
JUCE VST3/CLAP release; both paths retain the released matching engine.

## Release status

Version **1.1.0** is the current Windows release and the first release in which VST3 and CLAP share the JUCE processor and editor. Automated coverage includes the matching engine, WAV/model round trips, real-time renderer, project-state validation, workflow, balanced band-page layout, keyboard/accessibility-facing behavior, visual snapshots, and malformed-state rejection.

The release preserves a last-known-good correction when a replacement capture or saved state fails validation. Raw captured audio is never stored in the project.

Host and OS combinations can still expose integration bugs, so useful reports should include the plug-in format, host/version, operating system, and the exact workflow step involved.

## Useful pieces for other plug-in developers

Tone Trace's source contains more than the plug-in itself. A fair amount of reusable infrastructure grew around accessibility, regression testing, visual validation, and repeatable builds. If any of it makes another audio plug-in easier to test or more accessible, please study it and adapt the ideas to your own project. Some of the most useful parts of Tone Trace may be things users never see.

In particular, the JUCE tree includes keyboard- and screen-reader-oriented controls, focus and paging tests, project-state round-trip tests, source-contract checks, and a visual snapshot harness that renders the real editor offscreen at representative sizes. The accessibility code is included as working examples rather than a framework, and the visual harness is deliberately based on JUCE rather than Win32 so it can remain useful to future non-Windows work.

We benefited from being able to inspect mature open-source audio projects while building Tone Trace. Keeping these pieces readable is one way of returning that favor.

## License

Tone Trace EQ v1.0.0 and later are **source-available** under the Apache License 2.0 with the Commons Clause License Condition v1.0. See **[LICENSE](LICENSE)**.

You are explicitly welcome to use Tone Trace for personal or commercial **audio work**. This includes paid production and mastering, commercial radio and broadcasting, monetized streams and podcasts, released music, and similar work. Lanes Audio claims no royalty or ownership interest in audio merely because Tone Trace processed it.

The source may be inspected, studied, modified, and redistributed subject to the license. However, the license does **not** permit selling Tone Trace itself, a rebranded or lightly modified version of it, or another product or service whose value derives entirely or substantially from Tone Trace's software functionality. Contact `info@lanesaudio.com` to discuss a separate commercial software license.

JUCE, the CLAP adapter, and bundled third-party code retain their own licenses.
The package includes dependency notices under `licenses/`. The project license
does not replace the separate JUCE licensing terms. The intended licensing basis
for the official port is JUCE Starter, subject to its eligibility rules. See
[JUCE 8 terms](https://juce.com/legal/juce-8-licence/); anyone rebuilding or
distributing a derivative must meet the applicable JUCE terms themselves.

## Acknowledgements


Tone Trace was inspired by **[Spectrum Matcher](https://geraintluff.github.io/jsfx/)**, a REAPER JSFX by **Geraint Luff** that analyzes the spectral difference between an input and a reference and can apply an EQ correction from that comparison. Using Spectrum Matcher planted the seed for Tone Trace’s original match-EQ concept. Tone Trace later grew in its own direction because I wanted a more accessible capture workflow, an editable correction model, and tools for importing/exporting curves created by tone trace.

Development also benefited from studying **Surge XT** and other mature open-source audio projects. Surge was especially useful as a practical reference for where JUCE already provides solid behavior, where accessibility or host integration needs explicit attention, and where our own early solutions were becoming more complicated than they needed to be. That kind of open source visibility helped Tone Trace, and it is one reason the project's own accessibility and regression-testing infrastructure is kept readable for other developers to study and adapt.

## Contact and support

Questions, bug reports, accessibility feedback, and commercial software licensing: `info@lanesaudio.com`

Tone Trace is free to use for audio work. If it saves you time and you would like to support continued development, donations are welcome but never required:

[Donate via PayPal](http://paypal.me/dgl1984)
