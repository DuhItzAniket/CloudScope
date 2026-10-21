#include "cloudscope/ai/settings.hpp"

#include <fmt/format.h>

namespace cloudscope::ai {

namespace {

std::filesystem::path path_from_utf8(const std::string& text)
{
    const std::u8string u8(text.begin(), text.end());
    return std::filesystem::path(u8);
}

}  // namespace

Expected<AiSettings> ai_settings(const nlohmann::json& effective)
{
    if (!effective.is_object() || !effective.contains("ai")) {
        return fail(ErrorCode::InvalidArgument, "the configuration has no [ai] section");
    }
    AiSettings settings;
    try {
        const nlohmann::json& ai = effective.at("ai");
        settings.enabled = ai.at("enabled").get<bool>();
        const std::string dir = ai.at("model_dir").get<std::string>();
        settings.model_dir = dir.empty() ? std::filesystem::path() : path_from_utf8(dir);
        settings.provider = ai.at("provider").get<std::string>();
        settings.rate_hz = ai.at("rate_hz").get<double>();
        settings.threads = ai.at("threads").get<int>();
    } catch (const nlohmann::json::exception& error) {
        // The schema guarantees the keys; reaching this means the schema and this reader disagree.
        return fail(ErrorCode::Internal, fmt::format("ai settings: {}", error.what()));
    }
    return settings;
}

}  // namespace cloudscope::ai
