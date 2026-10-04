// Installation self-test: does every bundled library load and do real work on this machine?
// Catches missing DLLs or codecs after packaging or on a freshly set-up Raspberry Pi.
#pragma once

#include <string>
#include <vector>

namespace cloudscope {

struct SelfTestResult {
    std::string name;    // short identifier of the check, e.g. "cfitsio"
    bool passed = false;
    std::string detail;  // what was verified, or the reason for the failure
};

// Runs every check. Never throws; a failing check is reported in its result. Takes a few milliseconds.
[[nodiscard]] std::vector<SelfTestResult> run_self_test();

// True when every check passed.
[[nodiscard]] bool all_passed(const std::vector<SelfTestResult>& results);

}  // namespace cloudscope
