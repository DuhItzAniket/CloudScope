#include "cloudscope/capture/frame_hub.hpp"

#include <algorithm>
#include <utility>

namespace cloudscope {

FrameSubscription::FrameSubscription(std::string_view name, Delivery delivery, std::size_t queue_capacity)
    : name_(name), delivery_(delivery), queue_(delivery == Delivery::Queue ? queue_capacity : 1)
{
}

void FrameSubscription::offer(const FramePtr& frame)
{
    offered_.fetch_add(1, std::memory_order_relaxed);
    if (delivery_ == Delivery::Queue) {
        if (queue_.try_push(frame)) {
            ready_.release();
        } else {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    FramePtr replaced;
    {
        const std::lock_guard lock(latest_mutex_);
        replaced = std::exchange(latest_, frame);
    }
    if (replaced) {
        skipped_.fetch_add(1, std::memory_order_relaxed);  // the permit of the replaced frame stays valid
    } else {
        ready_.release();
    }
}

FramePtr FrameSubscription::try_take()
{
    return ready_.try_acquire() ? take_ready() : nullptr;
}

FramePtr FrameSubscription::wait(std::chrono::milliseconds timeout)
{
    // A timed semaphore wait may return a little before its time (seen on Windows: 48 ms of 50 ms).
    // The loop makes "nothing within the timeout" mean the whole timeout.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!ready_.try_acquire_until(deadline)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return nullptr;
        }
    }
    return take_ready();
}

FramePtr FrameSubscription::take_ready()
{
    if (delivery_ == Delivery::Queue) {
        std::optional<FramePtr> frame = queue_.try_pop();
        return frame ? std::move(*frame) : nullptr;
    }
    const std::lock_guard lock(latest_mutex_);
    return std::exchange(latest_, nullptr);
}

std::shared_ptr<FrameSubscription> FrameHub::subscribe(std::string_view name, Delivery delivery,
                                                       std::size_t queue_capacity)
{
    auto subscription = std::make_shared<FrameSubscription>(name, delivery, queue_capacity);
    const std::lock_guard lock(mutex_);
    // A new list each time, so publish() can walk its snapshot without holding the lock; ended subscriptions
    // are left out.
    auto updated = std::make_shared<Subscribers>();
    for (const std::weak_ptr<FrameSubscription>& existing : *subscribers_) {
        if (!existing.expired()) {
            updated->push_back(existing);
        }
    }
    updated->push_back(subscription);
    subscribers_ = std::move(updated);
    return subscription;
}

void FrameHub::publish(const FramePtr& frame)
{
    std::shared_ptr<const Subscribers> subscribers;
    {
        const std::lock_guard lock(mutex_);
        subscribers = subscribers_;
    }
    published_.fetch_add(1, std::memory_order_relaxed);
    for (const std::weak_ptr<FrameSubscription>& weak : *subscribers) {
        if (const std::shared_ptr<FrameSubscription> subscription = weak.lock()) {
            subscription->offer(frame);
        }
    }
}

std::size_t FrameHub::subscriber_count() const
{
    const std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::ranges::count_if(
        *subscribers_, [](const std::weak_ptr<FrameSubscription>& weak) { return !weak.expired(); }));
}

}  // namespace cloudscope
