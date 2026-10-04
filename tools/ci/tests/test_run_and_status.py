import os
import subprocess
import sys
from pathlib import Path

import pytest

import run
import status

RUN_PY = str(Path(run.__file__))


def run_step(name: str, code: str, in_actions: bool) -> subprocess.CompletedProcess:
    full_env = dict(os.environ)
    full_env.pop("GITHUB_ACTIONS", None)
    if in_actions:
        full_env["GITHUB_ACTIONS"] = "true"
    return subprocess.run([sys.executable, RUN_PY, name, sys.executable, "-c", code],
                          capture_output=True, text=True, env=full_env, check=False)


# -------------------------------------------------------------------------------------------------- run


def test_escaping_for_workflow_commands():
    assert run.escape_data("50% done\r\nnext") == "50%25 done%0D%0Anext"
    assert run.escape_property("Build: a, b") == "Build%3A a%2C b"


def test_chunks_respect_the_size_limit_and_keep_every_line():
    lines = [f"line {i:03d} " + "x" * 40 for i in range(100)]
    chunks = run.chunk_lines(lines, 500)
    assert all(len(chunk) <= 500 for chunk in chunks)
    assert "\n".join(chunks).split("\n") == lines
    assert run.chunk_lines(["y" * 2000], 500) == ["y" * 500]          # an overlong line is cut, not dropped


def test_failure_annotations_put_error_lines_first_and_keep_the_log_end():
    tail = [f"output {i}" for i in range(2000)]
    commands = run.failure_annotations("Build", 3, ["a.cpp(3): error C2065"], tail)
    assert len(commands) <= run.MAX_ANNOTATIONS
    assert commands[0] == "::error title=Build%3A error lines::a.cpp(3): error C2065"
    assert "exit code 3" in commands[1]
    assert commands[-1].endswith("output 1999")
    assert all(len(command) < 5000 for command in commands)


def test_error_lines_are_recognised_but_passing_tests_with_error_in_their_name_are_not():
    assert run.is_error_line("main.cpp:7:1: error: expected ';'")
    assert run.is_error_line(r"D:\a\x\test.cpp(30): FAILED:")
    assert run.is_error_line(" 3/15 Test  #3: unit.all_passed ...***Failed    0.02 sec")
    assert run.is_error_line("CMake Error at CMakeLists.txt:34 (include):")
    assert run.is_error_line("ld: undefined reference to `foo()'")
    assert not run.is_error_line("      Start 15: cli.rejects wrong usage with a message on standard error")
    assert not run.is_error_line("15/15 Test #15: cli.message on standard error ...   Passed    0.09 sec")
    assert not run.is_error_line("[3/7] Linking CXX static library lib/libcloudscope-core.a")


def test_successful_command_passes_output_through_without_annotations():
    result = run_step("Step", "print('hello'); print('error: only a word in normal output')", in_actions=True)
    assert result.returncode == 0
    assert "hello" in result.stdout and "::group::Step" in result.stdout
    assert "::error" not in result.stdout


def test_failing_command_returns_its_exit_code_and_writes_annotations():
    code = "import sys; print('compiling'); print('main.cpp:7:1: error: expected ;'); sys.exit(5)"
    result = run_step("Build (Debug)", code, in_actions=True)
    assert result.returncode == 5
    assert "::error title=Build (Debug)%3A error lines::main.cpp:7:1: error: expected ;" in result.stdout
    assert "::error title=Build (Debug) failed (exit code 5) [log end]::compiling%0Amain.cpp:7:1: error: expected ;" in result.stdout


def test_outside_github_actions_nothing_is_added():
    result = run_step("Step", "import sys; print('boom'); sys.exit(1)", in_actions=False)
    assert result.returncode == 1
    assert result.stdout.strip() == "boom"


def test_command_that_cannot_start_reports_127():
    result = subprocess.run([sys.executable, RUN_PY, "Step", "no-such-program-cloudscope"],
                            capture_output=True, text=True, check=False)
    assert result.returncode == 127
    assert "Cannot start" in result.stderr


def test_usage_error_without_a_command():
    assert subprocess.run([sys.executable, RUN_PY, "only-a-name"], capture_output=True, check=False).returncode == 2


# ----------------------------------------------------------------------------------------------- status


@pytest.mark.parametrize("url", [
    "https://github.com/DuhItzAniket/CloudScope.git",
    "https://github.com/DuhItzAniket/CloudScope",
    "git@github.com:DuhItzAniket/CloudScope.git",
    "ssh://git@github.com/DuhItzAniket/CloudScope.git/",
])
def test_repository_slug(url):
    assert status.repository_slug(url) == "DuhItzAniket/CloudScope"


def test_repository_slug_rejects_other_hosts():
    with pytest.raises(ValueError):
        status.repository_slug("https://gitlab.com/someone/project.git")


def check_run(state: str, conclusion: str | None = None) -> dict:
    return {"name": "job", "status": state, "conclusion": conclusion, "html_url": "", "id": 1}


@pytest.mark.parametrize("runs, expected", [
    ([], 3),
    ([check_run("completed", "success"), check_run("completed", "skipped")], 0),
    ([check_run("completed", "success"), check_run("in_progress")], 2),
    ([check_run("queued")], 2),
    ([check_run("completed", "failure"), check_run("in_progress")], 1),
    ([check_run("completed", "cancelled")], 1),
    ([check_run("completed", "timed_out"), check_run("completed", "success")], 1),
])
def test_exit_code_for_check_runs(runs, expected):
    assert status.exit_code_for(runs) == expected


def test_annotations_are_printed_in_reading_order():
    titles = ["Test (Debug) failed (exit code 8) [log end, part 2/2]", None,
              "Test (Debug) failed (exit code 8) [log end, part 1/2]", "Test (Debug): error lines"]
    ordered = sorted(({"title": title} for title in titles), key=status.annotation_order)
    assert [item["title"] for item in ordered] == [
        "Test (Debug): error lines", "Test (Debug) failed (exit code 8) [log end, part 1/2]",
        "Test (Debug) failed (exit code 8) [log end, part 2/2]", None]


def test_summary_counts():
    runs = [check_run("completed", "success"), check_run("completed", "failure"), check_run("in_progress")]
    assert status.summarise(runs) == (1, 1, 1)
