#!/usr/bin/env python3
"""Show the CI result of a commit, with failure details, without signing in to GitHub.

    python tools/ci/status.py                 result for HEAD
    python tools/ci/status.py --wait          poll until every job has finished
    python tools/ci/status.py <revision>      any git revision that has been pushed

Exit code: 0 all jobs passed, 1 a job failed, 2 jobs still running (or --wait timed out), 3 no CI jobs found.

Uses the public GitHub API (60 requests per hour without a token; unchanged answers do not count).
Set GITHUB_TOKEN to raise the limit. Failure details come from the annotations written by tools/ci/run.py.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

API = "https://api.github.com"
PASSING = {"success", "skipped", "neutral"}


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout.strip()


def repository_slug(remote_url: str) -> str:
    """'owner/name' from an https or ssh GitHub remote URL."""
    match = re.search(r"github\.com[:/]+([^/]+)/([^/]+?)(?:\.git)?/?$", remote_url)
    if not match:
        raise ValueError(f"not a GitHub remote: {remote_url}")
    return f"{match.group(1)}/{match.group(2)}"


class Api:
    """Minimal GitHub API client with conditional requests (ETag), so polling is free while nothing changes."""

    def __init__(self) -> None:
        self._cache: dict[str, tuple[str, object]] = {}
        self._token = os.environ.get("GITHUB_TOKEN", "")

    def get(self, path: str) -> object:
        request = urllib.request.Request(API + path)
        request.add_header("Accept", "application/vnd.github+json")
        request.add_header("User-Agent", "cloudscope-ci-status")
        if self._token:
            request.add_header("Authorization", f"Bearer {self._token}")
        if path in self._cache:
            request.add_header("If-None-Match", self._cache[path][0])
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                data = json.load(response)
                etag = response.headers.get("ETag")
                if etag:
                    self._cache[path] = (etag, data)
                return data
        except urllib.error.HTTPError as error:
            if error.code == 304:
                return self._cache[path][1]
            if error.code in (403, 429) and error.headers.get("X-RateLimit-Remaining") == "0":
                reset = int(error.headers.get("X-RateLimit-Reset", "0"))
                minutes = max(0, reset - int(time.time())) // 60 + 1
                raise SystemExit(f"GitHub API rate limit reached; try again in about {minutes} min "
                                 "or set GITHUB_TOKEN.") from error
            raise


def summarise(runs: list[dict]) -> tuple[int, int, int]:
    """(passed, failed, unfinished) counts."""
    passed = sum(1 for run in runs if run["status"] == "completed" and run["conclusion"] in PASSING)
    unfinished = sum(1 for run in runs if run["status"] != "completed")
    return passed, len(runs) - passed - unfinished, unfinished


def exit_code_for(runs: list[dict]) -> int:
    if not runs:
        return 3
    _, failed, unfinished = summarise(runs)
    if failed:
        return 1
    return 2 if unfinished else 0


def print_report(api: Api, slug: str, sha: str, runs: list[dict]) -> None:
    print(f"CI for {slug} @ {sha[:10]}")
    for run in sorted(runs, key=lambda item: item["name"]):
        state = run["conclusion"] if run["status"] == "completed" else run["status"]
        print(f"  {state:<12} {run['name']}")
    for run in sorted(runs, key=lambda item: item["name"]):
        if run["status"] != "completed" or run["conclusion"] in PASSING:
            continue
        print(f"\n--- {run['name']}: {run['conclusion']} ({run['html_url']})")
        annotations = api.get(f"/repos/{slug}/check-runs/{run['id']}/annotations?per_page=50")
        if not annotations:
            print("  (no annotations; open the link above for the log)")
        for annotation in annotations:
            title = annotation.get("title") or annotation.get("annotation_level", "")
            print(f"[{title}]")
            print(annotation.get("message", "").rstrip())
            print()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("revision", nargs="?", default="HEAD")
    parser.add_argument("--wait", action="store_true", help="poll until every job has finished")
    parser.add_argument("--interval", type=int, default=45, help="seconds between polls (default 45)")
    parser.add_argument("--timeout", type=int, default=75, help="minutes to wait at most (default 75)")
    args = parser.parse_args()

    slug = repository_slug(git("remote", "get-url", "origin"))
    sha = git("rev-parse", args.revision)
    api = Api()
    path = f"/repos/{slug}/commits/{sha}/check-runs?per_page=100"
    deadline = time.monotonic() + args.timeout * 60
    start = time.monotonic()

    while True:
        runs = api.get(path)["check_runs"]
        code = exit_code_for(runs)
        # Jobs take a moment to appear after a push: keep waiting for the first three minutes.
        appearing = code == 3 and time.monotonic() - start < 180
        if not args.wait or (code in (0, 1)) or (code == 3 and not appearing) or time.monotonic() > deadline:
            break
        passed, failed, unfinished = summarise(runs)
        print(f"  waiting: {passed} passed, {failed} failed, {unfinished} running "
              f"({int(time.monotonic() - start) // 60} min)", flush=True)
        time.sleep(args.interval)

    if not runs:
        print(f"No CI jobs found for {sha[:10]} (not pushed, or the workflow has not started).")
        return 3
    print_report(api, slug, sha, runs)
    return code


if __name__ == "__main__":
    sys.exit(main())
