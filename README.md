# Tone Trace EQ

**A match EQ you can keep working with.**

If you already know what an EQ does, Tone Trace starts from familiar ground. A normal EQ gives you bands and asks you to decide what needs more or less. A match EQ can take a different route: give it one sound you like and one sound you want to change, and it can learn the tonal difference for you.

Tone Trace takes that idea further. It sits somewhere between a match EQ, a hands-on graphic EQ, and a reusable correction tool. None of those ideas is unusual by itself; the useful part is that they stay connected. You capture a **Reference**—the sound you want—and a **Target**—the sound you want to correct. Tone Trace listens to both, builds the match, and then gives that result back to you as something you can continue to work with rather than a one-shot calculation.

That distinction matters in practice. You can audition the match, change how Tone Trace interprets the same captures, narrow the frequency range, choose a finer or broader set of editable bands, and make your own adjustments without recording the source again. When the correction is finished, it can stay in a live signal path with zero reported plug-in latency and no lookahead, or it can be exported as an impulse response for use elsewhere.

The result is useful anywhere two recordings ought to live in the same tonal world: matching microphones, bringing voice recordings from different chains closer together, restoring old material toward a cleaned-up reference, evening out production changes between sessions, or simply getting to a difficult EQ starting point faster than drawing the curve by hand.

Tone Trace EQ is a free match EQ from **Lanes Audio** and ships as a CLAP plug-in.

## Download and install

The 1.0.3 binary release is **Windows 10/11 x64**. Download the one
release asset named `ToneTrace_EQ_1.0.3_Windows_x64.zip`, extract it, and copy
`plugins\clap\Tone Trace EQ.clap` to:

`C:\Program Files\Common Files\CLAP\`

Then rescan CLAP plug-ins in the host. The ZIP also contains the complete
documentation, licenses, and `docs\BUILD_MANIFEST.txt` with a SHA-256 hash for
every packaged file. It contains no installer, developer tools, tests, or debug
symbols, and it changes nothing until you copy the `.clap` file.

Source builds are possible on other C++20 platforms, but 1.0.3 does not claim a
supported non-Windows binary release or native non-Windows custom editor.

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

A match does not become untouchable after Freeze. Tone Trace keeps the captured relationship available, so you can continue refining the correction without starting the capture process over.

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

## Live use and impulse responses

Once Tone Trace has learned the correction, Preview and Frozen operation report **zero samples of plug-in latency** and use no lookahead. That makes a learned match practical in monitoring, broadcast, voice, and other real-time paths instead of limiting it to offline analysis.

A learned correction can be exported as a minimum-phase impulse response at common production sample rates. You can also export a curve created by hand with the band controls or Correction Gain before learning a match. Tone Trace asks for confirmation when exporting a manually created curve. A completely flat unmatched instance still has nothing to export.

## Accessibility

The Windows editor was designed to be usable both visually and nonvisually. The Match page is mouse-operable, the graphic-EQ bands have conventional visual faders, and the same controls remain keyboard accessible without adding extra focus stops or verbose screen-reader chatter.

Band values report the same dB value everywhere: in the visible control,
exact-value field, NVDA, and Narrator. Values retain up to three meaningful
decimal places, and Narrator does not substitute percentages.

With NVDA, band navigation is intentionally concise. The read-only curve text
is explicitly labeled **Curve Description**, and global controls and workflow
status are also exposed through the host's generic parameter interface,
including workflows such as REAPER with OSARA. Optional tone notifications can
provide additional nonvisual feedback and are enabled by default.

Accessibility feedback is especially welcome at `info@lanesaudio.com`.

## Formats

Tone Trace 1.0 ships as **CLAP**.

The Windows CLAP build includes the native accessible editor. The custom editor
is Windows-only; non-Windows source builds use the host's CLAP parameter view.

Version 1.0.3 has one release asset:
`ToneTrace_EQ_1.0.3_Windows_x64.zip`. Per-file SHA-256 hashes are stored inside
that ZIP in `docs/BUILD_MANIFEST.txt`.

## Tone Trace is an EQ matcher, not a limiter

A learned curve can contain substantial boosts, so leave sensible headroom. **Correction Gain** is available for level management, and the plug-in includes an **Emergency Clip Guard** as a last-resort hard ceiling. That guard is deliberately not presented as a transparent mastering limiter.

Exported IRs remain linear and do not include the Emergency Clip Guard. If the finished signal needs dynamics control, use the limiter you prefer downstream.

## Documentation

- **[MANUAL.md](MANUAL.md)** — using Tone Trace, start to finish.
- **[DESIGN.md](DESIGN.md)** — matching engine, model, state, and DSP design.
- **[GUI_DESIGN.md](GUI_DESIGN.md)** — native editor and accessibility design.
- **[plugins/README.md](plugins/README.md)** — wrapper implementation and host-boundary details.
- **[CHANGELOG.md](CHANGELOG.md)** — versioned user-visible changes.
- **[RELEASE_NOTES.md](RELEASE_NOTES.md)** — 1.0.3 summary, asset contents, and installation.
- **[RELEASING.md](RELEASING.md)** — deterministic build, verification, tag, and upload procedure.
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — build, testing, and accessibility invariants for changes.
- **[SECURITY.md](SECURITY.md)** — private vulnerability-reporting policy.

## Building and testing

Tone Trace vendors the CLAP 1.2.10 headers under `third_party`, with the upstream license notice, so a normal release build does not download SDK code.

### Windows

Requirements are CMake 3.25 or newer, Visual Studio 2022 Build Tools with **Desktop development with C++**, and a Windows SDK. Run:

```bat
build_all_windows.bat
```

The builder starts clean, compiles the plug-in plus its verification targets,
runs every registered CTest, stages only the plug-in and documentation, writes
an internal SHA-256 build manifest, creates the ZIP, and verifies the archive.
It does not install or replace plug-ins on the machine.

### Other platforms

`CMakePresets.json` contains macOS universal and Linux verification presets for
developers. There is no 1.0.3 macOS/Linux release builder or supported binary
asset, and the custom native editor remains Windows-only.

### Developer path

For engine and Linux CLAP development on a system with a C++20 compiler and GNU Make:

```sh
make
make test
```

The Makefile and CMake paths build the same CLAP wrapper and shared engine used by the release builders.

## 1.0 status

Version **1.0.0** is the first stable public source release. Automated coverage includes the matching engine, WAV/model round trips, real-time renderer, project-state validation, CLAP workflow, balanced band-page layout, accessibility-facing parameter behavior, and malformed-state rejection.

The release also preserves a last-known-good correction when a replacement capture or saved state fails validation. Raw captured audio is never stored in the project.

Host and OS combinations can still expose integration bugs, so useful reports should include the plug-in format, host/version, operating system, and the exact workflow step involved.

## License

Tone Trace EQ v1.0.0 and later are **source-available** under the Apache License 2.0 with the Commons Clause License Condition v1.0. See **[LICENSE](LICENSE)**.

You are explicitly welcome to use Tone Trace for personal or commercial **audio work**. This includes paid production and mastering, commercial radio and broadcasting, monetized streams and podcasts, released music, and similar work. Lanes Audio claims no royalty or ownership interest in audio merely because Tone Trace processed it.

The source may be inspected, studied, modified, and redistributed subject to the license. However, the license does **not** permit selling Tone Trace itself, a rebranded or lightly modified version of it, or another product or service whose value derives entirely or substantially from Tone Trace's software functionality. Contact `info@lanesaudio.com` to discuss a separate commercial software license.

Vendored CLAP files retain their upstream license under `third_party`.

## Contact and support

Questions, bug reports, accessibility feedback, and commercial software licensing: `info@lanesaudio.com`

Tone Trace is free to use for audio work. If it saves you time and you would like to support continued development, donations are welcome but never required:

[Donate via PayPal](https://paypal.me/dgl1984)
