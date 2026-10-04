#!/usr/bin/env python3
"""Test coverage of the CloudScope sources (NFR-MNT-03), measured with gcov and reported by gcovr.

    cmake --preset linux-x64 -DCLOUDSCOPE_COVERAGE=ON
    cmake --build --preset linux-x64 --config Debug && ctest --preset linux-x64 -C Debug
    python3 tools/ci/coverage_report.py --build build/linux-x64 --output build/coverage --min-line 70

Writes <output>/index.html (line-by-line report), <output>/summary.json and <output>/summary.txt, prints the
per-file table, and exits with 1 if line coverage of the core library is below --min-line or if nothing was
measured (a broken measurement must not pass as "fine").

Counted: core/src, core/include and apps. Not counted: tests, generated files, third-party headers.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
COUNTED = ("core/src/", "core/include/", "apps/")
CORE = ("core/src/", "core/include/")


def percent(covered: int, total: int) -> float:
    return 100.0 * covered / total if total else 0.0


def summarise(summary: dict, prefixes: tuple[str, ...]) -> tuple[int, int]:
    """(covered lines, total lines) of the files whose path starts with one of the prefixes."""
    covered = total = 0
    for entry in summary.get("files", []):
        name = entry["filename"].replace("\\", "/")
        if name.startswith(prefixes):
            covered += entry["line_covered"]
            total += entry["line_total"]
    return covered, total


def table(summary: dict) -> str:
    """Per-file line and branch coverage, least covered first."""
    rows = sorted(summary.get("files", []), key=lambda entry: (entry["line_percent"], entry["filename"]))
    width = max((len(entry["filename"]) for entry in rows), default=4)
    lines = [f"{'File':<{width}}  {'Lines':>13}  {'Branches':>9}"]
    for entry in rows:
        branch = entry.get("branch_percent")  # None for a file without branches
        branch_text = f"{branch:7.1f}%" if branch is not None else f"{'-':>8}"
        lines.append(f"{entry['filename']:<{width}}  {entry['line_covered']:>4}/{entry['line_total']:<4} "
                     f"{entry['line_percent']:5.1f}%  {branch_text}")
    return "\n".join(lines)


def verdict(summary: dict, min_line: float) -> tuple[bool, str]:
    """(passed, one-line message) for the core library."""
    covered, total = summarise(summary, CORE)
    if total == 0:
        return False, "no lines of the core library were measured: the coverage build or the tests did not run"
    core = percent(covered, total)
    all_covered, all_total = summarise(summary, COUNTED)
    message = (f"core library {core:.1f} % of lines ({covered}/{total}); with applications "
               f"{percent(all_covered, all_total):.1f} % ({all_covered}/{all_total}); required {min_line:.0f} %")
    return core >= min_line, message


def run_gcovr(build: Path, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    command = [
        "gcovr", "--root", str(REPO), "--object-directory", str(build),
        "--exclude", r".*/generated/.*", "--exclude-unreachable-branches", "--exclude-throw-branches",
        "--json-summary", str(output / "summary.json"), "--json-summary-pretty",
        "--html-details", str(output / "index.html"),
        "--txt", str(output / "summary.txt"),
    ]
    for prefix in COUNTED:
        command += ["--filter", prefix]
    subprocess.run(command, check=True, cwd=REPO)
    return json.loads((output / "summary.json").read_text(encoding="utf-8"))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, required=True, help="build folder of a CLOUDSCOPE_COVERAGE=ON build")
    parser.add_argument("--output", type=Path, required=True, help="folder for the report")
    parser.add_argument("--min-line", type=float, default=70.0, help="required line coverage of the core library")
    args = parser.parse_args(argv)

    summary = run_gcovr(args.build.resolve(), args.output.resolve())
    print(table(summary))
    passed, message = verdict(summary, args.min_line)
    print(("Coverage: " if passed else "Coverage too low: ") + message)
    if os.environ.get("GITHUB_ACTIONS") == "true":
        level = "notice" if passed else "error"
        print(f"::{level} title=Coverage::{message}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
