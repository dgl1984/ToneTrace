# Releasing Tone Trace EQ

## Prepare the release

Agree the changelog and release notes before pushing a new version to `main`.
Set `VERSION` to `MAJOR.MINOR.PATCH`, update the user-facing version text, and
save `RELEASE_NOTES.md` as UTF-8 without a BOM. Keep the macOS/VoiceOver testing
status explicit; automated builds do not establish screen-reader usability.

Validate the notes locally:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_release_notes.ps1
```

## Automatic publication

After a push to `main`, CI builds and validates Windows and macOS packages and
runs Linux verification. Only when all three jobs succeed does the release job
publish a new version. Pull requests never publish releases.

The release job reads `VERSION` and `RELEASE_NOTES.md` from the tested commit,
downloads both packages from that same workflow run, and checks their SHA-256
files. It creates `v<version>` at that commit and a draft release containing:

- `ToneTrace_EQ_<version>_Windows_x64.zip`
- `ToneTrace_EQ_<version>_macOS_Universal.dmg`

It downloads the uploaded assets and compares them with the originals before
making the release public. Both platforms appear together on the Releases page;
users do not need an Actions artifact or a GitHub login to download them.

An existing release is never overwritten. To publish another update, bump
`VERSION` and update the notes. Ordinary pushes with the same version still run
CI but do not replace public downloads. A failed upload or verification may leave
a draft; inspect and repair that draft before publishing it. A rerun leaves
existing drafts unchanged too.

## Verify and share

Check the release job result and both downloads on the Releases page. The body
must match the agreed file:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_release_notes.ps1 -Tag v<version>
gh release view v<version> --json tagName,body,assets
```

GitHub also provides automatic source archives. These are separate from the
Windows ZIP and macOS installer DMG. Build logs and validator reports remain in
Actions for diagnosis; they are not user installation packages.

For local Windows verification, use `build_all_windows.bat`. For macOS, use
`bash BUILD_MACOS.sh` on a Mac. See `TESTING.md` for the manual VoiceOver checks.
