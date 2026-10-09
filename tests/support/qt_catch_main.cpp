// Entry point for test executables: a QCoreApplication exists while the Catch2 tests run,
// so tests can use Qt classes that need one (QProcess, timers, event loops).

#include <QtCore/QCoreApplication>
#include <catch2/catch_session.hpp>
#include <opencv2/core.hpp>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#endif

namespace {

// A Debug build of the Microsoft C runtime reports a failed assertion (an out-of-range vector index, an invalid
// distribution parameter) with a message box and waits for a click. Under ctest nobody clicks, and the test
// times out without a word. Report to stderr and abort instead, so the failure says what it is.
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
        std::cout.flush();  // Catch2's report so far, which the abort would otherwise lose
        std::fflush(stdout);
        std::abort();
    });
#endif
}

// OpenCV's parallel loops run on its own worker pool (Intel TBB on Debian), which is not built with the thread
// sanitizer: the sanitizer cannot see the pool's synchronisation and reports the workers' disjoint writes as races
// with the thread that reads the result. Under the sanitizer OpenCV therefore runs single-threaded; CloudScope's
// own threads stay fully checked.
static void single_threaded_opencv_under_tsan()
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
    const QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
