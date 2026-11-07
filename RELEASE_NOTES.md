# Tone Trace EQ 1.3 — release notes

Tone Trace 1.3 restores safe correction controls to host automation and REAPER/OSARA's effects parameter interface. The package version is **1.3.0**.

## Restored parameter access

Maximum Correction, Strength, frequency range (Low and High), Q / Sharpness, and Full Correction Range are exposed again. Correction Gain, Emergency Clip Guard, notification level and enable, and Bypass remain automatable. Parameter IDs and saved values are unchanged.

The restored correction controls use background rebuilds. Changes may arrive late and rapid edits may be combined into the latest setting; they do not provide sample-accurate automation. Use static settings and let the correction finish updating before offline rendering.

Match Mode, Resolution, and Workflow Step remain non-automatable because they change capture interpretation, resample manual band edits, or run capture/analysis/reset commands. Their editor controls remain available.

## Consistent button wording

**Capture Reference** is now **Learn Reference**, matching **Learn Target**. The button's spoken label and the host's workflow label use the same wording. Its behavior is unchanged.

## macOS status — untested update

This update uses Victor Tsaran's macOS port as its base for AU, VST3, and CLAP support. His VoiceOver accessibility implementation is retained, with the Reference button wording updated as described above. This version has **not been tested on a Mac**, including with VoiceOver; testing of the original port does not verify this update.

## Windows installation

The Windows 10/11 x64 package is `ToneTrace_EQ_1.3.0_Windows_x64.zip`. Extract it and copy `plugins\clap\Tone Trace EQ.clap` to `C:\Program Files\Common Files\CLAP\`, or the entire `plugins\vst3\Tone Trace EQ.vst3` folder to `C:\Program Files\Common Files\VST3\`, then rescan in your host.

Keep project backups before upgrading. VST3 and CLAP remain separate plug-in identities. This update retains the 1.2 state format; those states cannot be reopened by 1.0.5.
