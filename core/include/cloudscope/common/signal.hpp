// Typed signals: a list of callbacks that are called when something happens.
//
//   Signal<int, std::string> progress;
//   Connection connection = progress.connect([](int percent, const std::string& step) { ... });
//   progress.emit(40, "encoding");
//   connection.disconnect();
//
// Thread rules:
//   - connect(), disconnect() and emit() may be called from any thread.
//   - A callback connected without an executor runs on the thread that calls emit().
//   - A callback connected with an executor runs wherever that executor runs tasks; the arguments are copied.
//   - When disconnect() returns, the callback is not running and will not run again. The one exception is a
//     callback that disconnects itself: that is allowed and does not wait.
//     This is what makes "disconnect in the destructor" safe.
//   - A Connection does not disconnect when it is destroyed; a ScopedConnection does.
//   - A callback must not throw. If one does on the emitting thread, the exception reaches the caller of emit()
//     and the callbacks after it are not called for that emission; on an executor, the executor deals with it
//     (a ThreadPool logs it).
#pragma once

#include "cloudscope/common/executor.hpp"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace cloudscope {

namespace detail {

// What a Connection refers to: one callback slot, independent of the signal's argument types.
class SlotState {
public:
    // Runs `call` unless the slot has been disconnected. Blocks disconnect() from other threads meanwhile.
    void run(const std::function<void()>& call);
    // Marks the slot as disconnected and waits until no other thread is inside run().
    void disconnect();
    [[nodiscard]] bool connected() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable finished_;
    bool connected_ = true;
    std::vector<std::thread::id> runners_;  // one entry per call in progress: the thread that makes it
};

}  // namespace detail

class Connection {
public:
    Connection() = default;
    explicit Connection(std::shared_ptr<detail::SlotState> state) : state_(std::move(state)) {}

    void disconnect();
    [[nodiscard]] bool connected() const;

private:
    std::shared_ptr<detail::SlotState> state_;
};

// Disconnects when it goes out of scope.
class ScopedConnection {
public:
    ScopedConnection() = default;
    explicit ScopedConnection(Connection connection) : connection_(std::move(connection)) {}
    ~ScopedConnection() { connection_.disconnect(); }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;
    ScopedConnection(ScopedConnection&& other) noexcept : connection_(std::exchange(other.connection_, {})) {}
    ScopedConnection& operator=(ScopedConnection&& other) noexcept
    {
        if (this != &other) {
            connection_.disconnect();
            connection_ = std::exchange(other.connection_, {});
        }
        return *this;
    }

    void disconnect() { connection_.disconnect(); }
    [[nodiscard]] bool connected() const { return connection_.connected(); }

private:
    Connection connection_;
};

template <class... Args>
class Signal {
public:
    using Callback = std::function<void(const Args&...)>;

    Signal() = default;
    ~Signal() = default;
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;

    // The callback runs on the emitting thread.
    Connection connect(Callback callback) { return add(nullptr, std::move(callback)); }

    // The callback runs as a task of `executor`, which must outlive the connection.
    Connection connect(IExecutor& executor, Callback callback) { return add(&executor, std::move(callback)); }

    // Calls every connected callback, in the order they were connected.
    void emit(const Args&... args) const
    {
        const std::shared_ptr<const Slots> slots = current_slots();
        for (const Slot& slot : *slots) {
            if (slot.executor == nullptr) {
                slot.state->run([&] { slot.callback(args...); });
            } else {
                // Copies of state, callback and arguments travel with the task: the signal may be gone by then.
                slot.executor->post(
                    [state = slot.state, callback = slot.callback, copy = std::tuple<Args...>(args...)] {
                        state->run([&] { std::apply(callback, copy); });
                    });
            }
        }
    }

    // Number of callbacks currently connected.
    [[nodiscard]] std::size_t connection_count() const
    {
        const std::shared_ptr<const Slots> slots = current_slots();
        std::size_t count = 0;
        for (const Slot& slot : *slots) {
            count += slot.state->connected() ? 1U : 0U;
        }
        return count;
    }

private:
    struct Slot {
        std::shared_ptr<detail::SlotState> state;
        IExecutor* executor = nullptr;
        Callback callback;
    };
    using Slots = std::vector<Slot>;

    Connection add(IExecutor* executor, Callback callback)
    {
        auto state = std::make_shared<detail::SlotState>();
        const std::lock_guard lock(mutex_);
        // A new list is built each time, so emit() can walk its snapshot without holding the lock.
        auto updated = std::make_shared<Slots>();
        updated->reserve(slots_->size() + 1);
        for (const Slot& slot : *slots_) {
            if (slot.state->connected()) {  // disconnected slots are dropped here
                updated->push_back(slot);
            }
        }
        updated->push_back({.state = state, .executor = executor, .callback = std::move(callback)});
        slots_ = std::move(updated);
        return Connection(std::move(state));
    }

    std::shared_ptr<const Slots> current_slots() const
    {
        const std::lock_guard lock(mutex_);
        return slots_;
    }

    mutable std::mutex mutex_;
    std::shared_ptr<const Slots> slots_ = std::make_shared<const Slots>();
};

}  // namespace cloudscope
