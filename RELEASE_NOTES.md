# Tone Trace EQ 1.1.0 — release notes

Tone Trace EQ 1.1.0 moves to JUCE, a cross-platform foundation that makes future
support for additional operating systems more practical. This release brings
Windows VST3 alongside CLAP, with both formats sharing the same editor and
processing implementation.

The familiar matching engine and workflow are preserved. Achieving that
continuity required substantial engineering: bringing keyboard navigation,
screen-reader behavior, audio processing, saved projects, and host integration
across to the new framework, then testing those pieces together. Much of that
work is deliberately invisible in everyday use—the goal was for Tone Trace to
keep feeling like Tone Trace.

This release supports Windows x64. The new foundation prepares the project for
future platform work.

Download `ToneTrace_EQ_1.1.0_Windows_x64.zip` for both plug-ins, documentation,
dependency notices, and the per-file SHA-256 build manifest. Copy the CLAP file
to `C:\Program Files\Common Files\CLAP\`, and the complete VST3 bundle to
`C:\Program Files\Common Files\VST3\`, then rescan in your host.

Back up projects before upgrading. The CLAP port reads legacy Tone Trace states,
but states saved by 1.1.0 cannot be reopened by 1.0.5. VST3 is a separate plug-in
format and does not automatically replace CLAP instances.
