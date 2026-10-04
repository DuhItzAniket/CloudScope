#include "cloudscope/common/executor.hpp"

#include "cloudscope/common/log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <exception>

#ifdef _WIN32
#include <qt_windows.h>
#else
#include <pthread.h>
#endif

namespace cloudscope {

void InlineExecutor::post(Task task)
{
    task();
}

QtExecutor::QtExecutor(QObject* context) : context_(context) {}

void QtExecutor::post(Task task)
{
    QObject* context = context_.data();
    if (context == nullptr) {
        return;
    }
    QMetaObject::invokeMethod(context, std::move(task), Qt::QueuedConnection);
}

ThreadPool::ThreadPool(std::string_view name, std::size_t thread_count) : name_(name)
{
    const std::size_t count = std::max<std::size_t>(thread_count, 1);
    threads_.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        threads_.emplace_back([this, index] { work(index); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    work_available_.notify_all();
    for (std::thread& thread : threads_) {
        thread.join();
    }
}

void ThreadPool::post(Task task)
{
    {
        const std::lock_guard lock(mutex_);
        queue_.push_back(std::move(task));
    }
    work_available_.notify_one();
}

void ThreadPool::wait_idle()
{
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && running_ == 0; });
}

std::size_t ThreadPool::pending() const
{
    const std::lock_guard lock(mutex_);
    return queue_.size();
}

void ThreadPool::work(std::size_t index)
{
    set_current_thread_name(fmt::format("{}-{}", name_, index));
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            work_available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping, and everything queued has been done
            }
            task = std::move(queue_.front());
            queue_.pop_front();
            ++running_;
        }
        try {
            task();
        } catch (const std::exception& error) {
            logger("threads").error("task on {}-{} failed: {}", name_, index, error.what());
        } catch (...) {
            logger("threads").error("task on {}-{} failed with an unknown exception", name_, index);
        }
        task = nullptr;  // release what the task captured before reporting idle
        {
            const std::lock_guard lock(mutex_);
            --running_;
            if (queue_.empty() && running_ == 0) {
                idle_.notify_all();
            }
        }
    }
}

void set_current_thread_name(std::string_view name)
{
#ifdef _WIN32
    const std::wstring wide = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())).toStdWString();
    SetThreadDescription(GetCurrentThread(), wide.c_str());
#else
    constexpr std::size_t kLinuxLimit = 15;
    const std::string shortened(name.substr(0, kLinuxLimit));
    pthread_setname_np(pthread_self(), shortened.c_str());
#endif
}

}  // namespace cloudscope
