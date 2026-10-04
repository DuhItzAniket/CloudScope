#!/usr/bin/env python3
"""Run clang-tidy over the C++ sources and list every finding once.

    python3 tools/dev/tidy.py --build build/linux-x64            all sources
    python3 tools/dev/tidy.py --build build/linux-x64 core/src/common/log.cpp

Needs a configured and built build folder (for compile_commands.json and generated headers) and clang-tidy 19,
as on Debian 13 (apt install clang-tidy). Exit code 1 if there is any finding: the policy is zero (NFR-MNT-04).
Checks are configured in .clang-tidy and tests/.clang-tidy.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE_DIRS = ("core", "apps", "tests")
# The file part may start with a Windows drive letter ("C:/...").
FINDING = re.compile(r"^(?P<file>(?:[A-Za-z]:)?[^\s:][^:]*):(?P<line>\d+):(?P<column>\d+): (?:error|warning): "
                     r"(?P<message>.*) \[(?P<check>[^\]]+)\]$")


def source_files(repo: Path = REPO) -> list[Path]:
    files: list[Path] = []
    for directory in SOURCE_DIRS:
        files += [path for path in (repo / directory).rglob("*.cpp") if path.is_file()]
    return sorted(files)


def parse_findings(output: str, repo: Path = REPO) -> set[tuple[str, int, int, str, str]]:
    """(file, line, column, check, message) for every diagnostic line in clang-tidy's output."""
    findings = set()
    for line in output.splitlines():
        match = FINDING.match(line.strip())
        if not match:
            continue
        path = Path(match["file"])
        try:
            name = path.resolve().relative_to(repo).as_posix()
        except ValueError:
            name = path.as_posix()
        # "check,-warnings-as-errors" is how clang-tidy marks findings promoted to errors.
        check = match["check"].split(",")[0]
        findings.add((name, int(match["line"]), int(match["column"]), check, match["message"]))
    return findings


def run_one(tool: str, build: Path, source: Path) -> tuple[Path, str, int]:
    result = subprocess.run([tool, "-p", str(build), "--quiet", str(source)], capture_output=True, text=True,
                            check=False, cwd=REPO)
    return source, result.stdout + result.stderr, result.returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, required=True, help="build folder with compile_commands.json")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("files", nargs="*", type=Path, help="sources to check (default: all)")
    args = parser.parse_args(argv)

    tool = shutil.which("clang-tidy")
    if tool is None:
        print("clang-tidy not found (Debian: apt install clang-tidy)", file=sys.stderr)
        return 2
    build = args.build.resolve()
    if not (build / "compile_commands.json").is_file():
        print(f"{build / 'compile_commands.json'} not found: configure and build first", file=sys.stderr)
        return 2
    sources = [path.resolve() for path in args.files] or source_files()

    findings: set[tuple[str, int, int, str, str]] = set()
    broken: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for source, output, code in pool.map(lambda path: run_one(tool, build, path), sources):
            found = parse_findings(output)
            findings |= found
            name = source.relative_to(REPO).as_posix()
            print(f"  {name}: {len(found)} finding(s)", flush=True)
            if code != 0 and not found:
                broken.append(f"{name}:\n{output.strip()}")   # clang-tidy itself failed, e.g. a compile error

    for name, line, column, check, message in sorted(findings):
        print(f"error: {name}:{line}:{column}: {message} [{check}]")
    for text in broken:
        print(f"error: clang-tidy could not analyse {text}")
    by_check = collections.Counter(check for _, _, _, check, _ in findings)
    for check, count in by_check.most_common():
        print(f"  {count:4d}  {check}")
    print(f"{len(sources)} files analysed: {len(findings)} finding(s)" + (f", {len(broken)} file(s) not analysed" if broken else ""))
    return 1 if findings or broken else 0


if __name__ == "__main__":
    sys.exit(main())
