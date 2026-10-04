#include <cloudscope/common/spsc_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using cloudscope::SpscQueue;

TEST_CASE("the queue hands items over in order and reports full and empty", "[common][spsc_queue]")
{
    SpscQueue<int> queue(3);
    CHECK(queue.capacity() == 3);
    CHECK(queue.empty());
    CHECK_FALSE(queue.try_pop().has_value());

    CHECK(queue.try_push(1));
    CHECK(queue.try_push(2));
    CHECK(queue.try_push(3));
    CHECK(queue.size() == 3);
    CHECK_FALSE(queue.try_push(4));  // full: the producer is told, nothing is overwritten

    CHECK(queue.try_pop() == 1);
    CHECK(queue.try_push(4));  // room again after one item was taken
    CHECK(queue.try_pop() == 2);
    CHECK(queue.try_pop() == 3);
    CHECK(queue.try_pop() == 4);
    CHECK(queue.empty());
}

TEST_CASE("a rejected push leaves the item with the caller", "[common][spsc_queue]")
{
    SpscQueue<std::unique_ptr<std::string>> queue(1);
    auto first = std::make_unique<std::string>("first");
    auto second = std::make_unique<std::string>("second");

    CHECK(queue.try_push(std::move(first)));
    CHECK_FALSE(queue.try_push(std::move(second)));
    // The point of this test: a push that is refused has not moved from its argument, so the caller can
    // count a drop or try again. NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    REQUIRE(second != nullptr);
    CHECK(*second == "second");  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

    const std::unique_ptr<std::string> taken = queue.try_pop().value_or(nullptr);
    REQUIRE(taken != nullptr);
    CHECK(*taken == "first");
}

TEST_CASE("the indices wrap around many times without losing items", "[common][spsc_queue]")
{
    SpscQueue<int> queue(2);
    for (int i = 0; i < 1000; ++i) {
        REQUIRE(queue.try_push(i));
        REQUIRE(queue.try_pop() == i);
    }
    CHECK(queue.empty());
}

TEST_CASE("items left in the queue are destroyed with it", "[common][spsc_queue]")
{
    const auto tracker = std::make_shared<int>(7);
    {
        SpscQueue<std::shared_ptr<int>> queue(4);
        REQUIRE(queue.try_push(tracker));
        CHECK(tracker.use_count() == 2);
    }
    CHECK(tracker.use_count() == 1);
}

TEST_CASE("a capacity of zero is treated as one", "[common][spsc_queue]")
{
    SpscQueue<int> queue(0);
    CHECK(queue.capacity() == 1);
    CHECK(queue.try_push(5));
    CHECK_FALSE(queue.try_push(6));
    CHECK(queue.try_pop() == 5);
}

TEST_CASE("one producer and one consumer exchange a million items in order", "[common][spsc_queue][threads]")
{
    constexpr std::uint64_t kCount = 1'000'000;
    SpscQueue<std::uint64_t> queue(1024);
    std::atomic<bool> failed{false};
    std::uint64_t sum = 0;

    std::thread consumer([&] {
        std::uint64_t expected = 0;
        while (expected < kCount) {
            if (const auto value = queue.try_pop()) {
                if (*value != expected) {
                    failed = true;  // out of order, duplicated or lost
                    return;
                }
                sum += *value;
                ++expected;
            } else {
                std::this_thread::yield();
            }
        }
    });
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount;) {
            if (queue.try_push(i)) {
                ++i;
            } else {
                std::this_thread::yield();
            }
            if (failed) {
                return;
            }
        }
    });
    producer.join();
    consumer.join();

    CHECK_FALSE(failed);
    CHECK(sum == kCount * (kCount - 1) / 2);
    CHECK(queue.empty());
}
