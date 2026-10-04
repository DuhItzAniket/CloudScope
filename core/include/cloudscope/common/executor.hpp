// Where a piece of work runs. Code that produces events does not decide on which thread its listeners run:
// the listener names an executor when it subscribes (signal.hpp, event_bus.hpp).
#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cloudscope {

using Task = std::function<void()>;

class IExecutor {
public:
    IExecutor() = default;
    virtual ~IExecutor() = default;
    IExecutor(const IExecutor&) = delete;
    IExecutor& operator=(const IExecutor&) = delete;
    IExecutor(IExecutor&&) = delete;
    IExecutor& operator=(IExecutor&&) = delete;

    // Hands over a task. Never blocks on the task itself. Thread-safe.
    virtual void post(Task task) = 0;
};

// Runs the task at once, on the calling thread.
class InlineExecutor final : public IExecutor {
public:
    void post(Task task) override;
};

// Runs tasks on the thread of a QObject, through its event loop: the way into the UI thread.
// Tasks posted after the object was destroyed are dropped.
class QtExecutor final : public IExecutor {
public:
    explicit QtExecutor(QObject* context);
    void post(Task task) override;

private:
    QPointer<QObject> context_;
};

// A fixed set of worker threads with one first-in, first-out task queue.
// With one thread it is a serial executor: tasks run one after the other, in the order they were posted.
//
// A task that throws does not end its worker: the exception is logged (logger "threads") and the worker goes on.
// The destructor finishes all queued tasks, then joins the threads.
class ThreadPool final : public IExecutor {
public:
    // `name` shows up in logs and in debuggers as "<name>-0", "<name>-1", ...
    ThreadPool(std::string_view name, std::size_t thread_count);
    ~ThreadPool() override;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    void post(Task task) override;

    // Like post(), but the result (or the exception) of the function comes back through a future.
    template <class Function>
    [[nodiscard]] auto submit(Function function) -> std::future<std::invoke_result_t<Function>>
    {
        using Result = std::invoke_result_t<Function>;
        auto work = std::make_shared<std::packaged_task<Result()>>(std::move(function));
        std::future<Result> result = work->get_future();
        post([work] { (*work)(); });
        return result;
    }

    // Blocks until the queue is empty and no task is running. Must not be called from a task of this pool.
    void wait_idle();

    [[nodiscard]] std::size_t thread_count() const { return threads_.size(); }
    [[nodiscard]] std::size_t pending() const;

private:
    void work(std::size_t index);

    std::string name_;
    mutable std::mutex mutex_;
    std::condition_variable work_available_;
    std::condition_variable idle_;
    std::deque<Task> queue_;
    std::size_t running_ = 0;
    bool stopping_ = false;
    std::vector<std::thread> threads_;
};

// Names the calling thread for debuggers and system tools (15 characters are kept on Linux).
void set_current_thread_name(std::string_view name);

}  // namespace cloudscope
