// cloudscope-info: what is this build made of, do its libraries work, and which configuration is in effect?
//
//   cloudscope-info                 build and library versions
//   cloudscope-info --self-test     additionally exercise every library
//   cloudscope-info --show-config   additionally print the configuration in effect and where it came from
//   cloudscope-info --config FILE   read FILE on top of the standard configuration files (may be repeated)
//   cloudscope-info --json          the same as JSON
//
// Exit codes: 0 success, 1 a self-test check failed, the configuration is invalid or the report could not be
// written, 2 wrong usage, 3 internal error (a bug).

#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/build_info.hpp>
#include <cloudscope/common/self_test.hpp>

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitInternalError = 3;

// False if the stream did not take the whole text (closed pipe, full disk).
bool write(std::FILE* stream, const std::string& text)
{
    return std::fwrite(text.data(), 1, text.size(), stream) == text.size();
}

std::string display(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
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

std::string present(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error) ? "" : "  (not present)";
}

std::string config_text(const cloudscope::AppPaths& paths, const std::vector<std::filesystem::path>& extra_files,
                        const cloudscope::LoadedConfig& loaded)
{
    std::string out = "Configuration files, lowest priority first:\n";
    out += "  system:  " + display(paths.system_config) + present(paths.system_config) + "\n";
    out += "  user:    " + display(paths.user_config) + present(paths.user_config) + "\n";
    for (const std::filesystem::path& file : extra_files) {
        out += "  --config " + display(file) + "\n";
    }
    out += "Log folder: " + display(paths.log_directory) + "\n";
    for (const std::string& note : loaded.notes) {
        out += "Note: " + note + "\n";
    }
    out += "Configuration in effect:\n";
    const auto toml = cloudscope::to_toml(loaded.effective);
    out += toml ? *toml : toml.error().to_string() + "\n";
    return out;
}

nlohmann::ordered_json config_json(const cloudscope::AppPaths& paths, const cloudscope::LoadedConfig& loaded)
{
    nlohmann::ordered_json files = nlohmann::ordered_json::array();
    for (const std::filesystem::path& file : loaded.files) {
        files.push_back(display(file));
    }
    nlohmann::ordered_json root;
    root["system_file"] = display(paths.system_config);
    root["user_file"] = display(paths.user_config);
    root["log_folder"] = display(paths.log_directory);
    root["files_read"] = std::move(files);
    root["notes"] = loaded.notes;
    root["effective"] = loaded.effective;
    return root;
}

int run(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    const cloudscope::BuildInfo& info = cloudscope::build_info();
    QCoreApplication::setApplicationName(QStringLiteral("cloudscope-info"));
    QCoreApplication::setApplicationVersion(QString::fromStdString(info.version));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Shows what this CloudScope build is made of, checks that its libraries work, and prints the "
                       "configuration in effect."));
    const QCommandLineOption help_option = parser.addHelpOption();
    const QCommandLineOption version_option = parser.addVersionOption();
    const QCommandLineOption json_option(QStringLiteral("json"), QStringLiteral("Print the report as JSON."));
    const QCommandLineOption self_test_option(
        QStringLiteral("self-test"),
        QStringLiteral("Exercise every bundled library. The exit code is 1 if a check fails."));
    const QCommandLineOption show_config_option(
        QStringLiteral("show-config"), QStringLiteral("Print the configuration in effect and the files it came from. "
                                                      "The exit code is 1 if a configuration file is invalid."));
    const QCommandLineOption config_option(
        QStringLiteral("config"),
        QStringLiteral("Read this configuration file on top of the standard ones (may be repeated). "
                       "Implies --show-config."),
        QStringLiteral("file"));
    parser.addOption(json_option);
    parser.addOption(self_test_option);
    parser.addOption(show_config_option);
    parser.addOption(config_option);

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
        return write(stdout, parser.helpText().toStdString()) ? kExitOk : kExitFailed;
    }
    if (parser.isSet(version_option)) {
        return write(stdout, "cloudscope-info " + info.version + "\n") ? kExitOk : kExitFailed;
    }

    const bool with_self_test = parser.isSet(self_test_option);
    const bool with_config = parser.isSet(show_config_option) || parser.isSet(config_option);
    const bool as_json = parser.isSet(json_option);
    bool failed = false;

    nlohmann::ordered_json json_report = nlohmann::ordered_json::parse(cloudscope::to_json(info));
    std::string text_report = cloudscope::to_text(info);

    if (with_self_test) {
        const std::vector<cloudscope::SelfTestResult> results = cloudscope::run_self_test();
        failed = failed || !cloudscope::all_passed(results);
        json_report["self_test"] = self_test_json(results);
        text_report += self_test_text(results);
    }

    if (with_config) {
        std::vector<std::filesystem::path> extra_files;
        for (const QString& file : parser.values(config_option)) {
            extra_files.emplace_back(file.toStdU16String());
        }
        const cloudscope::AppPaths paths = cloudscope::standard_paths();
        const auto loaded = cloudscope::load_app_config(paths, extra_files);
        if (loaded) {
            json_report["configuration"] = config_json(paths, *loaded);
            text_report += config_text(paths, extra_files, *loaded);
        } else {
            failed = true;
            json_report["configuration"] = {{"error", loaded.error().to_string()}};
            write(stderr, "Configuration error: " + loaded.error().message + "\n");
        }
    }

    if (!write(stdout, as_json ? json_report.dump(2) + "\n" : text_report)) {
        failed = true;
    }
    return failed ? kExitFailed : kExitOk;
}

}  // namespace

int main(int argc, char** argv)
{
    // Nothing may leave main() as an exception: the program would end without a message.
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        write(stderr, std::string("Internal error: ") + error.what() + "\n");
    } catch (...) {
        write(stderr, "Internal error of unknown kind.\n");
    }
    return kExitInternalError;
}
