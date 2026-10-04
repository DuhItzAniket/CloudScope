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
    return "called_from_lib:libtbb.so\n";
}
