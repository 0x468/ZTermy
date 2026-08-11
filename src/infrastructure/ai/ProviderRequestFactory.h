#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <QByteArray>
#include <QNetworkRequest>

#include <expected>
#include <string_view>

namespace ztermy::ai
{

enum class AiWireProtocol : std::uint8_t
{
    serverSentEvents,
    ndjson,
};

struct PreparedProviderRequest final
{
    QNetworkRequest request;
    QByteArray body;
    AiWireProtocol protocol = AiWireProtocol::serverSentEvents;
};

class ProviderRequestFactory final
{
public:
    [[nodiscard]] static std::expected<PreparedProviderRequest, AiProviderError>
    prepare(const AiProviderConfiguration &configuration, const AiGenerationRequest &generation,
            std::string_view apiKey);
};

} // namespace ztermy::ai
