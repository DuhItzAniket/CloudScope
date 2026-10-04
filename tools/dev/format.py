#!/usr/bin/env python3
"""Format the C++ sources with clang-format, or check that they are formatted.

    python tools/dev/format.py            rewrite files in place
    python tools/dev/format.py --check    change nothing; exit 1 and list the files that need formatting (CI)

Formats every .cpp/.hpp under core/, apps/ and tests/ according to .clang-format. Uses `clang-format` from
PATH, or the one that ships with Visual Studio 2022. CI uses clang-format 19; another major version may
format a few constructs differently, so the script warns when it finds one.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE_DIRS = ("core", "apps", "tests")
EXTENSIONS = (".cpp", ".hpp", ".h")
EXPECTED_MAJOR = 19


def find_clang_format() -> str | None:
    found = shutil.which("clang-format")
    if found:
        return found
    for edition in ("BuildTools", "Community", "Professional", "Enterprise"):
        for base in (r"C:\Program Files (x86)", r"C:\Program Files"):
            candidate = Path(base) / "Microsoft Visual Studio" / "2022" / edition / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-format.exe"
            if candidate.is_file():
                return str(candidate)
    return None


def source_files(repo: Path = REPO) -> list[Path]:
    files: list[Path] = []
    for directory in SOURCE_DIRS:
        files += [path for path in (repo / directory).rglob("*") if path.suffix in EXTENSIONS and path.is_file()]
    return sorted(files)


def major_version(version_output: str) -> int | None:
    match = re.search(r"version (\d+)\.", version_output)
    return int(match.group(1)) if match else None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="do not modify files; fail if any needs formatting")
    args = parser.parse_args(argv)

    tool = find_clang_format()
    if tool is None:
        print("clang-format not found. Install it (Debian: apt install clang-format) or add it to PATH.", file=sys.stderr)
        return 2
    version = subprocess.run([tool, "--version"], capture_output=True, text=True, check=True).stdout.strip()
    if major_version(version) != EXPECTED_MAJOR:
        print(f"warning: {version}; CI uses clang-format {EXPECTED_MAJOR}, results may differ", file=sys.stderr)

    files = source_files()
    if not files:
        print("no source files found", file=sys.stderr)
        return 2
    command = [tool, "--style=file"] + (["--dry-run", "--Werror"] if args.check else ["-i"])
    result = subprocess.run(command + [str(path) for path in files], capture_output=True, text=True, check=False, cwd=REPO)
    if args.check:
        unformatted = sorted(set(re.findall(r"^(.+?):\d+:\d+: error: code should be clang-formatted", result.stderr, flags=re.M)))
        for name in unformatted:
            print(f"error: needs formatting: {Path(name).resolve().relative_to(REPO).as_posix()}")
        if result.returncode != 0 and not unformatted:
            print(result.stderr, file=sys.stderr)
        print(f"{len(files)} files checked with {version}: " +
              ("all formatted" if result.returncode == 0 else f"{len(unformatted)} need formatting (run tools/dev/format.py)"))
        return 0 if result.returncode == 0 else 1
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return 1
    print(f"{len(files)} files formatted with {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
