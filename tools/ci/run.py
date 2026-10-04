#!/usr/bin/env python3
"""Run one CI step and make its failure readable without signing in to GitHub.

    python3 tools/ci/run.py "Build (Debug)" cmake --build --preset linux-x64 --config Debug

The command's output is passed through unchanged. If the command fails, the lines that look like errors and
the end of the output are repeated as GitHub "error" annotations. Annotations are shown on the commit page
and can be read through the public API (see tools/ci/status.py); full job logs need a signed-in user.

Outside GitHub Actions the script only runs the command and returns its exit code.
"""

from __future__ import annotations

import collections
import os
import re
import subprocess
import sys

TAIL_LINES = 400            # how much of the end of the output is kept
MAX_ANNOTATIONS = 8         # GitHub shows at most 10 error annotations per step
MAX_ANNOTATION_CHARS = 3500
ERROR_LINE = re.compile(r"(\berror\b|\bFAILED\b|\bfatal\b|Error:|\*\*\*Failed|undefined reference|CMake Error)", re.I)
MAX_ERROR_LINES = 40
# CTest lines that only mention a test whose name contains a word such as "error".
NOT_AN_ERROR = re.compile(r"^\s*(Start\s+\d+:|\d+/\d+ Test\s+#\d+: .*\bPassed\b)")


def escape_data(text: str) -> str:
    """Escape a message for a GitHub workflow command."""
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def escape_property(text: str) -> str:
    """Escape a property value (such as title=) for a GitHub workflow command."""
    return escape_data(text).replace(":", "%3A").replace(",", "%2C")


def is_error_line(line: str) -> bool:
    return bool(ERROR_LINE.search(line)) and not NOT_AN_ERROR.match(line)


def chunk_lines(lines: list[str], max_chars: int) -> list[str]:
    """Join lines into blocks of at most max_chars characters; overlong lines are cut."""
    chunks: list[str] = []
    current: list[str] = []
    size = 0
    for line in lines:
        line = line[:max_chars]
        if current and size + len(line) + 1 > max_chars:
            chunks.append("\n".join(current))
            current, size = [], 0
        current.append(line)
        size += len(line) + 1
    if current:
        chunks.append("\n".join(current))
    return chunks


def failure_annotations(name: str, exit_code: int, error_lines: list[str], tail: list[str]) -> list[str]:
    """Build the workflow commands that report a failed step."""
    commands: list[str] = []
    title = escape_property(f"{name} failed (exit code {exit_code})")
    if error_lines:
        body = "\n".join(error_lines)[:MAX_ANNOTATION_CHARS]
        commands.append(f"::error title={escape_property(name + ': error lines')}::{escape_data(body)}")
    chunks = chunk_lines(tail, MAX_ANNOTATION_CHARS)
    budget = MAX_ANNOTATIONS - len(commands)
    chunks = chunks[-budget:]                      # the end of the log matters most
    for index, chunk in enumerate(chunks, start=1):
        part = f" [log end, part {index}/{len(chunks)}]" if len(chunks) > 1 else " [log end]"
        commands.append(f"::error title={title}{escape_property(part)}::{escape_data(chunk)}")
    return commands


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    name, command = argv[1], argv[2:]
    in_actions = os.environ.get("GITHUB_ACTIONS") == "true"

    if in_actions:
        print(f"::group::{name}", flush=True)
    tail: collections.deque[str] = collections.deque(maxlen=TAIL_LINES)
    error_lines: list[str] = []
    try:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
    except OSError as error:
        print(f"Cannot start {command[0]!r}: {error}", file=sys.stderr)
        if in_actions:
            print("::endgroup::")
            print(f"::error title={escape_property(name)}::{escape_data(f'Cannot start {command[0]!r}: {error}')}")
        return 127
    assert process.stdout is not None
    for line in process.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        line = line.rstrip("\r\n")
        tail.append(line)
        if len(error_lines) < MAX_ERROR_LINES and is_error_line(line):
            error_lines.append(line)
    exit_code = process.wait()
    if in_actions:
        print("::endgroup::", flush=True)
        if exit_code != 0:
            for command_text in failure_annotations(name, exit_code, error_lines, list(tail)):
                print(command_text, flush=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
