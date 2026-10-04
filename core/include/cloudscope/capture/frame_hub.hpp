// The hand-over point between the one thread that produces frames and the threads that use them
// (architecture 3 and 4.1: "frame ring buffer").
//
//   FrameHub hub;
//   auto recorder = hub.subscribe("recorder", Delivery::Queue, 8);   // every frame, in order
//   auto preview  = hub.subscribe("preview", Delivery::Latest);      // only the newest frame
//
//   hub.publish(frame);                              // producer thread: never blocks, never allocates pixels
//   FramePtr next = recorder->wait(100ms);           // consumer thread
//
// Each consumer chooses how it wants to be served:
//   Queue   every frame in order, through a lock-free queue. If the consumer falls behind and its queue is
//           full, the new frame is not queued for it and its dropped() count goes up.
//   Latest  only the most recent frame; an older frame that was not taken yet is replaced, and skipped()
//           goes up. For displays, statistics and inference, which must never lag behind the camera.
//
// A slow consumer therefore only ever affects itself: never the producer, never another consumer.
//
// Threads: publish() from one thread at a time (the producer). Each subscription is used by one consumer
// thread. subscribe() and dropping a subscription may happen on any thread at any time.
#pragma once

#include "cloudscope/capture/frame.hpp"
#include "cloudscope/common/spsc_queue.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope {

enum class Delivery : std::uint8_t { Queue, Latest };

class FrameSubscription {
public:
    FrameSubscription(std::string_view name, Delivery delivery, std::size_t queue_capacity);

    // The next frame, or nullptr if none arrives within `timeout`.
    [[nodiscard]] FramePtr wait(std::chrono::milliseconds timeout);
    // The next frame if one is ready, without waiting.
    [[nodiscard]] FramePtr try_take();

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] Delivery delivery() const { return delivery_; }
    [[nodiscard]] std::uint64_t offered() const { return offered_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t skipped() const { return skipped_.load(std::memory_order_relaxed); }

private:
    friend class FrameHub;
    void offer(const FramePtr& frame);    // producer thread
    [[nodiscard]] FramePtr take_ready();  // consumer thread, after a permit of ready_ was acquired

    std::string name_;
    Delivery delivery_;
    SpscQueue<FramePtr> queue_;  // Queue delivery
    std::mutex latest_mutex_;    // Latest delivery: guards latest_
    FramePtr latest_;
    std::counting_semaphore<> ready_{0};  // one permit per frame waiting to be taken
    std::atomic<std::uint64_t> offered_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> skipped_{0};
};

class FrameHub {
public:
    FrameHub() = default;
    ~FrameHub() = default;
    FrameHub(const FrameHub&) = delete;
    FrameHub& operator=(const FrameHub&) = delete;
    FrameHub(FrameHub&&) = delete;
    FrameHub& operator=(FrameHub&&) = delete;

    // The subscription ends when the returned pointer is released. `queue_capacity` is used for Queue delivery.
    [[nodiscard]] std::shared_ptr<FrameSubscription> subscribe(std::string_view name, Delivery delivery,
                                                               std::size_t queue_capacity = 4);

    // Offers the frame to every current subscriber. Producer thread only.
    void publish(const FramePtr& frame);

    [[nodiscard]] std::uint64_t published() const { return published_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::size_t subscriber_count() const;

private:
    using Subscribers = std::vector<std::weak_ptr<FrameSubscription>>;

    mutable std::mutex mutex_;  // guards subscribers_ (the pointer, not the frames)
    std::shared_ptr<const Subscribers> subscribers_ = std::make_shared<const Subscribers>();
    std::atomic<std::uint64_t> published_{0};
};

}  // namespace cloudscope
