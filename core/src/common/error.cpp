#include "cloudscope/common/error.hpp"

namespace cloudscope {

std::string_view to_string(ErrorCode code)
{
    switch (code) {
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::AlreadyExists:
        return "AlreadyExists";
    case ErrorCode::PermissionDenied:
        return "PermissionDenied";
    case ErrorCode::Unavailable:
        return "Unavailable";
    case ErrorCode::Timeout:
        return "Timeout";
    case ErrorCode::Io:
        return "Io";
    case ErrorCode::Parse:
        return "Parse";
    case ErrorCode::Validation:
        return "Validation";
    case ErrorCode::Unsupported:
        return "Unsupported";
    case ErrorCode::Cancelled:
        return "Cancelled";
    case ErrorCode::Internal:
        return "Internal";
    }
    return "Unknown";
}

Error Error::with_context(std::string_view context) const
{
    std::string text;
    text.reserve(context.size() + 2 + message.size());
    text.append(context).append(": ").append(message);
    return Error{.code = code, .message = std::move(text)};
}

std::string Error::to_string() const
{
    std::string text(cloudscope::to_string(code));
    text.append(": ").append(message);
    return text;
}

}  // namespace cloudscope
