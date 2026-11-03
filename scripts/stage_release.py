#!/usr/bin/env python3
"""Stage the clean ToneTrace portable release tree.

Centralizes the public-file whitelist so BUILD_WINDOWS.ps1 and BUILD_MACOS.sh
cannot accidentally ship development handoffs, audits, source, or build artifacts.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

MACOS_AU_NAME = "Tone Trace EQ.component"
MACOS_VST3_NAME = "Tone Trace EQ.vst3"
MACOS_CLAP_NAME = "Tone Trace EQ.clap"
MACOS_BUNDLES = (MACOS_VST3_NAME, MACOS_CLAP_NAME, MACOS_AU_NAME)

PUBLIC_DOC_FILES = {
    "README.md",
    "MANUAL.md",
    "CHANGELOG.md",
    "RELEASE_NOTES.md",
    "DESIGN.md",
    "GUI_DESIGN.md",
    "RELEASING.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
}

FORBIDDEN_PUBLIC_NAME_FRAGMENTS = (
    "AUDIT",
    "HANDOFF",
    "WIP",
    "GITHUB_PR",
    "INTERNAL_REPORT",
    "TMP",
)

PRIVATE_FILE_NAMES = {"agents.md", "claude.md", "gemini.md", "conversation.json", "transcript.json"}
PRIVATE_DIR_NAMES = {".codex", ".agents", ".claude", "graphify-out", ".git", "__pycache__"}


def is_private_path(path: Path) -> bool:
    return any(part.casefold() in PRIVATE_DIR_NAMES or part.casefold() in PRIVATE_FILE_NAMES
               or any(fragment in part.upper() for fragment in FORBIDDEN_PUBLIC_NAME_FRAGMENTS)
               for part in path.parts)


def die(message: str) -> "NoReturn":
    raise SystemExit(f"ERROR: {message}\nHOW TO FIX: verify the source checkpoint and release-staging arguments, then run again.")


def copy_path(src: Path, dst: Path) -> None:
    if not src.exists():
        die(f"required release input is missing: {src}")
    if src.is_dir():
        shutil.copytree(src, dst)
    else:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def validate_public_tree(root: Path, au: bool = False) -> None:
    for path in root.rglob("*"):
        if is_private_path(path):
            die(f"portable package contains private or review-only file: {path}")

    required_bundles = MACOS_BUNDLES if au else (MACOS_VST3_NAME, MACOS_CLAP_NAME)
    for bundle in required_bundles:
        bundle_path = root / bundle
        if not bundle_path.is_dir():
            die(f"plug-in bundle missing from staged tree: {bundle}")
        resources = bundle_path / "Contents" / "Resources"
        if au:
            if not (resources / "licenses").is_dir():
                die(f"licenses directory missing from {bundle}/Contents/Resources")
            if not (resources / "LICENSE").is_file() and not (resources / "LICENSE.txt").is_file():
                die(f"LICENSE missing from {bundle}/Contents/Resources")

    for doc in PUBLIC_DOC_FILES:
        if not (root / "docs" / doc).is_file():
            die(f"required public document missing: docs/{doc}")
    if not (root / "docs" / "plugins" / "README.md").is_file():
        die("plugins/README.md is missing from docs/plugins")
    if not (root / "LICENSE").is_file():
        die("root LICENSE file is missing")


def validate_output_location(source_root: Path, output: Path, allowed_output_root: Path | None = None) -> None:
    source_root = source_root.resolve()
    output = output.resolve()

    if output == source_root:
        die(f"release output must not replace the source tree: {output}")
    if output in source_root.parents:
        die(f"release output must not contain the source tree: {output}")

    if source_root not in output.parents:
        return

    if allowed_output_root is None:
        die(f"release output must not be inside the source tree without an explicit allowed output root: {output}")

    allowed_output_root = allowed_output_root.resolve()
    if allowed_output_root == source_root or source_root not in allowed_output_root.parents:
        die(f"allowed output root must itself be a dedicated subdirectory of the source tree: {allowed_output_root}")

    if output != allowed_output_root and allowed_output_root not in output.parents:
        die(f"release output is outside the explicitly allowed output root: {output}")


def stage(source_root: Path, vst3: Path, clap: Path, output: Path, allowed_output_root: Path | None = None,
          au: Path | None = None, licenses: Path | None = None) -> None:
    source_root = source_root.resolve()
    output = output.resolve()
    validate_output_location(source_root, output, allowed_output_root)

    if output.exists():
        if output.is_symlink():
            die(f"refusing to clear symlinked output directory: {output}")
        shutil.rmtree(output)
    output.mkdir(parents=True)

    copy_path(vst3, output / MACOS_VST3_NAME)
    copy_path(clap, output / MACOS_CLAP_NAME)
    if au is not None:
        copy_path(au, output / MACOS_AU_NAME)

    docs_out = output / "docs"
    docs_out.mkdir(parents=True)
    for name in sorted(PUBLIC_DOC_FILES):
        copy_path(source_root / name, docs_out / name)
    copy_path(source_root / "plugins" / "README.md", docs_out / "plugins" / "README.md")
    copy_path(source_root / "LICENSE", docs_out / "LICENSE")
    copy_path(source_root / "LICENSE", output / "LICENSE")

    if licenses and licenses.is_dir():
        copy_path(licenses, output / "licenses")
        if au is not None:
            for bundle in MACOS_BUNDLES:
                bundle_res = output / bundle / "Contents" / "Resources"
                bundle_res.mkdir(parents=True, exist_ok=True)
                copy_path(licenses, bundle_res / "licenses")
                copy_path(source_root / "LICENSE", bundle_res / "LICENSE")

    validate_public_tree(output, au=au is not None)


def main() -> int:
    ap = argparse.ArgumentParser(description="Stage and validate the clean ToneTrace portable release package.")
    ap.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1], help="ToneTrace source root.")
    ap.add_argument("--vst3", type=Path, required=True, help="Built Tone Trace EQ.vst3 bundle.")
    ap.add_argument("--clap", type=Path, required=True, help="Built Tone Trace EQ.clap binary or bundle.")
    ap.add_argument("--au", type=Path, help="Built Tone Trace EQ.component bundle (macOS).")
    ap.add_argument("--licenses", type=Path, help="Installed licenses directory.")
    ap.add_argument("--output", type=Path, required=True, help="Destination directory for staged package.")
    ap.add_argument("--allowed-output-root", type=Path, help="Allowed subtree when staging inside source.")
    args = ap.parse_args()

    stage(args.source_root, args.vst3, args.clap, args.output, args.allowed_output_root, args.au, args.licenses)
    print(f"ToneTrace release staging: PASS: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
