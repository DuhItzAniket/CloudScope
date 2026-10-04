import json
from pathlib import Path

import pytest

import licence_check as lc

REPO = Path(__file__).resolve().parents[3]

TABLE = """
[policy]
allowed = ["MIT", "Zlib", "BSD-3-Clause", "IJG"]
allowed_dynamic = ["LGPL-3.0-only"]
allowed_system = ["GPL-3.0-or-later WITH GCC-exception-3.1"]

[[component]]
name = "Qt"
licence = "LGPL-3.0-only"
linking = "dynamic"
homepage = "https://www.qt.io/"
used_for = "framework"
windows_dlls = ["Qt6Core.dll"]
linux_libraries = ["libQt6Core.so.*"]

[[component]]
name = "zlib"
licence = "Zlib"
linking = "dynamic"
homepage = "https://zlib.net/"
used_for = "compression"
vcpkg_ports = ["zlib"]
linux_libraries = ["libz.so.*"]
"""

STATUS = """Package: vcpkg-cmake
Version: 2024-04-23
Architecture: x64-windows
Multi-Arch: same
Status: install ok installed

Package: zlib
Version: 1.3.2
Port-Version: 1
Depends: vcpkg-cmake
Architecture: x64-windows
Status: install ok installed

Package: opencv4
Feature: jpeg
Architecture: x64-windows
Status: install ok installed

Package: removed
Version: 1.0
Architecture: x64-windows
Status: purge ok not-installed

Package: zlib
Version: 1.3.2
Architecture: x64-linux
Status: install ok installed
"""


def write_table(tmp_path: Path, text: str = TABLE) -> Path:
    path = tmp_path / "third_party.toml"
    path.write_text(text, encoding="utf-8")
    return path


def make_vcpkg_tree(tmp_path: Path, ports: dict[str, str | None], dlls: dict[str, str]) -> Path:
    """Fake vcpkg_installed folder: ports -> recorded licence (None = unknown), dll name -> owning port."""
    installed = tmp_path / "vcpkg_installed"
    (installed / "vcpkg" / "info").mkdir(parents=True)
    paragraphs = []
    for port, licence in ports.items():
        paragraphs.append(f"Package: {port}\nVersion: 1.0\nArchitecture: x64-windows\nStatus: install ok installed\n")
        share = installed / "x64-windows" / "share" / port
        share.mkdir(parents=True)
        concluded = licence if licence is not None else "NOASSERTION"
        (share / "vcpkg.spdx.json").write_text(json.dumps({"packages": [{"licenseConcluded": concluded}]}))
        files = [f"x64-windows/bin/{dll}" for dll, owner in dlls.items() if owner == port]
        (installed / "vcpkg" / "info" / f"{port}_1.0_x64-windows.list").write_text("\n".join(files) + "\n")
    (installed / "vcpkg" / "status").write_text("\n".join(paragraphs))
    return installed


def make_bin(tmp_path: Path, names: list[str]) -> Path:
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    for name in names:
        (bin_dir / name).write_bytes(b"MZ")
    return bin_dir


# ------------------------------------------------------------------------------------------ expressions


@pytest.mark.parametrize("expression, expected", [
    ("MIT", {"MIT"}),
    ("IJG AND BSD-3-Clause AND Zlib", {"IJG", "BSD-3-Clause", "Zlib"}),
    ("(MIT OR Apache-2.0) AND Zlib", {"MIT", "Apache-2.0", "Zlib"}),
    ("GPL-3.0-or-later WITH GCC-exception-3.1", {"GPL-3.0-or-later WITH GCC-exception-3.1"}),
])
def test_licence_ids(expression, expected):
    assert lc.licence_ids(expression) == expected


@pytest.mark.parametrize("expression, allowed, expected", [
    ("MIT", {"MIT"}, True),
    ("GPL-3.0-only", {"MIT"}, False),
    ("IJG AND BSD-3-Clause AND Zlib", {"IJG", "BSD-3-Clause", "Zlib"}, True),
    ("IJG AND BSD-3-Clause AND Zlib", {"IJG", "Zlib"}, False),          # AND: every part must be allowed
    ("GPL-3.0-only OR MIT", {"MIT"}, True),                              # OR: one allowed choice is enough
    ("GPL-3.0-only OR LGPL-3.0-only", {"MIT"}, False),
    ("(GPL-2.0-only OR MIT) AND Zlib", {"MIT", "Zlib"}, True),
    ("(GPL-2.0-only OR MIT) AND Zlib", {"MIT"}, False),
    ("GPL-3.0-or-later WITH GCC-exception-3.1", {"GPL-3.0-or-later"}, False),   # the exception is part of the id
    ("GPL-3.0-or-later WITH GCC-exception-3.1", {"GPL-3.0-or-later WITH GCC-exception-3.1"}, True),
])
def test_is_allowed(expression, allowed, expected):
    assert lc.is_allowed(expression, allowed) is expected


@pytest.mark.parametrize("expression", ["", "MIT AND", "(MIT", "MIT OR OR Zlib", "MIT WITH", "MIT )", "AND MIT", "a/b"])
def test_malformed_expressions_are_rejected(expression):
    with pytest.raises(ValueError):
        lc.parse_expression(expression)


# ------------------------------------------------------------------------------------------------ table


def test_table_loads_and_policy_depends_on_linking(tmp_path):
    policy, components = lc.load_table(write_table(tmp_path))
    assert [c.name for c in components] == ["Qt", "zlib"]
    assert "LGPL-3.0-only" in policy.allowed_for("dynamic")
    assert "LGPL-3.0-only" not in policy.allowed_for("static")
    assert "LGPL-3.0-only" not in policy.allowed_for("header-only")
    assert "GPL-3.0-or-later WITH GCC-exception-3.1" in policy.allowed_for("system")
    assert lc.check_policy(policy, components) == []


def test_statically_linked_lgpl_is_a_policy_violation(tmp_path):
    policy, components = lc.load_table(write_table(tmp_path, TABLE.replace('linking = "dynamic"', 'linking = "static"', 1)))
    problems = lc.check_policy(policy, components)
    assert len(problems) == 1 and "Qt" in problems[0] and "static" in problems[0]


def test_gpl_component_is_a_policy_violation(tmp_path):
    policy, components = lc.load_table(write_table(tmp_path, TABLE.replace('licence = "Zlib"', 'licence = "GPL-3.0-only"')))
    assert any("zlib" in problem and "GPL-3.0-only" in problem for problem in lc.check_policy(policy, components))


@pytest.mark.parametrize("old, new, message", [
    ('linking = "dynamic"', 'linking = "sometimes"', "linking must be"),
    ('used_for = "framework"', "", "missing fields"),
    ('used_for = "framework"', 'used_for = "framework"\ncolour = "red"', "unknown fields"),
    ('name = "zlib"', 'name = "Qt"', "listed twice"),
])
def test_invalid_tables_are_rejected(tmp_path, old, new, message):
    with pytest.raises(ValueError, match=message):
        lc.load_table(write_table(tmp_path, TABLE.replace(old, new, 1)))


def test_repository_table_is_valid_and_passes_its_own_policy():
    policy, components = lc.load_table(lc.DEFAULT_TABLE)
    assert lc.check_policy(policy, components) == []
    assert {"Qt", "OpenCV", "CFITSIO", "libjpeg-turbo"} <= {c.name for c in components}
    # No GPL-only or AGPL licence may ever be in the permissive lists (ADR-010, rule 3).
    for licence in policy.allowed | policy.allowed_dynamic:
        assert not licence.startswith(("GPL", "AGPL")), licence


# ---------------------------------------------------------------------------------------------- Windows


def test_vcpkg_status_lists_installed_ports_of_the_triplet_only():
    assert lc.parse_vcpkg_status(STATUS, "x64-windows") == {"vcpkg-cmake": "2024-04-23", "zlib": "1.3.2#1"}
    assert lc.parse_vcpkg_status(STATUS.replace("\n", "\r\n"), "x64-windows") == {"vcpkg-cmake": "2024-04-23", "zlib": "1.3.2#1"}


def test_windows_build_with_listed_components_passes(tmp_path):
    policy, components = lc.load_table(write_table(tmp_path))
    installed = make_vcpkg_tree(tmp_path, {"zlib": "Zlib"}, {"z.dll": "zlib"})
    bin_dir = make_bin(tmp_path, ["z.dll", "Qt6Core.dll", "app.exe"])
    rows, problems = lc.check_windows(policy, components, installed, "x64-windows", bin_dir)
    assert problems == []
    assert {row["name"]: row["files"] for row in rows} == {"Qt": "Qt6Core.dll", "zlib": "z.dll"}
    assert "PASS" in lc.render_report("windows", rows, problems)


def test_windows_unlisted_port_and_unattributed_dll_fail(tmp_path):
    policy, components = lc.load_table(write_table(tmp_path))
    installed = make_vcpkg_tree(tmp_path, {"zlib": "Zlib", "readline": "GPL-3.0-or-later"}, {"z.dll": "zlib"})
    bin_dir = make_bin(tmp_path, ["z.dll", "Qt6Core.dll", "mystery.dll"])
    rows, problems = lc.check_windows(policy, components, installed, "x64-windows", bin_dir)
    assert any("readline" in problem and "not listed" in problem for problem in problems)
    assert any("mystery.dll" in problem for problem in problems)
    report = lc.render_report("windows", rows, problems)
    assert "FAIL" in report and "mystery.dll" in report


def test_windows_licence_recorded_by_vcpkg_must_be_covered_by_the_table(tmp_path):
    policy, components = lc.load_table(write_table(tmp_path))
    bin_dir = make_bin(tmp_path, ["z.dll", "Qt6Core.dll"])
    changed = make_vcpkg_tree(tmp_path, {"zlib": "Zlib AND GPL-2.0-only"}, {"z.dll": "zlib"})
    _, problems = lc.check_windows(policy, components, changed, "x64-windows", bin_dir)
    assert any("vcpkg records licence" in problem for problem in problems)


def test_windows_unknown_vcpkg_licence_is_accepted_when_the_table_names_one(tmp_path):
    policy, components = lc.load_table(write_table(tmp_path))
    installed = make_vcpkg_tree(tmp_path, {"zlib": None}, {"z.dll": "zlib"})
    _, problems = lc.check_windows(policy, components, installed, "x64-windows", make_bin(tmp_path, ["z.dll", "Qt6Core.dll"]))
    assert problems == []


# ------------------------------------------------------------------------------------------------ Linux

READELF = """
Dynamic section at offset 0x2d58 contains 31 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libQt6Core.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libz.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000c (INIT)               0x4000
"""


def test_needed_libraries_are_read_from_readelf_output():
    assert lc.parse_needed(READELF) == ["libQt6Core.so.6", "libz.so.1", "libc.so.6"]


def test_linux_unlisted_library_fails(tmp_path, monkeypatch):
    policy, components = lc.load_table(write_table(tmp_path))
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    (bin_dir / "app").write_bytes(b"\x7fELF" + b"\0" * 16)
    (bin_dir / "notes.txt").write_text("not a program")

    class Completed:
        stdout = READELF
        returncode = 0

    monkeypatch.setattr(lc.subprocess, "run", lambda *args, **kwargs: Completed())
    monkeypatch.setattr(lc, "debian_package_version", lambda library: "")
    rows, problems = lc.check_linux(policy, components, bin_dir)
    assert [row["name"] for row in rows] == ["Qt", "zlib"]
    assert problems == ["libc.so.6 (needed by app) belongs to no listed component"]


def test_is_elf(tmp_path):
    elf = tmp_path / "program"
    elf.write_bytes(b"\x7fELF\x02\x01")
    text = tmp_path / "script.sh"
    text.write_text("#!/bin/sh\n")
    assert lc.is_elf(elf) and not lc.is_elf(text) and not lc.is_elf(tmp_path / "missing")
