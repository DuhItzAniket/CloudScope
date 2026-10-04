#!/usr/bin/env python3
"""Licence gate for third-party libraries (ADR-010).

Compares what a build actually links and ships with the reviewed table packaging/licences/third_party.toml
and applies the licence policy in that table. Writes a Markdown report; exits with 1 if anything is wrong.

    python tools/ci/licence_check.py windows --vcpkg-installed build/windows-msvc/vcpkg_installed \
        --bin build/windows-msvc/bin/Release --report licences-windows.md
    python3 tools/ci/licence_check.py linux --bin build/linux-x64/bin/Release --report licences-linux.md

Windows: every installed vcpkg port and every DLL next to the executables must belong to a listed component.
Linux:   every shared library the executables need directly (DT_NEEDED) must belong to a listed component.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import shutil
import subprocess
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

DEFAULT_TABLE = Path(__file__).resolve().parents[2] / "packaging" / "licences" / "third_party.toml"
LINKING_KINDS = ("dynamic", "static", "header-only", "system", "build-only")


# ----------------------------------------------------------------------------------------------- table


@dataclass(frozen=True)
class Component:
    name: str
    licence: str
    linking: str
    homepage: str
    used_for: str
    vcpkg_ports: tuple[str, ...] = ()
    windows_dlls: tuple[str, ...] = ()
    linux_libraries: tuple[str, ...] = ()


@dataclass(frozen=True)
class Policy:
    allowed: frozenset[str]
    allowed_dynamic: frozenset[str]
    allowed_system: frozenset[str]

    def allowed_for(self, linking: str) -> frozenset[str]:
        if linking == "dynamic":
            return self.allowed | self.allowed_dynamic
        if linking == "system":
            return self.allowed | self.allowed_system
        return self.allowed


def load_table(path: Path) -> tuple[Policy, list[Component]]:
    """Read and validate the component table. Raises ValueError with a readable message."""
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    policy_data = data.get("policy", {})
    policy = Policy(
        allowed=frozenset(policy_data.get("allowed", [])),
        allowed_dynamic=frozenset(policy_data.get("allowed_dynamic", [])),
        allowed_system=frozenset(policy_data.get("allowed_system", [])),
    )
    if not policy.allowed:
        raise ValueError("[policy] allowed is empty")
    components: list[Component] = []
    seen: set[str] = set()
    for entry in data.get("component", []):
        unknown = set(entry) - set(Component.__dataclass_fields__)
        if unknown:
            raise ValueError(f"component {entry.get('name', '?')!r}: unknown fields {sorted(unknown)}")
        missing = {"name", "licence", "linking", "homepage", "used_for"} - set(entry)
        if missing:
            raise ValueError(f"component {entry.get('name', '?')!r}: missing fields {sorted(missing)}")
        if entry["linking"] not in LINKING_KINDS:
            raise ValueError(f"component {entry['name']!r}: linking must be one of {LINKING_KINDS}")
        if entry["name"] in seen:
            raise ValueError(f"component {entry['name']!r} is listed twice")
        seen.add(entry["name"])
        components.append(Component(
            name=entry["name"], licence=entry["licence"], linking=entry["linking"], homepage=entry["homepage"],
            used_for=entry["used_for"], vcpkg_ports=tuple(entry.get("vcpkg_ports", ())),
            windows_dlls=tuple(entry.get("windows_dlls", ())),
            linux_libraries=tuple(entry.get("linux_libraries", ())),
        ))
    if not components:
        raise ValueError("no [[component]] entries")
    return policy, components


# ------------------------------------------------------------------------------------ SPDX expressions

_TOKEN = re.compile(r"\s*(\(|\)|[A-Za-z0-9.+-]+)")


def _tokenize(expression: str) -> list[str]:
    tokens: list[str] = []
    position = 0
    while position < len(expression):
        match = _TOKEN.match(expression, position)
        if not match:
            if expression[position:].strip() == "":
                break
            raise ValueError(f"cannot read licence expression {expression!r}")
        tokens.append(match.group(1))
        position = match.end()
    if not tokens:
        raise ValueError("empty licence expression")
    return tokens


def parse_expression(expression: str) -> tuple:
    """Parse an SPDX expression into ('ID', name) / ('AND', [..]) / ('OR', [..]). 'X WITH Y' is one ID."""
    tokens = _tokenize(expression)
    position = 0

    def peek() -> str | None:
        return tokens[position] if position < len(tokens) else None

    def take() -> str:
        nonlocal position
        position += 1
        return tokens[position - 1]

    def atom() -> tuple:
        token = take() if peek() is not None else None
        if token is None or token in (")", "AND", "OR", "WITH"):
            raise ValueError(f"cannot read licence expression {expression!r}")
        if token == "(":
            inner = either()
            if peek() != ")":
                raise ValueError(f"missing ')' in licence expression {expression!r}")
            take()
            return inner
        if peek() == "WITH":
            take()
            exception = take() if peek() not in (None, "(", ")", "AND", "OR", "WITH") else None
            if exception is None:
                raise ValueError(f"missing exception after WITH in {expression!r}")
            return ("ID", f"{token} WITH {exception}")
        return ("ID", token)

    def both() -> tuple:
        parts = [atom()]
        while peek() == "AND":
            take()
            parts.append(atom())
        return parts[0] if len(parts) == 1 else ("AND", parts)

    def either() -> tuple:
        parts = [both()]
        while peek() == "OR":
            take()
            parts.append(both())
        return parts[0] if len(parts) == 1 else ("OR", parts)

    tree = either()
    if position != len(tokens):
        raise ValueError(f"cannot read licence expression {expression!r}")
    return tree


def licence_ids(expression: str) -> set[str]:
    """All licence identifiers that occur in an SPDX expression."""
    def walk(node: tuple) -> set[str]:
        if node[0] == "ID":
            return {node[1]}
        return set().union(*(walk(child) for child in node[1]))
    return walk(parse_expression(expression))


def is_allowed(expression: str, allowed: frozenset[str] | set[str]) -> bool:
    """AND needs every part allowed; OR needs one allowed part (we choose that licence)."""
    def walk(node: tuple) -> bool:
        if node[0] == "ID":
            return node[1] in allowed
        results = [walk(child) for child in node[1]]
        return all(results) if node[0] == "AND" else any(results)
    return walk(parse_expression(expression))


def check_policy(policy: Policy, components: list[Component]) -> list[str]:
    problems = []
    for component in components:
        try:
            ok = is_allowed(component.licence, policy.allowed_for(component.linking))
        except ValueError as error:
            problems.append(f"{component.name}: {error}")
            continue
        if not ok:
            problems.append(f"{component.name}: licence '{component.licence}' is not allowed for "
                            f"{component.linking} linking (ADR-010)")
    return problems


def reported_versions(bin_dir: Path) -> dict[str, str]:
    """Library versions as reported by the built cloudscope-info; empty if it is missing or does not run."""
    for name in ("cloudscope-info.exe", "cloudscope-info"):
        executable = bin_dir / name
        if not executable.is_file():
            continue
        try:
            result = subprocess.run([str(executable), "--json"], capture_output=True, text=True, timeout=60,
                                    check=True)
            dependencies = json.loads(result.stdout)["dependencies"]
        except (OSError, subprocess.SubprocessError, ValueError, KeyError):
            return {}
        return {item["name"]: item.get("runtime_version") or item.get("compiled_version", "")
                for item in dependencies}
    return {}


# --------------------------------------------------------------------------------------------- Windows


def parse_vcpkg_status(text: str, triplet: str) -> dict[str, str]:
    """Installed ports and their versions from vcpkg's status file (feature paragraphs are skipped)."""
    ports: dict[str, str] = {}
    for paragraph in re.split(r"\r?\n\s*\r?\n", text):
        fields = dict(re.findall(r"^([A-Za-z-]+): (.*)$", paragraph, flags=re.M))
        fields = {key: value.strip() for key, value in fields.items()}
        if not fields or "Feature" in fields:
            continue
        if fields.get("Architecture") != triplet or fields.get("Status") != "install ok installed":
            continue
        version = fields.get("Version", "")
        if fields.get("Port-Version", "0") != "0":
            version += "#" + fields["Port-Version"]
        ports[fields["Package"]] = version
    return ports


def vcpkg_recorded_licence(installed: Path, triplet: str, port: str) -> str | None:
    """The licence vcpkg recorded for a port, or None when vcpkg has no information."""
    spdx = installed / triplet / "share" / port / "vcpkg.spdx.json"
    if not spdx.is_file():
        return None
    packages = json.loads(spdx.read_text(encoding="utf-8")).get("packages", [])
    concluded = packages[0].get("licenseConcluded") if packages else None
    if not concluded or concluded == "NOASSERTION" or concluded.startswith("LicenseRef-"):
        return None
    return concluded


def vcpkg_dll_owners(installed: Path, triplet: str) -> dict[str, str]:
    """Lower-case DLL file name -> port that installed it."""
    owners: dict[str, str] = {}
    for listing in (installed / "vcpkg" / "info").glob(f"*_{triplet}.list"):
        port = listing.name.split("_")[0]
        for line in listing.read_text(encoding="utf-8").splitlines():
            if line.lower().endswith(".dll"):
                owners[line.rsplit("/", 1)[-1].lower()] = port
    return owners


def check_windows(policy: Policy, components: list[Component], installed: Path, triplet: str,
                  bin_dir: Path) -> tuple[list[dict], list[str]]:
    problems: list[str] = []
    by_port = {port: component for component in components for port in component.vcpkg_ports}
    ports = parse_vcpkg_status((installed / "vcpkg" / "status").read_text(encoding="utf-8"), triplet)
    if not ports:
        problems.append(f"no installed vcpkg ports found for {triplet} in {installed}")
    dll_owner = vcpkg_dll_owners(installed, triplet)
    shipped = sorted(path.name for path in bin_dir.glob("*.dll"))
    if not shipped:
        problems.append(f"no DLLs found in {bin_dir}")

    files_by_component: dict[str, list[str]] = {}
    for dll in shipped:
        component = by_port.get(dll_owner.get(dll.lower(), ""))
        if component is None:
            component = next((c for c in components
                              if any(fnmatch.fnmatchcase(dll.lower(), p.lower()) for p in c.windows_dlls)), None)
        if component is None:
            problems.append(f"{dll} is next to the executables but belongs to no listed component")
        else:
            files_by_component.setdefault(component.name, []).append(dll)

    versions: dict[str, list[str]] = {}
    for port, version in sorted(ports.items()):
        component = by_port.get(port)
        if component is None:
            problems.append(f"vcpkg port '{port}' is installed but not listed in the component table")
            continue
        versions.setdefault(component.name, []).append(version if len(component.vcpkg_ports) == 1 else f"{port} {version}")
        recorded = vcpkg_recorded_licence(installed, triplet, port)
        if recorded is not None:
            try:
                extra = licence_ids(recorded) - licence_ids(component.licence)
            except ValueError as error:
                problems.append(f"{component.name}: {error}")
                continue
            if extra:
                problems.append(f"{component.name}: vcpkg records licence '{recorded}' for port '{port}', "
                                f"the table says '{component.licence}'")

    reported = reported_versions(bin_dir)
    rows = []
    for component in components:
        present = component.name in versions or component.name in files_by_component
        if not present:
            continue
        needs_dynamic = not is_allowed(component.licence, policy.allowed)
        if needs_dynamic and component.linking == "dynamic" and component.name not in files_by_component:
            problems.append(f"{component.name}: licence '{component.licence}' requires dynamic linking, "
                            f"but none of its DLLs is next to the executables")
        version = ", ".join(versions.get(component.name, [])) or reported.get(component.name, "")
        rows.append({"name": component.name, "version": version,
                     "licence": component.licence, "linking": component.linking,
                     "files": ", ".join(files_by_component.get(component.name, [])),
                     "homepage": component.homepage})
    return rows, problems


# ----------------------------------------------------------------------------------------------- Linux


def is_elf(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            return stream.read(4) == b"\x7fELF"
    except OSError:
        return False


def parse_needed(readelf_output: str) -> list[str]:
    """Shared-library names from the dynamic section printed by `readelf -d`."""
    return re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", readelf_output)


def debian_package_version(library: str) -> str:
    """'package version' of the Debian package that provides a shared library; '' if it cannot be found."""
    if not shutil.which("dpkg-query"):
        return ""
    found = subprocess.run(["dpkg-query", "-S", f"*/{library}"], capture_output=True, text=True, check=False)
    if found.returncode != 0 or not found.stdout.strip():
        return ""
    package = found.stdout.splitlines()[0].split(": ")[0].split(",")[0].strip()
    version = subprocess.run(["dpkg-query", "-W", "-f=${Version}", package], capture_output=True, text=True,
                             check=False).stdout.strip()
    return f"{package.split(':')[0]} {version}".strip()


def check_linux(policy: Policy, components: list[Component], bin_dir: Path) -> tuple[list[dict], list[str]]:
    del policy  # every DT_NEEDED entry is dynamic by definition; check_policy covers the rest
    problems: list[str] = []
    binaries = sorted(path for path in bin_dir.iterdir() if path.is_file() and is_elf(path))
    if not binaries:
        problems.append(f"no executables found in {bin_dir}")
    needed: dict[str, set[str]] = {}
    for binary in binaries:
        output = subprocess.run(["readelf", "-d", str(binary)], capture_output=True, text=True, check=True).stdout
        for library in parse_needed(output):
            needed.setdefault(library, set()).add(binary.name)

    files_by_component: dict[str, list[str]] = {}
    for library in sorted(needed):
        component = next((c for c in components
                          if any(fnmatch.fnmatchcase(library, pattern) for pattern in c.linux_libraries)), None)
        if component is None:
            problems.append(f"{library} (needed by {', '.join(sorted(needed[library]))}) belongs to no listed component")
        else:
            files_by_component.setdefault(component.name, []).append(library)

    reported = reported_versions(bin_dir)
    rows = []
    for component in components:
        libraries = files_by_component.get(component.name)
        if not libraries:
            continue
        versions = sorted({debian_package_version(library) for library in libraries} - {""})
        version = ", ".join(versions) or reported.get(component.name, "")
        rows.append({"name": component.name, "version": version, "licence": component.licence,
                     "linking": component.linking, "files": ", ".join(libraries), "homepage": component.homepage})
    return rows, problems


# ---------------------------------------------------------------------------------------------- report


def render_report(platform: str, rows: list[dict], problems: list[str]) -> str:
    lines = [f"# Third-party licences: {platform}", ""]
    lines.append("Result: **PASS**" if not problems else f"Result: **FAIL** ({len(problems)} problem(s))")
    lines += ["", "| Component | Version | Licence (SPDX) | Linking | Files | Project |", "|---|---|---|---|---|---|"]
    for row in rows:
        lines.append(f"| {row['name']} | {row['version']} | {row['licence']} | {row['linking']} | "
                     f"{row['files']} | {row['homepage']} |")
    if problems:
        lines += ["", "## Problems", ""] + [f"- {problem}" for problem in problems]
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    sub = parser.add_subparsers(dest="platform", required=True)
    windows = sub.add_parser("windows")
    windows.add_argument("--vcpkg-installed", type=Path, required=True)
    windows.add_argument("--triplet", default="x64-windows")
    linux = sub.add_parser("linux")
    for platform_parser in (windows, linux):
        platform_parser.add_argument("--bin", type=Path, required=True, help="folder with the built executables")
        platform_parser.add_argument("--report", type=Path, help="write the Markdown report to this file")
    args = parser.parse_args(argv)

    try:
        policy, components = load_table(args.table)
    except (OSError, ValueError, tomllib.TOMLDecodeError) as error:
        print(f"licence table {args.table}: {error}", file=sys.stderr)
        return 1
    problems = check_policy(policy, components)
    if args.platform == "windows":
        rows, found = check_windows(policy, components, args.vcpkg_installed, args.triplet, args.bin)
    else:
        rows, found = check_linux(policy, components, args.bin)
    problems += found

    report = render_report(args.platform, rows, problems)
    if args.report:
        args.report.write_text(report, encoding="utf-8", newline="\n")
    sys.stdout.write(report)
    for problem in problems:
        print(f"error: licence check: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
