#include "cloudscope/capture/frame.hpp"

#include <algorithm>
#include <mutex>
#include <new>
#include <vector>

namespace cloudscope {

namespace {

constexpr std::align_val_t kAlignment{64};

}  // namespace

std::string_view to_string(PixelFormat format)
{
    switch (format) {
    case PixelFormat::Gray8:
        return "GRAY8";
    case PixelFormat::Gray16:
        return "GRAY16";
    case PixelFormat::Bgr8:
        return "BGR8";
    case PixelFormat::Rgb8:
        return "RGB8";
    case PixelFormat::Yuyv:
        return "YUYV";
    case PixelFormat::Mjpeg:
        return "MJPEG";
    }
    return "UNKNOWN";
}

std::size_t bytes_per_pixel(PixelFormat format)
{
    switch (format) {
    case PixelFormat::Gray8:
        return 1;
    case PixelFormat::Gray16:
    case PixelFormat::Yuyv:
        return 2;
    case PixelFormat::Bgr8:
    case PixelFormat::Rgb8:
        return 3;
    case PixelFormat::Mjpeg:
        return 0;
    }
    return 0;
}

std::size_t frame_buffer_bytes(PixelFormat format, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t per_pixel = bytes_per_pixel(format);
    return pixels * (per_pixel == 0 ? 3 : per_pixel);
}

Frame::Frame(std::size_t capacity)
    : storage_(static_cast<std::byte*>(::operator new(std::max<std::size_t>(capacity, 1), kAlignment))),
      capacity_(capacity)
{
}

Frame::~Frame()
{
    ::operator delete(storage_, kAlignment);
}

void Frame::set_size(std::size_t bytes)
{
    size_ = std::min(bytes, capacity_);
}

struct FramePool::State {
    std::mutex mutex;
    std::vector<std::unique_ptr<Frame>> free;
    std::size_t frame_count = 0;
    std::size_t bytes_per_frame = 0;
};

FramePool::FramePool(std::size_t frame_count, std::size_t bytes_per_frame) : state_(std::make_shared<State>())
{
    state_->frame_count = frame_count;
    state_->bytes_per_frame = bytes_per_frame;
    state_->free.reserve(frame_count);
    for (std::size_t i = 0; i < frame_count; ++i) {
        state_->free.push_back(std::make_unique<Frame>(bytes_per_frame));
    }
}

std::shared_ptr<Frame> FramePool::acquire()
{
    std::unique_ptr<Frame> frame;
    {
        const std::lock_guard lock(state_->mutex);
        if (state_->free.empty()) {
            return nullptr;
        }
        frame = std::move(state_->free.back());
        state_->free.pop_back();
    }
    frame->info() = FrameInfo{};
    frame->set_size(0);
    // When the last reference goes, the buffer returns to the pool instead of being freed.
    // The deleter keeps the pool's state alive, so this is safe even after the FramePool object is gone.
    return {frame.release(), [state = state_](Frame* returned) {
                const std::lock_guard lock(state->mutex);
                state->free.push_back(std::unique_ptr<Frame>(returned));
            }};
}

std::size_t FramePool::frame_count() const
{
    return state_->frame_count;
}

std::size_t FramePool::free_count() const
{
    const std::lock_guard lock(state_->mutex);
    return state_->free.size();
}

std::size_t FramePool::bytes_per_frame() const
{
    return state_->bytes_per_frame;
}

}  // namespace cloudscope
