#include <cloudscope/common/self_test.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using cloudscope::SelfTestResult;

TEST_CASE("self-test exercises every bundled library and all checks pass", "[common][self_test]")
{
    const std::vector<SelfTestResult> results = cloudscope::run_self_test();

    std::set<std::string> names;
    for (const SelfTestResult& result : results) {
        INFO(result.name << ": " << result.detail);
        CHECK(result.passed);
        CHECK_FALSE(result.detail.empty());
        names.insert(result.name);
    }
    CHECK(names == std::set<std::string>{"qt-core", "opencv", "libjpeg-turbo", "cfitsio", "toml++", "nlohmann-json",
                                         "spdlog-fmt"});
    CHECK(cloudscope::all_passed(results));
}

TEST_CASE("all_passed reports a single failure", "[common][self_test]")
{
    CHECK(cloudscope::all_passed({}));
    CHECK(cloudscope::all_passed({{"a", true, "ok"}, {"b", true, "ok"}}));
    CHECK_FALSE(cloudscope::all_passed({{"a", true, "ok"}, {"b", false, "broken"}}));
}
