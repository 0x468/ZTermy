#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <QUrl>

#include <expected>

namespace ztermy::ai
{

enum class ProviderEndpointPurpose : std::uint8_t
{
    generation,
    models,
};

[[nodiscard]] std::expected<QUrl, AiProviderError> resolveProviderEndpoint(const AiProviderConfiguration &configuration,
                                                                           ProviderEndpointPurpose purpose);

} // namespace ztermy::ai
