#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class AiProviderReplayError : std::uint8_t
{
    invalidData,
    limitExceeded,
};

struct AiProviderReplay final
{
    std::vector<AiToolExchange> toolHistory;
    std::string finalAssistantContentJson;

    [[nodiscard]] friend bool operator==(const AiProviderReplay &, const AiProviderReplay &) = default;
};

class AiProviderReplayCodec final
{
public:
    static constexpr std::size_t maximumBytes = std::size_t{256} * 1024;

    [[nodiscard]] static std::expected<std::string, AiProviderReplayError>
    encode(std::span<const AiToolExchange> toolHistory, std::string_view finalAssistantContentJson);
    [[nodiscard]] static std::expected<AiProviderReplay, AiProviderReplayError> decode(std::string_view json);
};

} // namespace ztermy::ai
