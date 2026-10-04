// Runs the real cloudscope-info executable and checks its output and exit codes.

#include <cloudscope/common/build_info.hpp>

#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <string>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

constexpr int kTimeoutMs = 20000;

struct RunResult {
    bool finished = false;
    int exit_code = -1;
    std::string out;
    std::string err;
};

RunResult run_info(const QStringList& arguments)
{
    QProcess process;
#ifndef Q_OS_WIN
    // In a plain "C" locale (containers, some services) Qt prints a locale warning on standard error.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process.setProcessEnvironment(environment);
#endif
    process.start(QStringLiteral(CLOUDSCOPE_INFO_EXE), arguments);
    RunResult result;
    result.finished = process.waitForFinished(kTimeoutMs);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(kTimeoutMs);
        return result;
    }
    result.exit_code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.out = process.readAllStandardOutput().toStdString();
    result.err = process.readAllStandardError().toStdString();
    return result;
}

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

}  // namespace

TEST_CASE("cloudscope-info prints the build report", "[cli][info]")
{
    const RunResult result = run_info({});
    REQUIRE(result.finished);
    CHECK(result.exit_code == 0);
    const cloudscope::BuildInfo& info = cloudscope::build_info();
    CHECK_THAT(result.out, StartsWith("CloudScope " + info.version + " (git " + info.git_revision + ")"));
    CHECK_THAT(result.out, ContainsSubstring("CFITSIO"));
    CHECK_THAT(result.out, !ContainsSubstring("Self-test"));
}

TEST_CASE("cloudscope-info --self-test --json reports every check as passed", "[cli][info]")
{
    const RunResult result = run_info({QStringLiteral("--self-test"), QStringLiteral("--json")});
    REQUIRE(result.finished);
    CHECK(result.exit_code == 0);

    const nlohmann::json report = nlohmann::json::parse(result.out);
    CHECK(report.at("name") == "CloudScope");
    CHECK(report.at("dependencies").size() == cloudscope::build_info().dependencies.size());
    const nlohmann::json& checks = report.at("self_test");
    REQUIRE(checks.size() == 7);
    for (const nlohmann::json& check : checks) {
        INFO(check.dump());
        CHECK(check.at("passed") == true);
    }
}

TEST_CASE("cloudscope-info --json without --self-test has no self-test section", "[cli][info]")
{
    const RunResult result = run_info({QStringLiteral("--json")});
    REQUIRE(result.finished);
    CHECK(result.exit_code == 0);
    CHECK_FALSE(nlohmann::json::parse(result.out).contains("self_test"));
}

TEST_CASE("cloudscope-info --help and --version print to standard output", "[cli][info]")
{
    const RunResult help = run_info({QStringLiteral("--help")});
    REQUIRE(help.finished);
    CHECK(help.exit_code == 0);
    CHECK_THAT(help.out, ContainsSubstring("--self-test"));
    CHECK_THAT(help.out, ContainsSubstring("--json"));
    CHECK(help.err.empty());

    const RunResult version = run_info({QStringLiteral("--version")});
    REQUIRE(version.finished);
    CHECK(version.exit_code == 0);
    CHECK_THAT(version.out, StartsWith("cloudscope-info " + cloudscope::build_info().version));
}

TEST_CASE("cloudscope-info rejects wrong usage with exit code 2 and a message on standard error", "[cli][info]")
{
    const RunResult unknown_option = run_info({QStringLiteral("--no-such-option")});
    REQUIRE(unknown_option.finished);
    CHECK(unknown_option.exit_code == 2);
    CHECK(unknown_option.out.empty());
    CHECK_THAT(unknown_option.err, ContainsSubstring("no-such-option"));
    CHECK_THAT(unknown_option.err, ContainsSubstring("--help"));

    const RunResult positional = run_info({QStringLiteral("stray")});
    REQUIRE(positional.finished);
    CHECK(positional.exit_code == 2);
    CHECK_THAT(positional.err, ContainsSubstring("stray"));
}

#ifdef Q_OS_WIN
// Regression test (P011): started without a console and without redirected output, QCommandLineParser::process()
// showed its error in a message box and the program waited forever for a click that nobody could give.
TEST_CASE("cloudscope-info does not open a dialog when it has no console", "[cli][info][windows]")
{
    QProcess process;
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags = (arguments->flags & ~static_cast<unsigned long>(CREATE_NO_WINDOW)) | DETACHED_PROCESS;
        arguments->startupInfo->dwFlags &= ~static_cast<unsigned long>(STARTF_USESTDHANDLES);
    });
    process.start(QStringLiteral(CLOUDSCOPE_INFO_EXE), {QStringLiteral("--no-such-option")});
    const bool finished = process.waitForFinished(kTimeoutMs);
    if (!finished) {
        process.kill();
        process.waitForFinished(kTimeoutMs);
    }
    REQUIRE(finished);
    CHECK(process.exitStatus() == QProcess::NormalExit);
    CHECK(process.exitCode() == 2);
}
#endif
