// HAL: byte transports to a controller: USB serial, TCP, UDP (FR-CTL-03). CSDP frames travel over these.
//
// A transport moves bytes; it knows nothing about framing or messages.
//
// Threads: one thread may write while another reads. close() may be called from a third thread; a read() that
// is waiting then returns at once.
#pragma once

#include "cloudscope/hal/device.hpp"

#include <chrono>
#include <cstddef>
#include <span>

namespace cloudscope::hal {

class ITransport : public IDevice {
public:
    // Sends all the bytes or fails. Io on a broken link.
    [[nodiscard]] virtual Expected<void> write(std::span<const std::byte> data) = 0;

    // Waits up to `timeout` for data and returns how many bytes were put into `buffer`: at least one if any
    // arrived, 0 on timeout. Bytes arrive in the order they were sent, in pieces of any size.
    // Io on a broken link.
    [[nodiscard]] virtual Expected<std::size_t> read(std::span<std::byte> buffer,
                                                     std::chrono::milliseconds timeout) = 0;
};

}  // namespace cloudscope::hal
