#include <cloudscope/common/event_bus.hpp>
#include <cloudscope/common/signal.hpp>

#include "test_support.hpp"

#include <QtCore/QObject>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace cloudscope;
using namespace std::chrono_literals;

TEST_CASE("a signal calls its callbacks in connection order with the emitted values", "[common][signal]")
{
    Signal<int, std::string> signal;
    std::vector<std::string> calls;
    signal.connect(
        [&calls](int number, const std::string& text) { calls.push_back("a:" + std::to_string(number) + text); });
    signal.connect(
        [&calls](int number, const std::string& text) { calls.push_back("b:" + std::to_string(number) + text); });

    signal.emit(1, "x");
    signal.emit(2, "y");
    CHECK(calls == std::vector<std::string>{"a:1x", "b:1x", "a:2y", "b:2y"});
    CHECK(signal.connection_count() == 2);
}

TEST_CASE("emitting a signal without callbacks does nothing", "[common][signal]")
{
    const Signal<int> signal;
    signal.emit(1);
    CHECK(signal.connection_count() == 0);
    const Signal<> no_arguments;
    no_arguments.emit();
}

TEST_CASE("a disconnected callback is not called again", "[common][signal]")
{
    Signal<int> signal;
    int first = 0;
    int second = 0;
    Connection connection = signal.connect([&first](int value) { first += value; });
    signal.connect([&second](int value) { second += value; });

    signal.emit(1);
    CHECK(connection.connected());
    connection.disconnect();
    CHECK_FALSE(connection.connected());
    signal.emit(10);
    connection.disconnect();  // disconnecting twice is harmless

    CHECK(first == 1);
    CHECK(second == 11);
    CHECK(signal.connection_count() == 1);
}

TEST_CASE("a default-constructed connection is not connected and can be disconnected", "[common][signal]")
{
    Connection connection;
    CHECK_FALSE(connection.connected());
    connection.disconnect();
}

TEST_CASE("a scoped connection disconnects when it goes out of scope", "[common][signal]")
{
    Signal<> signal;
    int calls = 0;
    {
        const ScopedConnection scoped(signal.connect([&calls] { ++calls; }));
        signal.emit();
        CHECK(scoped.connected());
    }
    signal.emit();
    CHECK(calls == 1);

    ScopedConnection moved_to;
    {
        ScopedConnection original(signal.connect([&calls] { calls += 10; }));
        moved_to = std::move(original);
    }  // the moved-from object must not disconnect
    signal.emit();
    CHECK(calls == 11);
}

TEST_CASE("a callback may disconnect itself while it runs", "[common][signal]")
{
    Signal<> signal;
    int calls = 0;
    Connection connection;
    connection = signal.connect([&] {
        ++calls;
        connection.disconnect();  // must not wait for itself
    });
    signal.emit();
    signal.emit();
    CHECK(calls == 1);
}

TEST_CASE("a callback may connect another callback while the signal is being emitted", "[common][signal]")
{
    Signal<> signal;
    int late_calls = 0;
    bool connected_late = false;
    signal.connect([&] {
        if (!connected_late) {
            connected_late = true;
            signal.connect([&late_calls] { ++late_calls; });
        }
    });
    signal.emit();  // the new callback joins after this emission
    CHECK(late_calls == 0);
    signal.emit();
    CHECK(late_calls == 1);
}

TEST_CASE("an exception from a callback reaches the emitter and leaves the signal usable", "[common][signal]")
{
    Signal<> signal;
    int calls = 0;
    Connection throwing = signal.connect([] { throw std::runtime_error("callback failed"); });
    signal.connect([&calls] { ++calls; });

    CHECK_THROWS_AS(signal.emit(), std::runtime_error);
    throwing.disconnect();  // would hang if the failed call were still counted as running
    signal.emit();
    CHECK(calls == 1);
}

TEST_CASE("disconnect waits until a callback running on another thread has finished", "[common][signal][threads]")
{
    Signal<> signal;
    std::atomic<bool> entered{false};
    std::atomic<bool> finished{false};
    Connection connection = signal.connect([&] {
        entered = true;
        std::this_thread::sleep_for(150ms);
        finished = true;
    });

    std::thread emitter([&signal] { signal.emit(); });
    while (!entered) {
        std::this_thread::yield();
    }
    connection.disconnect();
    CHECK(finished);  // after disconnect() returns the callback is no longer running: safe to destroy its data
    emitter.join();
}

TEST_CASE("a callback connected with an executor runs there with copies of the arguments", "[common][signal][threads]")
{
    Signal<std::string> signal;
    ThreadPool worker("listener", 1);
    std::thread::id ran_on;
    std::string received;
    signal.connect(worker, [&](const std::string& text) {
        ran_on = std::this_thread::get_id();
        received = text;
    });

    {
        const std::string temporary = "frame 7 saved";
        signal.emit(temporary);
    }  // the original string is gone before the worker may have run
    worker.wait_idle();
    CHECK(received == "frame 7 saved");
    CHECK(ran_on != std::this_thread::get_id());
}

TEST_CASE("a queued call does not run after its connection was disconnected", "[common][signal][qt]")
{
    QObject context;
    QtExecutor ui(&context);
    Signal<int> signal;
    int calls = 0;
    Connection connection = signal.connect(ui, [&calls](int) { ++calls; });

    signal.emit(1);           // queued for the event loop
    connection.disconnect();  // the listener goes away before the loop runs
    CHECK_FALSE(test::wait_until([&calls] { return calls > 0; }, 100ms));

    Connection kept = signal.connect(ui, [&calls](int value) { calls += value; });
    signal.emit(5);
    CHECK(test::wait_until([&calls] { return calls == 5; }));
    kept.disconnect();
}

TEST_CASE("a queued call survives the destruction of its signal", "[common][signal]")
{
    ThreadPool worker("late", 1);
    std::atomic<int> received{0};
    {
        Signal<int> signal;
        worker.post([] { std::this_thread::sleep_for(50ms); });  // keeps the worker busy while the signal dies
        signal.connect(worker, [&received](int value) { received = value; });
        signal.emit(9);
    }
    worker.wait_idle();
    CHECK(received == 9);
}

TEST_CASE("connecting, emitting and disconnecting from several threads is safe", "[common][signal][threads]")
{
    Signal<int> signal;
    std::atomic<long long> total{0};
    std::atomic<bool> stop{false};

    std::vector<std::thread> threads;
    threads.reserve(6);
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&] {
            while (!stop) {
                signal.emit(1);
            }
        });
    }
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 300; ++i) {
                Connection connection = signal.connect([&total](int value) { total += value; });
                std::this_thread::yield();
                connection.disconnect();
            }
        });
    }
    for (std::size_t i = 3; i < threads.size(); ++i) {
        threads[i].join();
    }
    stop = true;
    for (std::size_t i = 0; i < 3; ++i) {
        threads[i].join();
    }
    CHECK(signal.connection_count() == 0);
    CHECK(total >= 0);  // the point of this test is that it finishes cleanly, also under the thread sanitizer
}

// ------------------------------------------------------------------------------------- event bus

namespace {

struct CameraDisconnected {
    std::string camera_id;
};

struct FrameSaved {
    int sequence = 0;
};

}  // namespace

TEST_CASE("the event bus delivers each event to the subscribers of its type only", "[common][event_bus]")
{
    EventBus bus;
    std::vector<std::string> disconnected;
    std::vector<int> saved;
    bus.subscribe<CameraDisconnected>(
        [&](const CameraDisconnected& event) { disconnected.push_back(event.camera_id); });
    bus.subscribe<FrameSaved>([&](const FrameSaved& event) { saved.push_back(event.sequence); });
    bus.subscribe<FrameSaved>([&](const FrameSaved& event) { saved.push_back(-event.sequence); });

    bus.publish(FrameSaved{.sequence = 1});
    bus.publish(CameraDisconnected{.camera_id = "B0268"});
    bus.publish(FrameSaved{.sequence = 2});

    CHECK(disconnected == std::vector<std::string>{"B0268"});
    CHECK(saved == std::vector<int>{1, -1, 2, -2});
    CHECK(bus.subscriber_count<FrameSaved>() == 2);
    CHECK(bus.subscriber_count<CameraDisconnected>() == 1);
}

TEST_CASE("publishing an event nobody listens to is harmless", "[common][event_bus]")
{
    EventBus bus;
    bus.publish(FrameSaved{.sequence = 1});
    CHECK(bus.subscriber_count<FrameSaved>() == 0);
}

TEST_CASE("an unsubscribed handler receives no more events", "[common][event_bus]")
{
    EventBus bus;
    int calls = 0;
    Connection connection = bus.subscribe<FrameSaved>([&calls](const FrameSaved&) { ++calls; });
    bus.publish(FrameSaved{});
    connection.disconnect();
    bus.publish(FrameSaved{});
    CHECK(calls == 1);
    CHECK(bus.subscriber_count<FrameSaved>() == 0);
}

TEST_CASE("events published on a worker thread reach a handler on the Qt thread", "[common][event_bus][qt]")
{
    EventBus bus;
    QObject context;
    QtExecutor ui(&context);
    std::vector<int> received;
    std::thread::id handled_on;
    const ScopedConnection connection(bus.subscribe<FrameSaved>(ui, [&](const FrameSaved& event) {
        received.push_back(event.sequence);
        handled_on = std::this_thread::get_id();
    }));

    std::thread publisher([&bus] {
        for (int i = 1; i <= 5; ++i) {
            bus.publish(FrameSaved{.sequence = i});
        }
    });
    publisher.join();
    CHECK(received.empty());  // queued until this thread's event loop runs
    CHECK(test::wait_until([&received] { return received.size() == 5; }));
    CHECK(received == std::vector<int>{1, 2, 3, 4, 5});
    CHECK(handled_on == std::this_thread::get_id());
}
