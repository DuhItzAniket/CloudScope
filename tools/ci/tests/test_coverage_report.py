import coverage_report as cov


def entry(name: str, covered: int, total: int, branch: float = 50.0) -> dict:
    return {"filename": name, "line_covered": covered, "line_total": total,
            "line_percent": 100.0 * covered / total if total else 0.0, "branch_percent": branch}


SUMMARY = {"files": [
    entry("core/src/common/log.cpp", 180, 200),
    entry("core/include/cloudscope/common/units.hpp", 20, 20),
    entry("apps/info/main.cpp", 40, 80),
    entry("tests/unit/test_log.cpp", 300, 300),          # must not count
    entry("build/linux-x64/core/generated/x.cpp", 1, 1),  # must not count
]}


def test_only_core_counts_towards_the_requirement():
    assert cov.summarise(SUMMARY, cov.CORE) == (200, 220)
    assert cov.summarise(SUMMARY, cov.COUNTED) == (240, 300)


def test_windows_path_separators_are_understood():
    summary = {"files": [entry("core\\src\\common\\log.cpp", 5, 10)]}
    assert cov.summarise(summary, cov.CORE) == (5, 10)


def test_verdict_passes_at_and_above_the_threshold():
    passed, message = cov.verdict(SUMMARY, 70.0)
    assert passed
    assert "core library 90.9 % of lines (200/220)" in message
    assert "with applications 80.0 % (240/300)" in message
    assert "required 70 %" in message
    assert cov.verdict(SUMMARY, 90.9)[0]
    assert not cov.verdict(SUMMARY, 91.0)[0]


def test_nothing_measured_is_a_failure_not_a_pass():
    passed, message = cov.verdict({"files": []}, 0.0)
    assert not passed and "no lines" in message
    passed, _ = cov.verdict({"files": [entry("apps/info/main.cpp", 10, 10)]}, 0.0)
    assert not passed  # applications alone do not satisfy the core requirement


def test_table_lists_the_least_covered_file_first():
    lines = cov.table(SUMMARY).splitlines()
    assert lines[0].startswith("File")
    assert lines[1].startswith("apps/info/main.cpp")
    assert "40/80" in lines[1] and "50.0%" in lines[1]
    assert lines[-1].split()[0] in ("core/include/cloudscope/common/units.hpp", "tests/unit/test_log.cpp",
                                    "build/linux-x64/core/generated/x.cpp")


def test_table_copes_with_files_that_have_no_branches():
    summary = {"files": [{"filename": "core/src/a.cpp", "line_covered": 3, "line_total": 3,
                          "line_percent": 100.0, "branch_percent": None}]}
    line = cov.table(summary).splitlines()[1]
    assert line.startswith("core/src/a.cpp") and line.rstrip().endswith("-")


def test_percent_of_nothing_is_zero():
    assert cov.percent(0, 0) == 0.0
    assert cov.percent(1, 4) == 25.0
