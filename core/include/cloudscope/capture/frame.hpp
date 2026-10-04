// A camera frame: the pixel data and what is known about it.
//
// A frame is written once by its producer (a camera driver or a simulator) and then shared, read-only, by
// every consumer through FramePtr. Nobody copies pixel data to hand a frame on.
#pragma once

#include "cloudscope/common/clock.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace cloudscope {

enum class PixelFormat : std::uint8_t {
    Gray8,   // 1 byte per pixel
    Gray16,  // 2 bytes per pixel, byte order of the host
    Bgr8,    // 3 bytes per pixel, blue first (the order OpenCV uses)
    Rgb8,    // 3 bytes per pixel, red first
    Yuyv,    // YUV 4:2:2, packed Y0 U Y1 V: 2 bytes per pixel
    Mjpeg,   // one JPEG image; the size varies from frame to frame
};

[[nodiscard]] std::string_view to_string(PixelFormat format);

// Bytes per pixel of an uncompressed format; 0 for a compressed one.
[[nodiscard]] std::size_t bytes_per_pixel(PixelFormat format);

// Bytes needed for one frame: exact for uncompressed formats. For compressed formats this is an upper bound
// to size buffers with (as many bytes as the uncompressed 3-byte image).
[[nodiscard]] std::size_t frame_buffer_bytes(PixelFormat format, int width, int height);

struct FrameInfo {
    std::uint64_t sequence = 0;  // position in the stream, counted by the source from 0; a gap means lost frames
    Timestamp captured;          // host clocks at the moment the frame arrived
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Gray8;
    std::size_t stride = 0;  // bytes from the start of one row to the next; 0 for compressed formats
    bool simulated = false;  // true for frames from a simulator or generator (NFR-DATA-03)
};

class Frame {
public:
    // Reserves room for `capacity` bytes of pixel data, aligned for vector instructions.
    explicit Frame(std::size_t capacity);
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&) = delete;
    Frame& operator=(Frame&&) = delete;

    [[nodiscard]] const FrameInfo& info() const { return info_; }
    [[nodiscard]] FrameInfo& info() { return info_; }  // producer side

    // Producer side: the whole buffer, and how much of it the frame occupies.
    [[nodiscard]] std::span<std::byte> buffer() { return {storage_, capacity_}; }
    void set_size(std::size_t bytes);  // clamped to the capacity
    [[nodiscard]] std::size_t capacity() const { return capacity_; }

    // Consumer side: exactly the frame's data (height * stride bytes, or the compressed size).
    [[nodiscard]] std::span<const std::byte> data() const { return {storage_, size_}; }

private:
    FrameInfo info_;
    std::byte* storage_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t size_ = 0;
};

using FramePtr = std::shared_ptr<const Frame>;

// A fixed set of frame buffers, allocated once and reused: the acquisition thread never allocates image
// memory while streaming. When every buffer is still held by a consumer, acquire() returns nullptr and the
// producer counts a dropped frame; it never waits (architecture 4.2: no back-pressure on acquisition).
//
// Thread-safe. Frames may outlive the pool object; the memory is released when the last one is gone.
class FramePool {
public:
    FramePool(std::size_t frame_count, std::size_t bytes_per_frame);

    // A writable frame for the producer to fill, or nullptr if none is free.
    [[nodiscard]] std::shared_ptr<Frame> acquire();

    [[nodiscard]] std::size_t frame_count() const;
    [[nodiscard]] std::size_t free_count() const;
    [[nodiscard]] std::size_t bytes_per_frame() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace cloudscope
