// CloudScope Desktop — CLI harness + headless inference/overlay test.
// Full QML GUI wires up in Ph28.
#include <QCoreApplication>
#include <QCommandLineParser>

#include <iostream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "CameraSource.h"
#include "CloudVision.h"
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

const char* modeName(OverlayMode m)
{
    switch (m) {
        case OverlayMode::Rect: return "rect";
        case OverlayMode::Polygon: return "polygon";
        case OverlayMode::Symmetry: return "symmetry";
    }
    return "?";
}

int runInferTest(const QString& image, const QString& clsModel,
                 const QString& segModel, const QString& outPrefix)
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
    std::string label = "cloud";
    if (infer.hasClassifier()) {
        auto r = infer.classify(frame);
        label = r.label;
        std::cout << "class=" << r.label << " conf=" << r.confidence << " top3=";
        for (const auto& t : r.top3)
            std::cout << t.first << ":" << t.second << " ";
        std::cout << "\n";
    }
    if (infer.hasSegmenter()) {
        cv::Mat mask = infer.segment(frame);
        if (!mask.empty()) {
            cv::Mat cloudOnly = (mask == 1);
            auto objs = CloudVision::extract(cloudOnly);
            bool ok = CloudVision::sane(objs, frame.cols, frame.rows);
            std::cout << "objects=" << objs.size() << " sane=" << (ok ? 1 : 0) << "\n";
            if (!outPrefix.isEmpty()) {
                if (!ok) {
                    cv::putText(frame, label + " (mask uncertain)",
                                cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 1.2,
                                cv::Scalar(255, 255, 255), 2);
                    cv::imwrite(outPrefix.toStdString() + "_labelonly.jpg", frame);
                    std::cout << "saved label-only fallback\n";
                } else {
                    const OverlayMode modes[3] = {OverlayMode::Rect,
                                                  OverlayMode::Polygon,
                                                  OverlayMode::Symmetry};
                    for (auto m : modes) {
                        cv::Mat ann = frame.clone();
                        CloudVision::render(ann, objs, label, m);
                        cv::imwrite(outPrefix.toStdString() + "_" +
                                        modeName(m) + ".jpg",
                                    ann);
                    }
                    std::cout << "saved rect+polygon+symmetry\n";
                }
            }
        }
    }
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("CloudScope");
    app.setApplicationVersion("0.3.0-ph27");

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
    cli.addOption({"out", "Annotated output prefix (writes rect/polygon/symmetry).",
                   "out"});
    cli.process(app);

    if (cli.isSet("camtest"))
        return runCamTest();
    if (cli.isSet("infertest"))
        return runInferTest(cli.value("infertest"), cli.value("cls-model"),
                            cli.value("seg-model"), cli.value("out"));

    std::cout << "CloudScope 0.3.0-ph27: use --camtest or --infertest <img> [--out p]. GUI in Ph28.\n";
    return 0;
}
