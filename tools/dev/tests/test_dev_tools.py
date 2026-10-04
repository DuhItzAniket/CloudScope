import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import format as fmt  # noqa: E402  (tools/dev/format.py)
import tidy  # noqa: E402

REPO = Path(__file__).resolve().parents[3]

TIDY_OUTPUT = f"""
{REPO.as_posix()}/core/src/common/log.cpp:103:5: error: use a ranges version of this algorithm [modernize-use-ranges,-warnings-as-errors]
  103 |     std::transform(lower.begin(), lower.end(), lower.begin(),
      |     ^
{REPO.as_posix()}/core/include/cloudscope/common/clock.hpp:24:12: warning: enum 'TimeSource' uses a larger base type [performance-enum-size]
{REPO.as_posix()}/core/include/cloudscope/common/clock.hpp:24:12: warning: enum 'TimeSource' uses a larger base type [performance-enum-size]
/usr/include/catch2/x.hpp:53:16: error: The value '5' is not in the valid range [clang-analyzer-optin.core.EnumCastOutOfRange,-warnings-as-errors]
note: this fix will not be applied because it overlaps with another fix
3 warnings generated.
"""


def test_tidy_findings_are_parsed_and_listed_once():
    findings = tidy.parse_findings(TIDY_OUTPUT, REPO)
    assert findings == {
        ("core/src/common/log.cpp", 103, 5, "modernize-use-ranges", "use a ranges version of this algorithm"),
        ("core/include/cloudscope/common/clock.hpp", 24, 12, "performance-enum-size",
         "enum 'TimeSource' uses a larger base type"),
        ("/usr/include/catch2/x.hpp", 53, 16, "clang-analyzer-optin.core.EnumCastOutOfRange",
         "The value '5' is not in the valid range"),
    }


def test_tidy_output_without_findings_gives_nothing():
    assert tidy.parse_findings("12 warnings generated.\nSuppressed 12 warnings (12 in non-user code).\n", REPO) == set()


def test_both_tools_cover_the_same_source_folders():
    tidy_files = {path.relative_to(REPO).as_posix() for path in tidy.source_files(REPO)}
    format_files = {path.relative_to(REPO).as_posix() for path in fmt.source_files(REPO)}
    assert "core/src/common/log.cpp" in tidy_files
    assert "tests/unit/test_log.cpp" in tidy_files
    assert "apps/info/main.cpp" in tidy_files
    assert tidy_files <= format_files                      # every analysed file is also formatted
    assert "core/include/cloudscope/common/log.hpp" in format_files
    assert not any(name.startswith(("build/", "legacy/")) for name in format_files)


def test_clang_format_version_is_read_from_its_banner():
    assert fmt.major_version("clang-format version 19.1.5") == 19
    assert fmt.major_version("Debian clang-format version 19.1.7 (3+b1)") == 19
    assert fmt.major_version("Ubuntu clang-format version 21.0.1") == 21
    assert fmt.major_version("no version here") is None
