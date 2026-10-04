// cloudscope-info: what is this build made of, and do its libraries work on this machine?
//
//   cloudscope-info               build and library versions
//   cloudscope-info --self-test   additionally exercise every library
//   cloudscope-info --json        the same as JSON
//
// Exit codes: 0 success, 1 a self-test check failed, 2 wrong usage.

#include <cloudscope/common/build_info.hpp>
#include <cloudscope/common/self_test.hpp>

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitSelfTestFailed = 1;
constexpr int kExitUsage = 2;

void write(std::FILE* stream, const std::string& text)
{
    std::fwrite(text.data(), 1, text.size(), stream);
}

std::string self_test_text(const std::vector<cloudscope::SelfTestResult>& results)
{
    std::string out = "Self-test:\n";
    for (const cloudscope::SelfTestResult& result : results) {
        out += result.passed ? "  PASS  " : "  FAIL  ";
        out += result.name + ": " + result.detail + "\n";
    }
    return out;
}

nlohmann::ordered_json self_test_json(const std::vector<cloudscope::SelfTestResult>& results)
{
    nlohmann::ordered_json checks = nlohmann::ordered_json::array();
    for (const cloudscope::SelfTestResult& result : results) {
        checks.push_back({{"name", result.name}, {"passed", result.passed}, {"detail", result.detail}});
    }
    return checks;
}

}  // namespace

int main(int argc, char* argv[])
{
    const QCoreApplication app(argc, argv);
    const cloudscope::BuildInfo& info = cloudscope::build_info();
    QCoreApplication::setApplicationName(QStringLiteral("cloudscope-info"));
    QCoreApplication::setApplicationVersion(QString::fromStdString(info.version));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Shows what this CloudScope build is made of and checks that its libraries work."));
    const QCommandLineOption help_option = parser.addHelpOption();
    const QCommandLineOption version_option = parser.addVersionOption();
    const QCommandLineOption json_option(QStringLiteral("json"), QStringLiteral("Print the report as JSON."));
    const QCommandLineOption self_test_option(
        QStringLiteral("self-test"),
        QStringLiteral("Exercise every bundled library. The exit code is 1 if a check fails."));
    parser.addOption(json_option);
    parser.addOption(self_test_option);

    // parse(), not process(): on Windows, process() reports errors and --help in a message box when the
    // program has no console (service, scheduled task, remote shell) and then waits for a click forever.
    if (!parser.parse(QCoreApplication::arguments())) {
        write(stderr, parser.errorText().toStdString() + "\nTry 'cloudscope-info --help'.\n");
        return kExitUsage;
    }
    if (!parser.positionalArguments().isEmpty()) {
        write(stderr, "Unexpected argument '" + parser.positionalArguments().constFirst().toStdString() +
                          "'.\nTry 'cloudscope-info --help'.\n");
        return kExitUsage;
    }
    if (parser.isSet(help_option)) {
        write(stdout, parser.helpText().toStdString());
        return kExitOk;
    }
    if (parser.isSet(version_option)) {
        write(stdout, "cloudscope-info " + info.version + "\n");
        return kExitOk;
    }

    const bool with_self_test = parser.isSet(self_test_option);
    std::vector<cloudscope::SelfTestResult> results;
    if (with_self_test) {
        results = cloudscope::run_self_test();
    }

    if (parser.isSet(json_option)) {
        nlohmann::ordered_json root = nlohmann::ordered_json::parse(cloudscope::to_json(info));
        if (with_self_test) {
            root["self_test"] = self_test_json(results);
        }
        write(stdout, root.dump(2) + "\n");
    } else {
        write(stdout, cloudscope::to_text(info));
        if (with_self_test) {
            write(stdout, self_test_text(results));
        }
    }
    return cloudscope::all_passed(results) ? kExitOk : kExitSelfTestFailed;
}
