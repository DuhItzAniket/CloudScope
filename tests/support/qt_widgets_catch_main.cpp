// Entry point for the user-interface tests: a QApplication (widgets) exists while the Catch2 tests run. The tests
// run on Qt's offscreen platform unless QT_QPA_PLATFORM says otherwise, so they need no display and no OpenGL:
// neither when CTest runs them nor when CMake's test discovery starts the executable at build time.

#include <QtCore/QByteArray>
#include <QtWidgets/QApplication>
#include <catch2/catch_session.hpp>
#include <opencv2/core.hpp>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#endif

namespace {

void report_crt_assertions_on_stderr()
{
#ifdef _MSC_VER
#ifdef _DEBUG
    for (const int report : {_CRT_ASSERT, _CRT_ERROR, _CRT_WARN}) {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
#endif
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {
        std::fputs("invalid parameter passed to a C runtime function\n", stderr);
        std::cout.flush();
        std::fflush(stdout);
        std::abort();
    });
#endif
}

void single_threaded_opencv_under_tsan()
{
#if defined(__SANITIZE_THREAD__)
    cv::setNumThreads(0);
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
    cv::setNumThreads(0);
#endif
#endif
}

}  // namespace

int main(int argc, char** argv)
{
    report_crt_assertions_on_stderr();
    single_threaded_opencv_under_tsan();
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    const QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("cloudscope-ui-tests"));
    return Catch::Session().run(argc, argv);
}
