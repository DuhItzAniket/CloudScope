// Error reporting without exceptions: a function that can fail in normal operation returns Expected<T>.
//
//   Expected<Frame> grab();                       // either a Frame or an Error
//   if (auto frame = grab()) use(*frame); else report(frame.error());
//   return fail(ErrorCode::NotFound, "no camera with id 'B0268'");
//
// Expected is tl::expected, which has the interface of C++23 std::expected.
#pragma once

#include <tl/expected.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace cloudscope {

// Coarse, stable categories. Code branches on the category; people read the message.
enum class ErrorCode : std::uint8_t {
    InvalidArgument,  // the caller passed something unusable
    NotFound,         // a file, device or key does not exist
    AlreadyExists,
    PermissionDenied,
    Unavailable,  // exists but cannot be used now: device busy or unplugged, service down
    Timeout,
    Io,           // reading, writing or transport failed
    Parse,        // text or binary data is malformed
    Validation,   // well-formed, but breaks a schema, a limit or a contract
    Unsupported,  // this device, file format or build cannot do it
    Cancelled,
    Internal,  // a bug: something that should always hold did not
};

[[nodiscard]] std::string_view to_string(ErrorCode code);

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;

    // Prefixes the message with where or during what the error happened: "config.toml: <message>".
    [[nodiscard]] Error with_context(std::string_view context) const;

    // "Parse: config.toml: line 3: ..." for logs and command-line output.
    [[nodiscard]] std::string to_string() const;

    friend bool operator==(const Error&, const Error&) = default;
};

template <class T>
using Expected = tl::expected<T, Error>;

using Unexpected = tl::unexpected<Error>;

// The error return value of a function returning Expected<T>.
[[nodiscard]] inline Unexpected fail(ErrorCode code, std::string message)
{
    return Unexpected(Error{.code = code, .message = std::move(message)});
}

[[nodiscard]] inline Unexpected fail(Error error)
{
    return Unexpected(std::move(error));
}

}  // namespace cloudscope
