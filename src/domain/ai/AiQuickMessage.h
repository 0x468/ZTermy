#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ztermy::ai
{

struct AiQuickMessage final
{
    std::string id;
    std::string name;
    std::string slug;
    std::string content;
    std::string description;
    std::int64_t createdUtcMs = 0;
    std::int64_t modifiedUtcMs = 0;

    bool operator==(const AiQuickMessage &) const = default;
};

[[nodiscard]] bool validAiQuickMessageSlug(std::string_view slug) noexcept;
[[nodiscard]] std::string normalizeAiQuickMessageSlug(std::string_view value);
[[nodiscard]] bool validAiQuickMessage(const AiQuickMessage &message) noexcept;

} // namespace ztermy::ai
