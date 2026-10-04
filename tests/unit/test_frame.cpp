#include <cloudscope/capture/frame.hpp>
#include <cloudscope/capture/frame_hub.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace cloudscope;
using namespace std::chrono_literals;

namespace {

FramePtr make_frame(FramePool& pool, std::uint64_t sequence)
{
    std::shared_ptr<Frame> frame = pool.acquire();
    REQUIRE(frame != nullptr);
    frame->info().sequence = sequence;
    frame->info().width = 4;
    frame->info().height = 2;
    frame->info().format = PixelFormat::Gray8;
    frame->info().stride = 4;
    std::memset(frame->buffer().data(), static_cast<int>(sequence & 0xFFU), 8);
    frame->set_size(8);
    return frame;
}

}  // namespace

TEST_CASE("pixel formats report their names and sizes", "[capture][frame]")
{
    CHECK(to_string(PixelFormat::Gray8) == "GRAY8");
    CHECK(to_string(PixelFormat::Mjpeg) == "MJPEG");
    CHECK(bytes_per_pixel(PixelFormat::Gray8) == 1);
    CHECK(bytes_per_pixel(PixelFormat::Gray16) == 2);
    CHECK(bytes_per_pixel(PixelFormat::Yuyv) == 2);
    CHECK(bytes_per_pixel(PixelFormat::Bgr8) == 3);
    CHECK(bytes_per_pixel(PixelFormat::Rgb8) == 3);
    CHECK(bytes_per_pixel(PixelFormat::Mjpeg) == 0);

    CHECK(frame_buffer_bytes(PixelFormat::Bgr8, 3840, 2160) == 24883200U);
    CHECK(frame_buffer_bytes(PixelFormat::Gray16, 4656, 3496) == 32554752U);
    CHECK(frame_buffer_bytes(PixelFormat::Mjpeg, 4656, 3496) == 48832128U);  // upper bound for a compressed frame
    CHECK(frame_buffer_bytes(PixelFormat::Bgr8, 0, 100) == 0);
    CHECK(frame_buffer_bytes(PixelFormat::Bgr8, -5, 100) == 0);
}

TEST_CASE("a frame's data is the part of its buffer that was filled, on an aligned address", "[capture][frame]")
{
    Frame frame(1000);
    CHECK(frame.capacity() == 1000);
    CHECK(frame.buffer().size() == 1000);
    CHECK(frame.data().empty());
    CHECK(reinterpret_cast<std::uintptr_t>(frame.buffer().data()) % 64 == 0);

    frame.buffer()[0] = std::byte{0xAB};
    frame.set_size(640);
    CHECK(frame.data().size() == 640);
    CHECK(frame.data()[0] == std::byte{0xAB});
    frame.set_size(5000);  // more than the buffer holds
    CHECK(frame.data().size() == 1000);
}

TEST_CASE("the pool lends its buffers and gets them back when the last reference goes", "[capture][frame_pool]")
{
    FramePool pool(2, 64);
    CHECK(pool.frame_count() == 2);
    CHECK(pool.bytes_per_frame() == 64);
    CHECK(pool.free_count() == 2);

    std::shared_ptr<Frame> first = pool.acquire();
    std::shared_ptr<Frame> second = pool.acquire();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->capacity() == 64);
    CHECK(pool.free_count() == 0);
    CHECK(pool.acquire() == nullptr);  // exhausted: the producer counts a drop, it does not wait

    FramePtr shared = first;  // a consumer holds the frame too
    first.reset();
    CHECK(pool.free_count() == 0);  // still in use
    const Frame* address = shared.get();
    shared.reset();
    CHECK(pool.free_count() == 1);

    const std::shared_ptr<Frame> reused = pool.acquire();
    CHECK(reused.get() == address);  // the same buffer again: no new allocation
}

TEST_CASE("a reused buffer starts with clean metadata", "[capture][frame_pool]")
{
    FramePool pool(1, 16);
    {
        const std::shared_ptr<Frame> frame = pool.acquire();
        frame->info().sequence = 99;
        frame->info().simulated = true;
        frame->set_size(16);
    }
    const std::shared_ptr<Frame> again = pool.acquire();
    CHECK(again->info().sequence == 0);
    CHECK_FALSE(again->info().simulated);
    CHECK(again->data().empty());
}

TEST_CASE("frames may outlive their pool", "[capture][frame_pool]")
{
    FramePtr survivor;
    {
        FramePool pool(1, 32);
        survivor = make_frame(pool, 5);
    }
    REQUIRE(survivor != nullptr);
    CHECK(survivor->info().sequence == 5);
    CHECK(survivor->data().size() == 8);
    survivor.reset();  // returns to the pool's state, which is then released: must not crash or leak
}

TEST_CASE("a queue subscriber receives every frame in order", "[capture][frame_hub]")
{
    FramePool pool(8, 16);
    FrameHub hub;
    const auto recorder = hub.subscribe("recorder", Delivery::Queue, 4);
    CHECK(recorder->name() == "recorder");
    CHECK(hub.subscriber_count() == 1);
    CHECK(recorder->try_take() == nullptr);

    for (std::uint64_t i = 0; i < 3; ++i) {
        hub.publish(make_frame(pool, i));
    }
    CHECK(hub.published() == 3);
    for (std::uint64_t i = 0; i < 3; ++i) {
        const FramePtr frame = recorder->try_take();
        REQUIRE(frame != nullptr);
        CHECK(frame->info().sequence == i);
    }
    CHECK(recorder->try_take() == nullptr);
    CHECK(recorder->offered() == 3);
    CHECK(recorder->dropped() == 0);
}

TEST_CASE("a queue subscriber that falls behind drops new frames and counts them", "[capture][frame_hub]")
{
    FramePool pool(8, 16);
    FrameHub hub;
    const auto slow = hub.subscribe("slow", Delivery::Queue, 2);
    for (std::uint64_t i = 0; i < 5; ++i) {
        hub.publish(make_frame(pool, i));
    }
    CHECK(slow->offered() == 5);
    CHECK(slow->dropped() == 3);
    // The frames that did fit are the oldest ones, in order: a recording has a gap, not a reordering.
    CHECK(slow->try_take()->info().sequence == 0);
    CHECK(slow->try_take()->info().sequence == 1);
    CHECK(slow->try_take() == nullptr);
    CHECK(pool.free_count() == 8);  // dropped frames went straight back to the pool
}

TEST_CASE("a latest subscriber sees only the newest frame", "[capture][frame_hub]")
{
    FramePool pool(8, 16);
    FrameHub hub;
    const auto preview = hub.subscribe("preview", Delivery::Latest);
    for (std::uint64_t i = 0; i < 5; ++i) {
        hub.publish(make_frame(pool, i));
    }
    CHECK(preview->skipped() == 4);
    CHECK(preview->dropped() == 0);
    const FramePtr frame = preview->try_take();
    REQUIRE(frame != nullptr);
    CHECK(frame->info().sequence == 4);
    CHECK(preview->try_take() == nullptr);  // one frame, taken once
    CHECK(pool.free_count() == 7);          // only the frame in our hand is still out

    hub.publish(make_frame(pool, 5));
    CHECK(preview->try_take()->info().sequence == 5);
}

TEST_CASE("each subscriber is served independently and frames are shared, not copied", "[capture][frame_hub]")
{
    FramePool pool(4, 16);
    FrameHub hub;
    const auto recorder = hub.subscribe("recorder", Delivery::Queue, 4);
    const auto preview = hub.subscribe("preview", Delivery::Latest);

    hub.publish(make_frame(pool, 0));
    hub.publish(make_frame(pool, 1));

    const FramePtr recorded = recorder->try_take();
    const FramePtr shown = preview->try_take();
    REQUIRE(recorded != nullptr);
    REQUIRE(shown != nullptr);
    CHECK(recorded->info().sequence == 0);
    CHECK(shown->info().sequence == 1);
    const FramePtr recorded_next = recorder->try_take();
    CHECK(recorded_next.get() == shown.get());  // the very same frame object
}

TEST_CASE("a subscription ends when its handle is released", "[capture][frame_hub]")
{
    FramePool pool(4, 16);
    FrameHub hub;
    auto first = hub.subscribe("first", Delivery::Queue, 4);
    const auto second = hub.subscribe("second", Delivery::Queue, 4);
    CHECK(hub.subscriber_count() == 2);

    first.reset();
    CHECK(hub.subscriber_count() == 1);
    hub.publish(make_frame(pool, 0));  // must not touch the released subscription
    CHECK(second->offered() == 1);
    CHECK(second->try_take() != nullptr);
    CHECK(pool.free_count() == 4);

    hub.publish(make_frame(pool, 1));  // with no subscriber left for it, a frame is simply released
    CHECK(hub.published() == 2);
}

TEST_CASE("wait returns a frame as soon as one is published, or nothing after the timeout",
          "[capture][frame_hub][threads]")
{
    FramePool pool(4, 16);
    FrameHub hub;
    const auto consumer = hub.subscribe("consumer", Delivery::Queue, 4);

    const auto start = std::chrono::steady_clock::now();
    CHECK(consumer->wait(50ms) == nullptr);
    CHECK(std::chrono::steady_clock::now() - start >= 50ms);

    std::thread producer([&] {
        std::this_thread::sleep_for(30ms);
        hub.publish(make_frame(pool, 7));
    });
    const FramePtr frame = consumer->wait(2000ms);
    producer.join();
    REQUIRE(frame != nullptr);
    CHECK(frame->info().sequence == 7);
}

TEST_CASE("a producer and two consumer threads exchange frames without loss or corruption",
          "[capture][frame_hub][threads]")
{
    constexpr std::uint64_t kFrames = 20000;
    FramePool pool(64, 16);
    FrameHub hub;
    const auto lossless = hub.subscribe("lossless", Delivery::Queue, 32);
    const auto latest = hub.subscribe("latest", Delivery::Latest);
    std::atomic<bool> done{false};
    std::atomic<bool> corrupted{false};

    const auto intact = [](const Frame& frame) {
        const auto expected = static_cast<std::byte>(frame.info().sequence & 0xFFU);
        for (const std::byte value : frame.data()) {
            if (value != expected) {
                return false;
            }
        }
        return frame.data().size() == 8;
    };

    std::uint64_t lossless_received = 0;
    std::thread queue_consumer([&] {
        std::uint64_t last = 0;
        bool first = true;
        while (true) {
            const FramePtr frame = lossless->wait(20ms);
            if (frame == nullptr) {
                if (done) {
                    break;
                }
                continue;
            }
            if (!intact(*frame) || (!first && frame->info().sequence <= last)) {
                corrupted = true;
            }
            last = frame->info().sequence;
            first = false;
            ++lossless_received;
        }
    });
    std::uint64_t latest_received = 0;
    std::thread latest_consumer([&] {
        std::uint64_t last = 0;
        bool first = true;
        while (true) {
            const FramePtr frame = latest->wait(20ms);
            if (frame == nullptr) {
                if (done) {
                    break;
                }
                continue;
            }
            if (!intact(*frame) || (!first && frame->info().sequence <= last)) {
                corrupted = true;
            }
            last = frame->info().sequence;
            first = false;
            ++latest_received;
        }
    });

    std::uint64_t pool_misses = 0;
    for (std::uint64_t i = 0; i < kFrames; ++i) {
        std::shared_ptr<Frame> frame = pool.acquire();
        if (frame == nullptr) {
            ++pool_misses;  // every buffer is with a consumer: this frame is dropped at the source
            std::this_thread::yield();
            continue;
        }
        frame->info().sequence = i;
        std::memset(frame->buffer().data(), static_cast<int>(i & 0xFFU), 8);
        frame->set_size(8);
        hub.publish(frame);
    }
    done = true;
    queue_consumer.join();
    latest_consumer.join();

    CHECK_FALSE(corrupted);
    const std::uint64_t published = hub.published();
    CHECK(published + pool_misses == kFrames);
    // Every published frame is accounted for by each consumer: received, dropped or skipped.
    CHECK(lossless_received + lossless->dropped() == published);
    CHECK(latest_received + latest->skipped() == published);
    CHECK(latest_received >= 1);
    CHECK(pool.free_count() == 64);
}
