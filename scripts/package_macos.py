#!/usr/bin/env python3
"""Sign a staged ToneTrace macOS release and build its installer and disk image.

The disk image holds "Install Tone Trace EQ.pkg" beside the manual and licences.
The installer places the three plug-in bundles (VST3, CLAP, AU). Each bundle carries
its own licences and notice, so deleting a bundle uninstalls everything it installed.

Signing is ad hoc unless a Developer ID is supplied through the environment:
  TONETRACE_CODESIGN_IDENTITY   "Developer ID Application: ..." (default "-": ad hoc)
  TONETRACE_INSTALLER_IDENTITY  "Developer ID Installer: ..." (default: unsigned package)
  TONETRACE_NOTARY_PROFILE      notarytool keychain profile (default: not notarized)
"""
from __future__ import annotations

import argparse
import hashlib
import os
import plistlib
import shutil
import subprocess
import tempfile
from pathlib import Path
from xml.sax.saxutils import escape

from stage_release import MACOS_AU_NAME, MACOS_VST3_NAME, MACOS_CLAP_NAME, validate_public_tree

PLUGIN_BUNDLES = (
    (MACOS_VST3_NAME, "/Library/Audio/Plug-Ins/VST3", "VST3 plug-in"),
    (MACOS_CLAP_NAME, "/Library/Audio/Plug-Ins/CLAP", "CLAP plug-in"),
    (MACOS_AU_NAME, "/Library/Audio/Plug-Ins/Components", "Audio Unit plug-in (Logic, GarageBand)"),
)
DISK_IMAGE_ITEMS = ("docs", "licenses", "LICENSE")
INSTALLER_NAME = "Install Tone Trace EQ.pkg"
IDENTIFIER = "com.lanesaudio.tonetrace-eq"
MINIMUM_MACOS = "12.0"

# Trigger macOS AudioComponentRegistrar rescan so hosts discover the new AU immediately.
AU_POSTINSTALL = """#!/bin/sh
/usr/bin/killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
"""


def die(message: str) -> "NoReturn":
    raise SystemExit(f"ERROR: {message}")


def run(*args: str) -> None:
    print("+ " + " ".join(args))
    subprocess.run(args, check=True)


def sign_bundle(bundle: Path, identity: str) -> None:
    command = ["codesign", "--force", "--sign", identity]
    if identity != "-":
        command += ["--options", "runtime", "--timestamp"]
    run(*command, str(bundle))
    run("codesign", "--verify", "--strict", "--deep", "--verbose=2", str(bundle))


def component_package(bundle: Path, location: str, identifier: str, scripts: Path | None, out: Path,
                      version: str) -> None:
    with tempfile.TemporaryDirectory(prefix="tonetrace-pkg-") as td:
        root = Path(td) / "root"
        root.mkdir()
        shutil.copytree(bundle, root / bundle.name, symlinks=True)
        components = Path(td) / "components.plist"
        run("pkgbuild", "--analyze", "--root", str(root), str(components))
        with components.open("rb") as f:
            entries = plistlib.load(f)
        for entry in entries:
            entry["BundleIsRelocatable"] = False
            entry["BundleOverwriteAction"] = "upgrade"
        with components.open("wb") as f:
            plistlib.dump(entries, f)
        command = ["pkgbuild", "--root", str(root), "--component-plist", str(components),
                   "--install-location", location, "--identifier", identifier, "--version", version]
        if scripts is not None:
            command += ["--scripts", str(scripts)]
        run(*command, str(out))


def distribution_xml(version: str, packages: list[tuple[str, str, str]]) -> str:
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<installer-gui-script minSpecVersion="2">',
        f'    <title>Tone Trace EQ {escape(version)}</title>',
        '    <license file="LICENSE"/>',
        '    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>',
        '    <domains enable_localSystem="true"/>',
        f'    <os-version min="{MINIMUM_MACOS}"/>',
        '    <choices-outline>',
    ]
    lines += [f'        <line choice="{identifier}"/>' for identifier, _, _ in packages]
    lines.append('    </choices-outline>')
    for identifier, _, title in packages:
        lines.append(f'    <choice id="{identifier}" title="{escape(title)}" visible="true">')
        lines.append(f'        <pkg-ref id="{identifier}"/>')
        lines.append('    </choice>')
    for identifier, file_name, _ in packages:
        lines.append(f'    <pkg-ref id="{identifier}" version="{escape(version)}">{escape(file_name)}</pkg-ref>')
    lines.append('</installer-gui-script>')
    return "\n".join(lines) + "\n"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description="Sign staged ToneTrace macOS release and build its disk image.")
    ap.add_argument("--portable", type=Path, required=True, help="Staged release directory from stage_release.py.")
    ap.add_argument("--output-dir", type=Path, required=True, help="Directory for the .dmg and SHA256SUMS.txt.")
    ap.add_argument("--version", required=True, help="ToneTrace version, e.g. 1.2.0.")
    args = ap.parse_args()

    portable = args.portable.resolve()
    validate_public_tree(portable, au=True)

    identity = os.environ.get("TONETRACE_CODESIGN_IDENTITY", os.environ.get("SPACETRACE_CODESIGN_IDENTITY", "-"))
    installer_identity = os.environ.get("TONETRACE_INSTALLER_IDENTITY", os.environ.get("SPACETRACE_INSTALLER_IDENTITY", ""))
    notary_profile = os.environ.get("TONETRACE_NOTARY_PROFILE", os.environ.get("SPACETRACE_NOTARY_PROFILE", ""))
    if notary_profile and (identity == "-" or not installer_identity):
        die("notarization requires Developer ID Application and Developer ID Installer identities.")

    for bundle, _, _ in PLUGIN_BUNDLES:
        sign_bundle(portable / bundle, identity)

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    image = output_dir / f"ToneTrace_EQ_{args.version}_macOS_Universal.dmg"

    with tempfile.TemporaryDirectory(prefix="tonetrace-product-") as td:
        work = Path(td)
        packages: list[tuple[str, str, str]] = []
        au_scripts = work / "au-scripts"
        au_scripts.mkdir()
        (au_scripts / "postinstall").write_text(AU_POSTINSTALL, encoding="ascii")
        (au_scripts / "postinstall").chmod(0o755)

        for bundle, location, title in PLUGIN_BUNDLES:
            suffix = Path(bundle).suffix.lstrip(".").lower()
            identifier = f"{IDENTIFIER}.{suffix}"
            file_name = f"ToneTraceEQ-{suffix}.pkg"
            scripts = au_scripts if bundle == MACOS_AU_NAME else None
            component_package(portable / bundle, location, identifier, scripts, work / file_name, args.version)
            packages.append((identifier, file_name, title))

        resources = work / "resources"
        resources.mkdir()
        shutil.copy2(portable / "LICENSE", resources / "LICENSE")
        distribution = work / "distribution.xml"
        distribution.write_text(distribution_xml(args.version, packages), encoding="utf-8")

        volume = work / f"Tone Trace EQ {args.version}"
        volume.mkdir()
        installer = volume / INSTALLER_NAME
        command = ["productbuild", "--distribution", str(distribution), "--package-path", str(work),
                   "--resources", str(resources)]
        if installer_identity:
            command += ["--sign", installer_identity, "--timestamp"]
        run(*command, str(installer))
        if notary_profile:
            run("xcrun", "notarytool", "submit", str(installer), "--keychain-profile", notary_profile, "--wait")
            run("xcrun", "stapler", "staple", str(installer))

        for name in DISK_IMAGE_ITEMS:
            source = portable / name
            if source.is_dir():
                shutil.copytree(source, volume / name)
            else:
                shutil.copy2(source, volume / name)

        if image.exists():
            image.unlink()
        run("hdiutil", "create", "-volname", volume.name, "-srcfolder", str(volume),
            "-format", "UDZO", "-imagekey", "zlib-level=9", "-ov", str(image))

    if identity != "-":
        run("codesign", "--force", "--sign", identity, "--timestamp", str(image))
    if notary_profile:
        run("xcrun", "notarytool", "submit", str(image), "--keychain-profile", notary_profile, "--wait")
        run("xcrun", "stapler", "staple", str(image))
    run("hdiutil", "verify", str(image))

    sums = output_dir / "SHA256SUMS.txt"
    sums.write_text(f"{sha256(image)}  {image.name}\n", encoding="ascii")
    signing = "Developer ID" if identity != "-" else "ad hoc (not notarized)"
    print(f"ToneTrace macOS disk image: PASS: {image} [{signing}]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
