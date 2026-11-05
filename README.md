# Tone Trace EQ

Tone Trace EQ is a free match EQ from **Lanes Audio** for Windows (VST3 and CLAP). It compares the tone of two recordings and builds an EQ curve you can adjust by ear.

Say you need to replace a line of narration, but the microphone you have today sounds brighter than the one you used for the session. Play a few lines from the original recording as your **Reference**, then play the new take as your **Target**. Tone Trace works out an EQ curve for the new take. Play it alongside the original and listen to how the edit sounds.

The same approach works with instruments, mixes, and restoration work. Choose passages that give you a fair comparison. A muted trumpet and an open trumpet will sound different even through the same microphone. The [manual](MANUAL.md#choosing-material-and-a-mode) has more examples.

## Working with the result

If the new take sounds closer but a little too dark, ease back **Correction Strength**. If only the top end needs work, limit the correction to that range. You can try another matching mode with the same captures or adjust individual frequencies with up to 120 bands. Those bands also work as a graphic EQ on their own.

The graph shows what Tone Trace measured and where it applied EQ. **Curve Description** gives you the comparison in words. With **Trace Curve** on, moving from band to band plays a tone at each band's frequency. You can capture both recordings, read the results, and enter exact adjustments using a keyboard and screen reader.

When the match sounds right, choose **Freeze Correction**. You can still edit it later, and your project saves the curve and your adjustments. A finished correction works live with no lookahead and zero reported plug-in latency. You can also save the Reference for the next session, save the correction model, or export an impulse response to use in a convolution processor.

## Download and install

Get the public packages from [GitHub Releases](https://github.com/dgl1984/ToneTrace/releases). This guide covers **Tone Trace 1.2** for **Windows 10/11 x64**. The package version is **1.2.0**. See the [release notes](RELEASE_NOTES.md) for package details and upgrade information.

Extract the Windows ZIP and copy the format your host uses:

- **CLAP:** copy `plugins\clap\Tone Trace EQ.clap` to `C:\Program Files\Common Files\CLAP\`.
- **VST3:** copy the complete `plugins\vst3\Tone Trace EQ.vst3` folder to `C:\Program Files\Common Files\VST3\`. Keep the bundle together.

Rescan plug-ins in your host if needed. Both formats have the same matching features and editor. For your first session, follow [Your first match](MANUAL.md#your-first-match).

## Documentation

- [User manual](MANUAL.md): installation, capture, editing, keyboard operation, and troubleshooting.
- [Changelog](CHANGELOG.md) and [release notes](RELEASE_NOTES.md): changes and upgrade information, including automation compatibility.
- [Contributing](CONTRIBUTING.md): building, testing, and working on the source.

## License

Tone Trace EQ v1.0.0 and later are **source-available** under the Apache License 2.0 with the Commons Clause License Condition v1.0. See **[LICENSE](LICENSE)**.

You are explicitly welcome to use Tone Trace for personal or commercial **audio work**. This includes paid production and mastering, commercial radio and broadcasting, monetized streams and podcasts, released music, and similar work. Lanes Audio claims no royalty or ownership interest in audio merely because Tone Trace processed it.

The source may be inspected, studied, modified, and redistributed subject to the license. However, the license does **not** permit selling Tone Trace itself, a rebranded or lightly modified version of it, or another product or service whose value derives entirely or substantially from Tone Trace's software functionality. Contact `info@lanesaudio.com` to discuss a separate commercial software license.

## Credits

**[Victor Tsaran (vick08)](https://github.com/vick08)** contributed the macOS port, including universal Audio Unit, VST3, and CLAP builds for Apple Silicon and Intel Macs, installer packaging, and macOS build and validation checks in GitHub Actions.

Tone Trace was inspired by **[Spectrum Matcher](https://geraintluff.github.io/jsfx/)**, a REAPER JSFX by **Geraint Luff** that analyzes the spectral difference between an input and a reference and can apply an EQ correction from that comparison. Using Spectrum Matcher planted the seed for Tone Trace’s original match-EQ concept. Tone Trace later grew in its own direction because I wanted a more accessible capture workflow, an editable correction model, and tools for importing/exporting curves created by Tone Trace.

Development also benefited from studying **Surge XT** and other mature open-source audio projects. Surge was especially useful as a practical reference for where JUCE already provides solid behavior, where accessibility or host integration needs explicit attention, and where our own early solutions were becoming more complicated than they needed to be. That kind of open source visibility helped Tone Trace, and it is one reason the project's own accessibility and regression-testing infrastructure is kept readable for other developers to study and adapt.

### Design and AI-written code

Finally, if a program does more than say “Hello, world!” and/or looks well-designed visually, every line responsible for such elevated standards was coded by AI—but the design is mine.

## Contact and support

Questions, bug reports, accessibility feedback, and commercial software licensing: `info@lanesaudio.com`.

If Tone Trace saves you time and you would like to support its development, [donations are welcome](http://paypal.me/dgl1984), but never required.
