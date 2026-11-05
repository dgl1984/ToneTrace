#!/usr/bin/env python3
"""Restore ToneTrace's pinned JUCE and CLAP build dependencies on macOS or Linux.

Downloads exact revisions, checks each archive against a pinned SHA-256, and
stops rather than overwrite local dependency files that differ from the verified archive.
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]

# JUCE 8.0.12 source archive for tag commit 29396c22c93392d6738e021b83196283d6e4d850.
JUCE_PINS = (
    {"name": "JUCE", "url": "https://codeload.github.com/juce-framework/JUCE/zip/refs/tags/8.0.12",
     "archive": "JUCE-8.0.12-source.zip", "prefix": "JUCE-8.0.12/",
     "sha256": "91ceb4b54f9cc763884f6285fcfee4372fbe72d33061a339255cee2957e34ad8", "target": "JUCE"},
)

# Pinned CLAP revisions for commit 55525c9858d4b25687be7759a5e0f70eccef218e.
CLAP_REVISIONS = (
    ("clap-juce-extensions", "55525c9858d4b25687be7759a5e0f70eccef218e",
     "a6ff539a7faebcfcda4131d8982a388dd3382350d082511dfd9578ce7358f9ea", "clap-juce-extensions"),
    ("clap", "29ffcc273be7c7c651f6c9953b99e69700e2387a",
     "98f18381998028d751669c218265dbba4a17e44bb112e63a8ff02d07aee62d76", "clap-juce-extensions/clap-libs/clap"),
    ("clap-helpers", "a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc",
     "0369451cdb2f82eff5aef1db5abff6bff300c553da0dd267433e386bf97fd674", "clap-juce-extensions/clap-libs/clap-helpers"),
)
CLAP_PINS = tuple(
    {"name": name, "url": f"https://codeload.github.com/free-audio/{name}/zip/{commit}",
     "archive": f"{name}-{commit}.zip", "prefix": f"{name}-{commit}/", "sha256": digest, "target": target}
    for name, commit, digest, target in CLAP_REVISIONS
)


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"ERROR: {message}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch_archive(pin: dict, archives: Path) -> Path:
    archive = archives / pin["archive"]
    if not archive.is_file():
        # Check local cache candidate from sibling repository if present
        local_candidate = Path.home() / "src/spacetrace/deps/archives" / pin["archive"]
        if local_candidate.is_file() and sha256_file(local_candidate) == pin["sha256"]:
            print(f"Using local cached archive from {local_candidate}")
            shutil.copy2(local_candidate, archive)
            return archive

        partial = archive.with_name(archive.name + ".partial")
        print(f"Downloading {pin['url']}")
        with urllib.request.urlopen(pin["url"]) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        if sha256_file(partial) != pin["sha256"]:
            partial.unlink()
            fail(f"SHA-256 mismatch for {pin['name']}; existing dependencies were preserved.")
        partial.replace(archive)
    if sha256_file(archive) != pin["sha256"]:
        fail(f"Cached archive SHA-256 mismatch: {archive}")
    return archive


def extract_verified(pin: dict, archive: Path, deps: Path) -> None:
    target_root = (deps / pin["target"]).resolve()
    with zipfile.ZipFile(archive) as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            if not info.filename.startswith(pin["prefix"]):
                fail(f"Unexpected archive root in {archive.name}: {info.filename}")
            relative = PurePosixPath(info.filename[len(pin["prefix"]):])
            target = (target_root / relative).resolve()
            if target_root not in target.parents:
                fail(f"Unsafe archive path in {archive.name}: {info.filename}")
            data = zf.read(info)
            if target.exists():
                if sha256_file(target) != hashlib.sha256(data).hexdigest():
                    fail(f"Local dependency differs from the pinned archive: {target}. Local content was preserved.")
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    print(f"Verified {pin['name']} at {target_root}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Fetch and verify ToneTrace's pinned build dependencies.")
    ap.add_argument("--deps", type=Path, default=ROOT / "deps", help="Dependency directory (default: deps/).")
    ap.add_argument("--only", choices=("juce", "clap"), help="Restore only one dependency group.")
    args = ap.parse_args()

    deps = args.deps.resolve()
    archives = deps / "archives"
    archives.mkdir(parents=True, exist_ok=True)

    groups = {"juce": JUCE_PINS, "clap": CLAP_PINS}
    for group, pins in groups.items():
        if args.only and args.only != group:
            continue
        for pin in pins:
            extract_verified(pin, fetch_archive(pin, archives), deps)

    if args.only != "juce":
        record = deps / "clap-juce-extensions" / "TONETRACE_PINNED_REVISIONS.txt"
        record.write_text("".join(f"{name} {commit}\n" for name, commit, _, _ in CLAP_REVISIONS), encoding="ascii")
    return 0


if __name__ == "__main__":
    sys.exit(main())
