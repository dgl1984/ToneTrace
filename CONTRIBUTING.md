# Contributing to Tone Trace EQ

Bug reports and focused pull requests are welcome. By contributing, you agree
that your contribution is distributed under the repository's `LICENSE`.

## Report a bug

Include:

- operating system and architecture;
- host name and version;
- exact Tone Trace version;
- the workflow step and minimal reproduction;
- whether the native editor or host parameter view was used;
- for accessibility issues, the screen reader and bridge (for example NVDA and
  OSARA) plus the spoken result you expected and received.

Do not attach private audio unless you have permission to share it. A synthetic
or short redacted fixture is preferable.

## Build and test

Windows release verification:

```bat
build_all_windows.bat
```

Portable developer verification with a C++20 compiler:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Pull-request requirements

- Keep DSP and state logic in the shared engine/core; wrappers translate host
  events and do not duplicate matching logic.
- Do not allocate, lock, perform file I/O, or analyze on the audio thread.
- Preserve parameter IDs and serialized-state compatibility.
- Treat the CLAP parameter surface as authoritative.
- Visual editor changes must preserve native control roles, accessible names,
  tab order, keyboard behavior, and concise announcements.
- Add or update tests and documentation for user-visible behavior.
- Run all registered tests before requesting review.

Formatting-only rewrites should be separate from functional changes so audio,
state, and accessibility behavior remains reviewable.
