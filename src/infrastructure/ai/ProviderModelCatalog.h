#pragma once

#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiProviderTypes.h"

#include <QByteArray>
#include <QNetworkRequest>
#include <QStringList>

#include <expected>

namespace ztermy::ai
{

class ProviderModelCatalog final
{
public:
    [[nodiscard]] static std::expected<QNetworkRequest, AiProviderError>
    prepareRequest(const AiProviderConfiguration &configuration, security::SensitiveByteArray apiKey);
    [[nodiscard]] static std::expected<QStringList, AiProviderError> parse(const AiProviderKind kind,
                                                                           const QByteArray &body);
};

} // namespace ztermy::ai
