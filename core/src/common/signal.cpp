#include "cloudscope/common/signal.hpp"

#include "cloudscope/common/scope_exit.hpp"

#include <algorithm>

namespace cloudscope {

namespace detail {

void SlotState::run(const std::function<void()>& call)
{
    const std::thread::id self = std::this_thread::get_id();
    {
        const std::lock_guard lock(mutex_);
        if (!connected_) {
            return;
        }
        runners_.push_back(self);
    }
    // The entry must go away even if the callback throws.
    const ScopeExit leave([this, self] {
        {
            const std::lock_guard lock(mutex_);
            runners_.erase(std::ranges::find(runners_, self));
        }
        finished_.notify_all();
    });
    call();
}

void SlotState::disconnect()
{
    const std::thread::id self = std::this_thread::get_id();
    std::unique_lock lock(mutex_);
    connected_ = false;
    // Wait for the callback to finish on every other thread. Calls made by this thread itself (a callback
    // that disconnects itself) cannot be waited for; they end when the caller returns.
    finished_.wait(lock, [this, self] {
        return std::ranges::all_of(runners_, [self](std::thread::id runner) { return runner == self; });
    });
}

bool SlotState::connected() const
{
    const std::lock_guard lock(mutex_);
    return connected_;
}

}  // namespace detail

void Connection::disconnect()
{
    if (state_) {
        state_->disconnect();
    }
}

bool Connection::connected() const
{
    return state_ && state_->connected();
}

}  // namespace cloudscope
