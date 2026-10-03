# Tone Trace EQ 1.1.0 — release notes

This is a port of Tone Trace's existing feature set to JUCE for Windows VST3
and CLAP. The 1.0.5 matching engine and workflow are preserved; no new matching
features are introduced.

The port restores the released keyboard controls, curve descriptions/readouts,
and accessible band values, and corrects port regressions in navigation tones,
imported-reference capture, project-state handling, Options-dialog focus, and
initial Import/Export menu selection for screen readers. Documentation now covers
both formats and the shared editor.

Download `ToneTrace_EQ_1.1.0_Windows_x64.zip` for both plug-ins, documentation,
dependency notices, and the per-file SHA-256 build manifest. Copy the CLAP file
to `C:\Program Files\Common Files\CLAP\`, and the complete VST3 bundle to
`C:\Program Files\Common Files\VST3\`, then rescan in your host.

Back up projects before upgrading. The CLAP port reads legacy Tone Trace states,
but states saved by 1.1.0 cannot be reopened by 1.0.5. VST3 is a separate plug-in
format and does not automatically replace CLAP instances.

