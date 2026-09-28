// CloudScope Desktop — CLI harness + headless inference test.
// Full QML GUI wires up in Ph28.
#include <QCoreApplication>
#include <QCommandLineParser>

#include <iostream>

#include <opencv2/imgcodecs.hpp>
#include "CameraSource.h"
#include "OnnxInfer.h"

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

int runInferTest(const QString& image, const QString& clsModel,
                 const QString& segModel)
{
    cv::Mat frame = cv::imread(image.toStdString(), cv::IMREAD_COLOR);
    if (frame.empty()) {
        std::cerr << "cannot read image: " << image.toStdString() << "\n";
        return 2;
    }
    OnnxInfer infer;
    if (!infer.load(clsModel.toStdString(), segModel.toStdString())) {
        std::cerr << "no model loaded\n";
        return 2;
    }
    std::cout << "backend=" << infer.backend() << "\n";
    if (infer.hasClassifier()) {
        auto r = infer.classify(frame);
        std::cout << "class=" << r.label << " conf=" << r.confidence << " top3=";
        for (const auto& t : r.top3)
            std::cout << t.first << ":" << t.second << " ";
        std::cout << "\n";
    }
    if (infer.hasSegmenter()) {
        cv::Mat mask = infer.segment(frame);
        if (!mask.empty()) {
            int cloud = cv::countNonZero(mask == 1);
            int sky = cv::countNonZero(mask == 0);
            std::cout << "mask=" << mask.cols << "x" << mask.rows
                      << " cloud_frac=" << (double)cloud / mask.total()
                      << " sky_frac=" << (double)sky / mask.total() << "\n";
        }
    }
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("CloudScope");
    app.setApplicationVersion("0.2.0-ph26");

    QCommandLineParser cli;
    cli.setApplicationDescription("CloudScope Desktop (camera probe + inference test)");
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addOption({{"c", "camtest"}, "Probe local cameras 0..4 and grab frames."});
    cli.addOption({"infertest", "Classify+segment one image.", "image"});
    cli.addOption({"cls-model", "Classifier ONNX path.",
                   "cls-model", "models/cloudscope_b0268_expa3.onnx"});
    cli.addOption({"seg-model", "Segmenter ONNX path.",
                   "seg-model", "models/cloudscope_seg_v2.onnx"});
    cli.process(app);

    if (cli.isSet("camtest"))
        return runCamTest();
    if (cli.isSet("infertest"))
        return runInferTest(cli.value("infertest"), cli.value("cls-model"),
                            cli.value("seg-model"));

    std::cout << "CloudScope 0.2.0-ph26: use --camtest or --infertest <img>. GUI in Ph28.\n";
    return 0;
}
