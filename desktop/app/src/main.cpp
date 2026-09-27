// CloudScope Desktop — Ph25: CLI harness (camera matrix + version).
// Full QML GUI wires up in Ph28.
#include <QCoreApplication>
#include <QCommandLineParser>

#include <iostream>

#include "CameraSource.h"

namespace {
int runCamTest()
{
    auto devs = CameraSource::enumerateLocal(5);
    int ok = 0;
    for (const auto& d : devs) {
        std::cout << d.name << (d.opened ? " OPEN " : " n/a ");
        if (d.opened) {
            std::cout << d.width << "x" << d.height;
            CameraSource src;
            int grabbed = 0;
            if (src.open(std::to_string(d.index))) {
                cv::Mat f;
                for (int i = 0; i < 5 && src.read(f); ++i)
                    ++grabbed;
            }
            std::cout << " grabbed=" << grabbed;
            if (grabbed > 0)
                ++ok;
        }
        std::cout << "\n";
    }
    std::cout << "usable cameras: " << ok << "\n";
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("CloudScope");
    app.setApplicationVersion("0.1.0-ph25");

    QCommandLineParser cli;
    cli.setApplicationDescription("CloudScope Desktop (camera matrix probe)");
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addOption({{"c", "camtest"}, "Probe local cameras 0..4 and grab frames."});
    cli.process(app);

    if (cli.isSet("camtest"))
        return runCamTest();

    std::cout << "CloudScope 0.1.0-ph25: use --camtest. GUI lands in Ph28.\n";
    return 0;
}
