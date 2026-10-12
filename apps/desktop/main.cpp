// cloudscope-desktop: the CloudScope Desktop workbench (Stage D).
//
//   cloudscope-desktop                              start with the configured camera and layout
//   cloudscope-desktop --config FILE                read FILE on top of the standard configuration (may be repeated)
//   cloudscope-desktop --camera ID [--mode M] [--start]   open a camera at start (and stream it)
//   cloudscope-desktop --renderer auto|opengl|raster
//   cloudscope-desktop --theme dark|night
//   cloudscope-desktop --reset-layout               start from the default panel layout
//   cloudscope-desktop --exit-after SECONDS         quit after SECONDS and print the preview report (benchmarks)
//
// Exit codes: 0 success, 1 the configuration is invalid or the application could not start, 2 wrong usage,
// 3 internal error (a bug).

#include "app_context.hpp"
#include "camera_profiles.hpp"
#include "live_view.hpp"
#include "main_window.hpp"
#include "theme.hpp"

#include <cloudscope/common/app_config.hpp>
#include <cloudscope/common/build_info.hpp>
#include <cloudscope/common/log.hpp>

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitInternalError = 3;

bool write(std::FILE* stream, const std::string& text)
{
    return std::fwrite(text.data(), 1, text.size(), stream) == text.size();
}

int run(int argc, char** argv)
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    const QApplication app(argc, argv);
    const cloudscope::BuildInfo& info = cloudscope::build_info();
    QCoreApplication::setApplicationName(QStringLiteral("cloudscope-desktop"));
    QCoreApplication::setOrganizationName(QStringLiteral("CloudScope"));
    QCoreApplication::setApplicationVersion(QString::fromStdString(info.version));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CloudScope Desktop: the sky-camera workbench."));
    const QCommandLineOption help_option = parser.addHelpOption();
    const QCommandLineOption version_option = parser.addVersionOption();
    const QCommandLineOption config_option(QStringLiteral("config"),
                                           QStringLiteral("Read this configuration file on top of the standard ones."),
                                           QStringLiteral("file"));
    const QCommandLineOption camera_option(QStringLiteral("camera"), QStringLiteral("Open this camera at start."),
                                           QStringLiteral("id"));
    const QCommandLineOption mode_option(
        QStringLiteral("mode"), QStringLiteral("Mode for --camera, e.g. 1920x1080@30/MJPEG."), QStringLiteral("mode"));
    const QCommandLineOption start_option(QStringLiteral("start"),
                                          QStringLiteral("Start streaming the camera at once."));
    const QCommandLineOption renderer_option(QStringLiteral("renderer"),
                                             QStringLiteral("Live-view renderer: auto, opengl or raster."),
                                             QStringLiteral("name"));
    const QCommandLineOption theme_option(QStringLiteral("theme"), QStringLiteral("Theme: dark or night."),
                                          QStringLiteral("name"));
    const QCommandLineOption reset_layout_option(QStringLiteral("reset-layout"),
                                                 QStringLiteral("Start from the default panel layout."));
    const QCommandLineOption exit_after_option(
        QStringLiteral("exit-after"), QStringLiteral("Quit after this many seconds and print the preview report."),
        QStringLiteral("seconds"));
    const QCommandLineOption no_gamepad_option(QStringLiteral("no-gamepad"), QStringLiteral("Do not open a gamepad."));
    for (const QCommandLineOption& option : {config_option, camera_option, mode_option, start_option, renderer_option,
                                             theme_option, reset_layout_option, exit_after_option, no_gamepad_option}) {
        parser.addOption(option);
    }
    // parse(), not process(): process() shows errors in a message box and waits (see cpp_style.md).
    if (!parser.parse(QCoreApplication::arguments())) {
        write(stderr, parser.errorText().toStdString() + "\nTry 'cloudscope-desktop --help'.\n");
        return kExitUsage;
    }
    if (!parser.positionalArguments().isEmpty()) {
        write(stderr, "Unexpected argument '" + parser.positionalArguments().constFirst().toStdString() +
                          "'.\nTry 'cloudscope-desktop --help'.\n");
        return kExitUsage;
    }
    if (parser.isSet(help_option)) {
        return write(stdout, parser.helpText().toStdString()) ? kExitOk : kExitFailed;
    }
    if (parser.isSet(version_option)) {
        return write(stdout, "cloudscope-desktop " + info.version + "\n") ? kExitOk : kExitFailed;
    }

    std::vector<std::filesystem::path> extra_files;
    for (const QString& file : parser.values(config_option)) {
        extra_files.emplace_back(file.toStdU16String());
    }
    auto context = cloudscope::desktop::AppContext::create(extra_files);
    if (!context) {
        const std::string text = "Configuration error: " + context.error().message;
        write(stderr, text + "\n");
        QMessageBox::critical(nullptr, QStringLiteral("CloudScope Desktop"), QString::fromStdString(text));
        return kExitFailed;
    }
    const cloudscope::AppPaths& paths = (*context)->paths();
    if (auto logging = cloudscope::logging_config((*context)->config().effective, paths)) {
        if (auto started = cloudscope::init_logging(*logging); !started) {
            write(stderr, "Logging: " + started.error().message + "\n");
        }
    }
    cloudscope::route_qt_messages_to_log();

    cloudscope::desktop::MainWindowOptions options;
    options.renderer = cloudscope::desktop::renderer_from_string(
        parser.isSet(renderer_option) ? parser.value(renderer_option).toStdString() : (*context)->settings().renderer);
    if (parser.isSet(theme_option)) {
        auto theme = cloudscope::desktop::theme_from_string(parser.value(theme_option).toStdString());
        if (!theme) {
            write(stderr, theme.error().message + "\n");
            return kExitUsage;
        }
        options.theme = *theme;
    }
    options.restore_layout = !parser.isSet(reset_layout_option);
    options.gamepad = !parser.isSet(no_gamepad_option);
    std::optional<cloudscope::hal::CameraMode> mode;
    if (parser.isSet(mode_option)) {
        auto parsed = cloudscope::desktop::parse_mode(parser.value(mode_option).toStdString());
        if (!parsed) {
            write(stderr, parsed.error().message + "\n");
            return kExitUsage;
        }
        mode = *parsed;
    }
    int exit_after_s = 0;
    if (parser.isSet(exit_after_option)) {
        bool ok = false;
        exit_after_s = parser.value(exit_after_option).toInt(&ok);
        if (!ok || exit_after_s <= 0) {
            write(stderr, "--exit-after needs a positive number of seconds.\n");
            return kExitUsage;
        }
    }

    cloudscope::desktop::MainWindow window(**context, options);
    window.show();
    const std::string camera =
        parser.isSet(camera_option) ? parser.value(camera_option).toStdString() : (*context)->settings().default_camera;
    window.open_camera_at_start(camera, mode, parser.isSet(start_option));
    if (exit_after_s > 0) {
        QTimer::singleShot(exit_after_s * 1000, &window, [&window] {
            write(stdout, window.preview_report().toStdString() + "\n");
            window.close();
        });
    }
    const int result = QApplication::exec();
    cloudscope::shutdown_logging();
    return result == 0 ? kExitOk : kExitFailed;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        write(stderr, std::string("Internal error: ") + error.what() + "\n");
    } catch (...) {
        write(stderr, "Internal error of unknown kind.\n");
    }
    return kExitInternalError;
}
