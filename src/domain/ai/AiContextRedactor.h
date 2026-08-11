#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class AiSecretKind : std::uint8_t
{
    privateKey,
    authorizationHeader,
    uriCredential,
    apiKey,
    shellAssignment,
    userRule,
    count,
};

enum class AiRedactionRuleType : std::uint8_t
{
    literal,
    regularExpression,
};

struct AiUserRedactionRule final
{
    std::string id;
    AiRedactionRuleType type = AiRedactionRuleType::literal;
    std::string pattern;
    bool caseSensitive = true;
    bool enabled = true;
};

struct AiRedactionResult final
{
    std::string text;
    std::array<std::size_t, static_cast<std::size_t>(AiSecretKind::count)> counts{};
    std::vector<std::string> invalidRuleIds;

    [[nodiscard]] std::size_t totalRedactions() const noexcept;
};

class AiContextRedactor final
{
public:
    [[nodiscard]] AiRedactionResult redact(std::string_view text,
                                           std::span<const AiUserRedactionRule> userRules = {}) const;
};

} // namespace ztermy::ai
