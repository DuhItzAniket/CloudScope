// cloudscope-aibench: time a STRATIA model package on every ONNX Runtime engine of this build (P070, FR-AI-09).
//
//   cloudscope-aibench --model models/stratia-1.0.0-demo [--provider all|auto|cpu|directml|cuda] [--runs 20]
//                      [--warmup 3] [--threads 0] [--width 1920 --height 1080] [--picture frame.jpg]
//                      [--json] [--report docs/reviews/ai_benchmark.md]
//
// Exit codes: 0 success, 1 no engine ran the model, 2 wrong usage, 3 internal error.

#include <cloudscope/ai/benchmark.hpp>
#include <cloudscope/ai/model_card.hpp>
#include <cloudscope/ai/onnx_engine.hpp>
#include <cloudscope/common/log.hpp>
#include <cloudscope/hal/registry.hpp>

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>

#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using namespace cloudscope;

std::filesystem::path path_of(const QString& text)
{
    return std::filesystem::path(text.toStdU16String());
}

int run(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cloudscope-aibench"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Benchmark a STRATIA model package on every available execution provider."));
    const QCommandLineOption help = parser.addHelpOption();
    const QCommandLineOption model(QStringLiteral("model"),
                                   QStringLiteral("Model package folder (model_card.json + one .onnx)."),
                                   QStringLiteral("folder"));
    const QCommandLineOption provider(
        QStringLiteral("provider"),
        QStringLiteral("all (default), auto (the first that works), cpu, directml or cuda."), QStringLiteral("name"),
        QStringLiteral("all"));
    const QCommandLineOption runs(QStringLiteral("runs"), QStringLiteral("Timed runs per engine (default 20)."),
                                  QStringLiteral("n"), QStringLiteral("20"));
    const QCommandLineOption warmup(QStringLiteral("warmup"), QStringLiteral("Untimed runs before them (default 3)."),
                                    QStringLiteral("n"), QStringLiteral("3"));
    const QCommandLineOption threads(QStringLiteral("threads"),
                                     QStringLiteral("Threads of the CPU provider (0: default)."), QStringLiteral("n"),
                                     QStringLiteral("0"));
    const QCommandLineOption width(QStringLiteral("width"), QStringLiteral("Synthetic picture width (default 1920)."),
                                   QStringLiteral("px"), QStringLiteral("1920"));
    const QCommandLineOption height(QStringLiteral("height"),
                                    QStringLiteral("Synthetic picture height (default 1080)."), QStringLiteral("px"),
                                    QStringLiteral("1080"));
    const QCommandLineOption picture(QStringLiteral("picture"), QStringLiteral("A picture file to feed instead."),
                                     QStringLiteral("file"));
    const QCommandLineOption json(QStringLiteral("json"), QStringLiteral("Print the result as JSON."));
    const QCommandLineOption report(QStringLiteral("report"), QStringLiteral("Write a Markdown report to this file."),
                                    QStringLiteral("file"));
    parser.addOptions({model, provider, runs, warmup, threads, width, height, picture, json, report});
    if (!parser.parse(QCoreApplication::arguments())) {
        std::fprintf(stderr, "%s\nTry 'cloudscope-aibench --help'.\n", parser.errorText().toUtf8().constData());
        return 2;
    }
    if (parser.isSet(help)) {
        parser.showHelp(0);
    }
    if (!parser.isSet(model)) {
        std::fprintf(stderr, "--model is required.\nTry 'cloudscope-aibench --help'.\n");
        return 2;
    }
    auto package = ai::open_model_package(path_of(parser.value(model)));
    if (!package) {
        std::fprintf(stderr, "%s\n", package.error().to_string().c_str());
        return 2;
    }
    hal::DeviceRegistry registry;
    if (auto added = registry.add_driver(ai::OnnxDriver::make(parser.value(threads).toInt())); !added) {
        std::fprintf(stderr, "%s\n", added.error().to_string().c_str());
        return 3;
    }
    ai::BenchmarkOptions options;
    options.provider = parser.value(provider).toStdString();
    options.runs = std::max(1, parser.value(runs).toInt());
    options.warmup = std::max(0, parser.value(warmup).toInt());
    options.width = std::max(64, parser.value(width).toInt());
    options.height = std::max(64, parser.value(height).toInt());
    cv::Mat frame;
    if (parser.isSet(picture)) {
        frame = cv::imread(path_of(parser.value(picture)).string(), cv::IMREAD_COLOR);
        if (frame.empty()) {
            std::fprintf(stderr, "could not read the picture %s\n", parser.value(picture).toUtf8().constData());
            return 2;
        }
    }
    auto result = ai::benchmark_model(*package, registry, options, frame.empty() ? nullptr : &frame);
    if (!result) {
        std::fprintf(stderr, "%s\n", result.error().to_string().c_str());
        return 1;
    }
    if (parser.isSet(json)) {
        std::cout << ai::benchmark_json(*result).dump(2) << '\n';
    } else {
        std::cout << ai::benchmark_markdown(*result);
    }
    if (parser.isSet(report)) {
        std::ofstream out(path_of(parser.value(report)));
        out << ai::benchmark_markdown(*result);
        if (!out) {
            std::fprintf(stderr, "could not write %s\n", parser.value(report).toUtf8().constData());
            return 3;
        }
    }
    const bool any =
        std::any_of(result->rows.begin(), result->rows.end(), [](const ai::BenchmarkRow& r) { return r.ok; });
    return any ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "cloudscope-aibench: %s\n", error.what());
        return 3;
    }
}
