#!/usr/bin/env python3
"""
bump_version.py — Bump Urho3D version in canonical references.

Usage:
    bump_version.py <new-version>            # bump to <new-version>
    bump_version.py --release <new-version>  # allow even patch (release)
    bump_version.py --dry-run <new-version>  # show changes without writing
    bump_version.py --current                # print current version and exit

Convention:
    Odd patch number  = development / beta build (default for dev bumps)
    Even patch number = release tag (must be confirmed with --release)

The script edits canonical Urho3D version references in:
    - CLAUDE.md
    - README.md

Each substitution is a literal string with surrounding-context anchors, so
third-party version strings (SDL 2.0.10+, "Update SDL to 2.0.1", Assimp/zlib
changelogs, etc.) are not touched. The filesystem directory name and any
IDE/build paths are NOT auto-renamed — that's a manual step.
"""
import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CLAUDE_MD = REPO_ROOT / "CLAUDE.md"
README_MD = REPO_ROOT / "README.md"

# Pattern that detects the canonical "current version" line in CLAUDE.md.
CURRENT_VERSION_RE = re.compile(r'\*\*Current Version\*\*:\s*(\d+\.\d+\.\d+)')

# Substitution table. Each row is (file, before_template, after_template).
# Templates use {old} and {new} which get filled with the current/target
# versions. Substitutions are LITERAL string replacements (not regex), so
# the surrounding context must uniquely anchor the version string and avoid
# false-positives on third-party version mentions.
SUB_TEMPLATES = [
    # CLAUDE.md
    (CLAUDE_MD, "**Current Version**: {old}",  "**Current Version**: {new}"),
    (CLAUDE_MD, "Urho3D {old} now includes",   "Urho3D {new} now includes"),

    # README.md
    (README_MD, "# Urho3D {old}",              "# Urho3D {new}"),
    (README_MD, "What's New in {old}",         "What's New in {new}"),
    (README_MD, "(v{old})",                    "(v{new})"),
    (README_MD, "The {old} change history",    "The {new} change history"),
]


def detect_current_version():
    """Read the canonical 'Current Version' line from CLAUDE.md."""
    if not CLAUDE_MD.exists():
        return None
    text = CLAUDE_MD.read_text()
    m = CURRENT_VERSION_RE.search(text)
    return m.group(1) if m else None


def parse_version(s):
    """Validate and split a MAJOR.MINOR.PATCH string."""
    m = re.match(r'^(\d+)\.(\d+)\.(\d+)$', s)
    if not m:
        sys.exit(f"ERROR: version must be MAJOR.MINOR.PATCH (got: {s})")
    return tuple(int(x) for x in m.groups())


def main():
    parser = argparse.ArgumentParser(
        description="Bump Urho3D version in canonical references.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Convention: odd patch = dev/beta, even patch = release.\n"
            "Default refuses even-patch bumps; pass --release to confirm.\n"
        ),
    )
    parser.add_argument("new_version", nargs='?',
                        help="New version (MAJOR.MINOR.PATCH)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show changes without writing files")
    parser.add_argument("--release", action="store_true",
                        help="Allow even-patch bumps (release version)")
    parser.add_argument("--current", action="store_true",
                        help="Print current version and exit")
    args = parser.parse_args()

    # Mode 1: report current version and exit
    if args.current:
        v = detect_current_version()
        print(v if v else "unknown")
        return 0

    if not args.new_version:
        parser.print_help(sys.stderr)
        return 1

    # Validate target version format
    major, minor, patch = parse_version(args.new_version)
    if patch % 2 == 0 and not args.release:
        sys.exit(
            f"ERROR: patch number {patch} is even — that's a release tag.\n"
            f"       Use an odd patch for dev bumps, or pass --release to\n"
            f"       confirm an even-patch (release) bump."
        )

    # Detect current
    current = detect_current_version()
    if not current:
        sys.exit(f"ERROR: could not detect current version from {CLAUDE_MD}")

    if current == args.new_version:
        print(f"Version is already {args.new_version} — nothing to do.")
        return 0

    print(f"Current version: {current}")
    print(f"New version:     {args.new_version}")
    if args.dry_run:
        print("(dry run — no files will be modified)")
    print()

    changes = 0
    skipped = 0
    for file_path, before_tmpl, after_tmpl in SUB_TEMPLATES:
        if not file_path.exists():
            print(f"WARN: file not found: {file_path}", file=sys.stderr)
            continue

        before = before_tmpl.format(old=current, new=args.new_version)
        after = after_tmpl.format(old=current, new=args.new_version)
        text = file_path.read_text()

        if before not in text:
            print(f"  skip: {file_path.name}: '{before}' not present (already bumped?)")
            skipped += 1
            continue

        new_text = text.replace(before, after)
        if args.dry_run:
            print(f" would: {file_path.name}: '{before}'  ->  '{after}'")
        else:
            file_path.write_text(new_text)
            print(f"  bump: {file_path.name}: '{before}'  ->  '{after}'")
        changes += 1

    print()
    if args.dry_run:
        print(f"Dry run complete. {changes} change(s) would be applied, "
              f"{skipped} skipped.")
        print("Re-run without --dry-run to write the changes.")
    else:
        print(f"Bumped {changes} reference(s) from {current} to "
              f"{args.new_version}. {skipped} skipped.")
        print()
        print("Manual follow-ups not handled by this script:")
        print(f"  - Directory name (currently 'urho3d-{current}') — rename and "
              f"update build paths if you want it to match.")
        print(f"  - Git tag — create with `git tag -a v{args.new_version} -m \"...\"`"
              f" if you want CMake's URHO3D_VERSION to pick it up.")
        print(f"  - IDE / project file references to the old path.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
