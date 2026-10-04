#include <cloudscope/common/executor.hpp>
#include <cloudscope/common/log.hpp>

#include "test_support.hpp"

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace cloudscope;
using namespace std::chrono_literals;

TEST_CASE("the inline executor runs the task at once on the calling thread", "[common][executor]")
{
    InlineExecutor executor;
    std::thread::id ran_on;
    executor.post([&ran_on] { ran_on = std::this_thread::get_id(); });
    CHECK(ran_on == std::this_thread::get_id());
}

TEST_CASE("a single-thread pool runs tasks one after the other in posting order", "[common][executor]")
{
    std::vector<int> order;
    std::thread::id worker;
    {
        ThreadPool pool("serial", 1);
        CHECK(pool.thread_count() == 1);
        for (int i = 0; i < 200; ++i) {
            pool.post([&order, &worker, i] {
                order.push_back(i);  // no lock needed: one worker, so tasks never overlap
                worker = std::this_thread::get_id();
            });
        }
        pool.wait_idle();
        CHECK(pool.pending() == 0);
    }
    REQUIRE(order.size() == 200);
    for (int i = 0; i < 200; ++i) {
        CHECK(order[static_cast<std::size_t>(i)] == i);
    }
    CHECK(worker != std::this_thread::get_id());
}

TEST_CASE("a pool with several threads really runs tasks in parallel", "[common][executor]")
{
    constexpr int kThreads = 4;
    ThreadPool pool("parallel", kThreads);
    std::mutex mutex;
    std::set<std::thread::id> workers;
    std::atomic<int> inside{0};
    std::atomic<int> most_at_once{0};

    for (int i = 0; i < kThreads * 4; ++i) {
        pool.post([&] {
            const int now = ++inside;
            int seen = most_at_once.load();
            while (now > seen && !most_at_once.compare_exchange_weak(seen, now)) {}
            std::this_thread::sleep_for(20ms);
            {
                const std::lock_guard lock(mutex);
                workers.insert(std::this_thread::get_id());
            }
            --inside;
        });
    }
    pool.wait_idle();
    CHECK(most_at_once > 1);
    CHECK(most_at_once <= kThreads);
    CHECK(workers.size() > 1);
    CHECK(workers.size() <= kThreads);
}

TEST_CASE("submit returns the result or the exception through a future", "[common][executor]")
{
    ThreadPool pool("submit", 2);
    std::future<int> answer = pool.submit([] { return 6 * 7; });
    std::future<std::string> text = pool.submit([] { return std::string("done"); });
    std::future<void> nothing = pool.submit([] {});
    std::future<int> broken = pool.submit([]() -> int { throw std::runtime_error("no camera"); });

    CHECK(answer.get() == 42);
    CHECK(text.get() == "done");
    nothing.get();
    CHECK_THROWS_AS(broken.get(), std::runtime_error);
}

TEST_CASE("a task that throws does not stop its worker and the failure is logged", "[common][executor]")
{
    LoggingConfig config;
    config.console = false;
    REQUIRE(init_logging(config).has_value());

    ThreadPool pool("sturdy", 1);
    std::atomic<int> completed{0};
    pool.post([] { throw std::runtime_error("sensor exploded"); });
    pool.post([] { throw 42; });
    pool.post([&completed] { ++completed; });
    pool.wait_idle();
    CHECK(completed == 1);  // the worker survived both exceptions

    int logged = 0;
    for (const LogRecord& record : recent_log_records()) {
        if (record.component == "threads" && record.level == LogLevel::Error) {
            ++logged;
            CHECK(record.message.find("sturdy-0") != std::string::npos);
        }
    }
    CHECK(logged == 2);
    shutdown_logging();
}

TEST_CASE("destroying a pool finishes the queued tasks first", "[common][executor]")
{
    std::atomic<int> completed{0};
    {
        ThreadPool pool("drain", 2);
        for (int i = 0; i < 50; ++i) {
            pool.post([&completed] {
                std::this_thread::sleep_for(1ms);
                ++completed;
            });
        }
    }  // no wait_idle(): the destructor must not throw queued work away
    CHECK(completed == 50);
}

TEST_CASE("wait_idle returns only when queued and running tasks are done", "[common][executor]")
{
    ThreadPool pool("idle", 2);
    std::atomic<bool> finished{false};
    pool.post([&finished] {
        std::this_thread::sleep_for(100ms);
        finished = true;
    });
    pool.wait_idle();
    CHECK(finished);
    pool.wait_idle();  // an idle pool returns at once
}

TEST_CASE("a pool asked for zero threads still has one", "[common][executor]")
{
    ThreadPool pool("minimal", 0);
    CHECK(pool.thread_count() == 1);
    CHECK(pool.submit([] { return 1; }).get() == 1);
}

TEST_CASE("the Qt executor runs tasks on the thread of its object through the event loop", "[common][executor][qt]")
{
    QObject context;
    QtExecutor executor(&context);
    std::thread::id ran_on;
    std::atomic<bool> ran{false};

    std::thread other([&] {
        executor.post([&] {
            ran_on = std::this_thread::get_id();
            ran = true;
        });
    });
    other.join();
    CHECK_FALSE(ran);  // queued: nothing runs until this thread's event loop does

    CHECK(test::wait_until([&ran] { return ran.load(); }));
    CHECK(ran_on == std::this_thread::get_id());
}

TEST_CASE("the Qt executor drops tasks once its object is gone", "[common][executor][qt]")
{
    auto context = std::make_unique<QObject>();
    QtExecutor executor(context.get());
    bool ran = false;
    executor.post([&ran] { ran = true; });  // queued for the object ...
    context.reset();                        // ... which dies before the event loop runs
    executor.post([&ran] { ran = true; });  // posted after its death
    CHECK_FALSE(test::wait_until([&ran] { return ran; }, 100ms));
}

TEST_CASE("worker threads carry the pool's name", "[common][executor]")
{
#ifdef __linux__
    ThreadPool pool("frame-recorder-pool", 1);
    const std::string name = pool.submit([] {
                                     std::array<char, 32> buffer{};
                                     pthread_getname_np(pthread_self(), buffer.data(), buffer.size());
                                     return std::string(buffer.data());
                                 })
                                 .get();
    CHECK(name == "frame-recorder-");  // Linux keeps 15 characters
#else
    ThreadPool pool("frame-recorder-pool", 1);
    CHECK(pool.submit([] { return true; }).get());  // naming must at least not disturb the thread
#endif
}
