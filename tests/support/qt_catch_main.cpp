// Entry point for test executables: a QCoreApplication exists while the Catch2 tests run,
// so tests can use Qt classes that need one (QProcess, timers, event loops).

#include <QtCore/QCoreApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
    const QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
