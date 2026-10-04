// A bounded queue between exactly one producer thread and one consumer thread, without locks.
//
// The producer never waits: try_push() returns false when the queue is full, and the caller decides what a
// full queue means (for camera frames: count a drop). This is the hand-over used between the acquisition
// thread and each frame consumer (architecture 4.1).
//
// Rules: only one thread may call try_push(), only one thread may call try_pop(). size() may be called from
// any thread and is exact only when both ends are quiet.
#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace cloudscope {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)  // "structure was padded due to alignment specifier": that is the intent
#endif

template <class T>
class SpscQueue {
public:
    // Holds up to `capacity` items (at least 1).
    explicit SpscQueue(std::size_t capacity) : slots_(capacity < 1 ? 2 : capacity + 1) {}

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;
    ~SpscQueue() = default;

    // Producer thread. Returns false when the queue is full; `value` is then left untouched (not moved from),
    // so the caller still has it.
    [[nodiscard]] bool try_push(T&& value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t following = next(tail);
        if (following == head_.load(std::memory_order_acquire)) {
            return false;
        }
        slots_[tail].emplace(std::move(value));
        tail_.store(following, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool try_push(const T& value) { return try_push(T(value)); }

    // Consumer thread. Empty optional when there is nothing to take.
    [[nodiscard]] std::optional<T> try_pop()
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        // Every slot between head and tail holds a value. Taking the value itself (not the optional around it)
        // also keeps GCC 15 from reporting a "maybe uninitialized" read in optimised builds.
        std::optional<T>& slot = slots_[head];
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access,bugprone-optional-value-conversion): see above
        std::optional<T> value(std::move(*slot));
        slot.reset();
        head_.store(next(head), std::memory_order_release);
        return value;
    }

    [[nodiscard]] std::size_t capacity() const { return slots_.size() - 1; }

    [[nodiscard]] std::size_t size() const
    {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return tail >= head ? tail - head : tail + slots_.size() - head;
    }

    [[nodiscard]] bool empty() const { return size() == 0; }

private:
    // 64 bytes: the cache line size of x86-64 and of the Cortex-A76 in the Raspberry Pi 5.
    static constexpr std::size_t kCacheLine = 64;

    [[nodiscard]] std::size_t next(std::size_t index) const { return index + 1 == slots_.size() ? 0 : index + 1; }

    std::vector<std::optional<T>> slots_;  // one slot stays empty to tell "full" from "empty"
    // Each index on its own cache line, so the two threads do not slow each other down.
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};  // next slot to read; written by the consumer
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};  // next slot to write; written by the producer
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}  // namespace cloudscope
