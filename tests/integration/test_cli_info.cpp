// Runs the real cloudscope-info executable and checks its output and exit codes.

#include "test_support.hpp"

#include <cloudscope/common/build_info.hpp>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTemporaryDir>
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

// A private home folder for one test: the program under test looks for its configuration there,
// never in the developer's own profile.
class Home {
public:
    Home() { REQUIRE(temporary_.isValid()); }

    QString root() const { return temporary_.path(); }
    QString user_config() const
    {
#ifdef Q_OS_WIN
        return root() + QStringLiteral("/roaming/CloudScope/config.toml");
#else
        return root() + QStringLiteral("/config/cloudscope/config.toml");
#endif
    }
    void apply(QProcessEnvironment& environment) const
    {
#ifdef Q_OS_WIN
        environment.insert(QStringLiteral("PROGRAMDATA"), root() + QStringLiteral("/programdata"));
        environment.insert(QStringLiteral("APPDATA"), root() + QStringLiteral("/roaming"));
        environment.insert(QStringLiteral("LOCALAPPDATA"), root() + QStringLiteral("/local"));
#else
        environment.insert(QStringLiteral("HOME"), root());
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), root() + QStringLiteral("/config"));
        environment.insert(QStringLiteral("XDG_STATE_HOME"), root() + QStringLiteral("/state"));
#endif
    }
    static QString write(const QString& path, const char* text)
    {
        const QFileInfo info(path);
        REQUIRE(QDir().mkpath(info.absolutePath()));
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(text);
        return path;
    }

private:
    QTemporaryDir temporary_;
};

RunResult run_info(const QStringList& arguments, const Home* home = nullptr)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
#ifndef Q_OS_WIN
    // In a plain "C" locale (containers, some services) Qt prints a locale warning on standard error.
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
#endif
    if (home != nullptr) {
        home->apply(environment);
    }
    process.setProcessEnvironment(environment);
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

TEST_CASE("cloudscope-info --show-config reports the built-in defaults when no file exists", "[cli][info][config]")
{
    const Home home;
    const RunResult result = run_info({QStringLiteral("--show-config"), QStringLiteral("--json")}, &home);
    REQUIRE(result.finished);
    CHECK(result.exit_code == 0);
    CHECK(result.err.empty());

    const nlohmann::json configuration = nlohmann::json::parse(result.out).at("configuration");
    CHECK(configuration.at("files_read").empty());
    CHECK(configuration.at("notes").empty());
    CHECK(configuration.at("effective").at("schema_version") == 1);
    CHECK(configuration.at("effective").at("logging").at("level") == "info");
    // The standard locations follow the redirected home folder.
    const std::string root = home.root().toStdString();
    CHECK_THAT(QDir::fromNativeSeparators(QString::fromStdString(configuration.at("user_file").get<std::string>()))
                   .toStdString(),
               StartsWith(root));
    CHECK_THAT(QDir::fromNativeSeparators(QString::fromStdString(configuration.at("log_folder").get<std::string>()))
                   .toStdString(),
               StartsWith(root));

    const RunResult text = run_info({QStringLiteral("--show-config")}, &home);
    REQUIRE(text.finished);
    CHECK(text.exit_code == 0);
    CHECK_THAT(text.out, ContainsSubstring("Configuration files, lowest priority first:"));
    CHECK_THAT(text.out, ContainsSubstring("(not present)"));
    CHECK_THAT(text.out, ContainsSubstring("Configuration in effect:"));
    CHECK_THAT(text.out, ContainsSubstring("max_files = 5"));
}

TEST_CASE("cloudscope-info merges the user file and --config files in order", "[cli][info][config]")
{
    const Home home;
    Home::write(home.user_config(), "schema_version = 1\n[logging]\nlevel = \"debug\"\nmax_files = 3\n");
    const QString first = Home::write(home.root() + QStringLiteral("/first.toml"),
                                      "schema_version = 1\n[logging]\nmax_files = 7\nconsole = false\n");
    const QString second =
        Home::write(home.root() + QStringLiteral("/second.toml"), "schema_version = 1\n[logging]\nmax_files = 9\n");

    const RunResult result = run_info(
        {QStringLiteral("--config"), first, QStringLiteral("--config"), second, QStringLiteral("--json")}, &home);
    REQUIRE(result.finished);
    CHECK(result.exit_code == 0);

    const nlohmann::json configuration = nlohmann::json::parse(result.out).at("configuration");
    CHECK(configuration.at("files_read").size() == 3);
    const nlohmann::json& logging = configuration.at("effective").at("logging");
    CHECK(logging.at("level") == "debug");   // user file
    CHECK(logging.at("console") == false);   // first --config file
    CHECK(logging.at("max_files") == 9);     // the last file wins
    CHECK(logging.at("max_file_mb") == 10);  // built-in default
}

TEST_CASE("cloudscope-info reports an invalid configuration file and exits with code 1", "[cli][info][config]")
{
    const Home home;
    const QString bad = Home::write(home.root() + QStringLiteral("/bad.toml"),
                                    "schema_version = 1\n[logging]\nlevel = \"loud\"\nmax_fiels = 2\n");

    const RunResult result = run_info({QStringLiteral("--config"), bad}, &home);
    REQUIRE(result.finished);
    CHECK(result.exit_code == 1);
    CHECK_THAT(result.err, ContainsSubstring("bad.toml"));
    CHECK_THAT(result.err, ContainsSubstring("logging.level: must be one of \"trace\""));
    CHECK_THAT(result.err, ContainsSubstring("logging.max_fiels: unknown key (did you mean 'max_files'?)"));
    CHECK_THAT(result.out, !ContainsSubstring("Configuration in effect"));

    const RunResult json = run_info({QStringLiteral("--config"), bad, QStringLiteral("--json")}, &home);
    REQUIRE(json.finished);
    CHECK(json.exit_code == 1);
    CHECK(nlohmann::json::parse(json.out).at("configuration").contains("error"));

    const RunResult missing = run_info({QStringLiteral("--config"), home.root() + QStringLiteral("/typo.toml")}, &home);
    REQUIRE(missing.finished);
    CHECK(missing.exit_code == 1);
    CHECK_THAT(missing.err, ContainsSubstring("the file does not exist"));

    const RunResult no_value = run_info({QStringLiteral("--config")}, &home);
    REQUIRE(no_value.finished);
    CHECK(no_value.exit_code == 2);
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

TEST_CASE("cloudscope-info --devices lists the simulated devices of the default configuration", "[cli][info][devices]")
{
    const Home home;
    const RunResult result = run_info({QStringLiteral("--devices"), QStringLiteral("--json")}, &home);
    REQUIRE(result.finished);
    CHECK(result.exit_code == 0);
    CHECK(result.err.empty());

    const nlohmann::json report = nlohmann::json::parse(result.out);
    CHECK_FALSE(report.contains("configuration"));  // only what was asked for
    const nlohmann::json& devices = report.at("devices");
    REQUIRE(devices.size() == 5);
    CHECK(devices[0] == nlohmann::json{{"id", "sim:camera:sky"},
                                       {"kind", "camera"},
                                       {"name", "Simulated sky camera"},
                                       {"driver", "sim"},
                                       {"simulated", true}});
    CHECK(devices[1].at("id") == "sim:mount:pan-tilt");
    CHECK(devices[2].at("id") == "sim:imu:head");
    CHECK(devices[3].at("id") == "sim:sensor:gps");
    CHECK(devices[4].at("id") == "sim:sensor:environment");
    for (const nlohmann::json& device : devices) {
        CHECK(device.at("simulated") == true);  // never to be mistaken for hardware
    }

    const RunResult text = run_info({QStringLiteral("--devices")}, &home);
    REQUIRE(text.finished);
    CHECK(text.exit_code == 0);
    // (No line ends in the expected text: they differ between Windows and Linux.)
    CHECK_THAT(text.out, ContainsSubstring("Devices:"));
    CHECK_THAT(text.out, ContainsSubstring("  sim:camera:sky          camera    Simulated sky camera  [simulated]"));
    CHECK_THAT(text.out,
               ContainsSubstring("  sim:sensor:environment  sensor    Simulated environment sensors  [simulated]"));
    CHECK_THAT(text.out, !ContainsSubstring("Configuration in effect"));
}

TEST_CASE("cloudscope-info --devices follows the simulation settings", "[cli][info][devices][config]")
{
    const Home home;

    Home::write(home.user_config(), "schema_version = 1\n[simulation]\nenabled = false\n");
    const RunResult off = run_info({QStringLiteral("--devices"), QStringLiteral("--json")}, &home);
    REQUIRE(off.finished);
    CHECK(off.exit_code == 0);
    CHECK(nlohmann::json::parse(off.out).at("devices").empty());
    const RunResult off_text = run_info({QStringLiteral("--devices")}, &home);
    REQUIRE(off_text.finished);
    CHECK_THAT(off_text.out, ContainsSubstring("Devices: none"));

    // With a folder of pictures, the replay camera appears.
    const std::u8string folder = (cloudscope::test::data_dir() / "sky").generic_u8string();
    const std::string with_replay =
        "schema_version = 1\n[simulation]\nreplay_folder = \"" + std::string(folder.begin(), folder.end()) + "\"\n";
    Home::write(home.user_config(), with_replay.c_str());
    const RunResult replay = run_info({QStringLiteral("--devices"), QStringLiteral("--json")}, &home);
    REQUIRE(replay.finished);
    CHECK(replay.exit_code == 0);
    const nlohmann::json devices = nlohmann::json::parse(replay.out).at("devices");
    REQUIRE(devices.size() == 6);
    CHECK(devices[1].at("id") == "sim:camera:replay");
    CHECK(devices[1].at("simulated") == true);

    // A setting outside its range is an error that names the key; no device list is printed.
    Home::write(home.user_config(), "schema_version = 1\n[simulation]\nreplay_fps = 0\n");
    const RunResult invalid = run_info({QStringLiteral("--devices")}, &home);
    REQUIRE(invalid.finished);
    CHECK(invalid.exit_code == 1);
    CHECK_THAT(invalid.err, ContainsSubstring("simulation.replay_fps: must be at least 0.01"));
    CHECK_THAT(invalid.out, !ContainsSubstring("Devices"));
}
