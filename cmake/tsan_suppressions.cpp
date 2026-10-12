// Default suppressions for the thread sanitizer. This file is compiled into every executable of a build with
// CLOUDSCOPE_SANITIZE=thread (see CloudScopeCompilerOptions.cmake); the sanitizer calls the function at start.
//
// Keep this list short and explain every entry: a suppression hides races.

extern "C" const char* __tsan_default_suppressions();

extern "C" const char* __tsan_default_suppressions()
{
    // OpenCV's worker threads come from Intel TBB (libtbb), which is not built with the sanitizer. The
    // sanitizer cannot see TBB's own synchronisation and reports its internal memory management as data
    // races (seen in P016: both stacks of every report were inside libtbb.so). Calls made from inside libtbb
    // are therefore ignored. CloudScope code is still checked, also when it runs on those threads.
    //
    // Qt (Core, Gui) is not built with the sanitizer either, and its mutexes are futex-based, which the sanitizer
    // does not intercept. Two kinds of false positive appeared with the desktop application (P045):
    //   - Qt's own worker threads (the image-scaling pool behind QPainter::drawImage with smooth scaling, started
    //     inside libQt6Gui) allocate and free container memory; the reports were malloc/realloc inside
    //     libQt6Core on those threads against allocations on the UI thread. Calls from inside the two libraries
    //     are ignored like TBB's.
    //   - A task handed to the UI thread through Qt's event queue (QtExecutor::post, which is
    //     QMetaObject::invokeMethod with a functor and a queued connection): the functor is built on the posting
    //     thread and called on the UI thread, ordered by the event queue's own lock. The reports name the
    //     functor wrapper of Qt's headers (QtPrivate::QCallableObject / FunctorCall), which is inlined into
    //     CloudScope's binaries, hence a race suppression rather than a library one. Everything the task touches
    //     on the UI thread stays checked.
    return "called_from_lib:libtbb.so\n"
           "called_from_lib:libQt6Core.so\n"
           "called_from_lib:libQt6Gui.so\n"
           "race:*QtPrivate::QCallableObject*\n"
           "race:*QtPrivate::FunctorCall*\n";
}
