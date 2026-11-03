# Tone Trace EQ 1.2 — release notes

Windows downloads are on [GitHub Releases](https://github.com/dgl1984/ToneTrace/releases).

Tone Trace 1.2 redesigns the editor and makes the capture process more informative. You can read a Reference spectrum report before starting the Target, inspect the correction with clearer graph labels, and work across band pages that fit the window. The matching engine remains unchanged.

## What changes in everyday use

**Curve Description** follows the work from capture through the finished match. A usable Reference gets its own report. During analysis, the Target report can appear before the completed comparison. Reports are prepared in the background, so they may take a moment to arrive.

The graph and readout now agree on **Correction + gain**, including the global output trim. Reference and Target curves show their relative tonal shape. Frequency and dB axes make the display easier to read, and the dashed Target line helps distinguish the captures.

The editor fits down to 720 × 640, keeps Bypass available across pages, and adjusts the number of bands per page to the available space. Spoken announcements go through the screen reader's accessibility system.

This version also fixes capture and correction failures, including a crash caused by host parameter changes during correction completion. Full captures stop doing unnecessary capture work, reports reuse completed results, and raw capture storage is allocated when needed. See the [changelog](CHANGELOG.md) for the full list.

## Automation compatibility

Correction Gain, Emergency Clip Guard, notification level and enable, and Bypass support automation. Match Mode, Maximum Correction, Strength, Resolution, frequency range, Q / Sharpness, Full Correction Range, and Workflow Step need background work and are no longer advertised as automatable.

Those controls keep their parameter IDs and saved values, but existing automation lanes may behave differently in your host. Set them before playback or rendering, and wait for analysis to finish before exporting or rendering a changed correction. Bypass still keeps the correction processor running for a smooth return to the effect.

## Package and installation

The Windows 10/11 x64 package is `ToneTrace_EQ_1.2.0_Windows_x64.zip`. The product version is **1.2** and the package version is **1.2.0**.

Extract the ZIP. Copy `plugins\clap\Tone Trace EQ.clap` to `C:\Program Files\Common Files\CLAP\`, or the entire `plugins\vst3\Tone Trace EQ.vst3` folder to `C:\Program Files\Common Files\VST3\`, then rescan in your host. Documentation, licenses, and per-file hashes are included.

Keep project backups before upgrading. VST3 and CLAP remain separate plug-in identities. States saved by 1.2 cannot be reopened by 1.0.5.
