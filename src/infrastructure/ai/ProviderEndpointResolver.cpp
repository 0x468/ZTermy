#include "infrastructure/ai/ProviderEndpointResolver.h"

#include <QString>

#include <algorithm>
#include <array>
#include <string_view>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] QString fromUtf8(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString generationLeaf(const AiProviderKind kind)
{
    switch (kind)
    {
        case AiProviderKind::openAiResponses:
            return QStringLiteral("responses");
        case AiProviderKind::anthropicMessages:
            return QStringLiteral("messages");
        case AiProviderKind::ollama:
            return QStringLiteral("chat");
        case AiProviderKind::openAiCompatible:
            return QStringLiteral("chat/completions");
    }
    return {};
}

[[nodiscard]] QString fullDefaultPath(const AiProviderKind kind, const ProviderEndpointPurpose purpose)
{
    if (purpose == ProviderEndpointPurpose::models)
    {
        return kind == AiProviderKind::ollama ? QStringLiteral("/api/tags") : QStringLiteral("/v1/models");
    }
    switch (kind)
    {
        case AiProviderKind::openAiResponses:
            return QStringLiteral("/v1/responses");
        case AiProviderKind::anthropicMessages:
            return QStringLiteral("/v1/messages");
        case AiProviderKind::ollama:
            return QStringLiteral("/api/chat");
        case AiProviderKind::openAiCompatible:
            return QStringLiteral("/v1/chat/completions");
    }
    return {};
}

[[nodiscard]] bool hasVersionSuffix(const QString &path)
{
    constexpr std::array suffixes{std::string_view{"/v1"}, std::string_view{"/v2"}, std::string_view{"/v3"},
                                  std::string_view{"/v4"}};
    return std::ranges::any_of(suffixes, [&path](const std::string_view suffix) {
        return path.endsWith(QString::fromLatin1(suffix.data(), static_cast<qsizetype>(suffix.size())),
                             Qt::CaseInsensitive);
    });
}

[[nodiscard]] std::expected<QUrl, AiProviderError> invalidUrlError()
{
    return std::unexpected(
        AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                        .message = "Provider API address must be an HTTP(S) URL without credentials.",
                        .retryable = false});
}

} // namespace

std::expected<QUrl, AiProviderError> resolveProviderEndpoint(const AiProviderConfiguration &configuration,
                                                             const ProviderEndpointPurpose purpose)
{
    auto rawBase = fromUtf8(configuration.baseUrl).trimmed();
    const bool exactEndpoint = rawBase.endsWith(QLatin1Char('#'));
    const bool explicitPrefix = rawBase.endsWith(QLatin1Char('/'));
    if (exactEndpoint)
    {
        rawBase.chop(1);
        if (purpose == ProviderEndpointPurpose::models)
        {
            return std::unexpected(
                AiProviderError{.code = AiProviderErrorCode::invalidRequest,
                                .message = "Model discovery is unavailable for an exact API endpoint.",
                                .retryable = false});
        }
    }

    auto base = QUrl(rawBase);
    if (!base.isValid() || base.host().isEmpty() || !base.userInfo().isEmpty()
        || (base.scheme() != QStringLiteral("http") && base.scheme() != QStringLiteral("https")))
    {
        return invalidUrlError();
    }
    if (exactEndpoint)
    {
        base.setFragment(QString{});
        return base;
    }

    auto basePath = base.path();
    while (basePath.size() > 1 && basePath.endsWith(QLatin1Char('/')))
    {
        basePath.chop(1);
    }

    if (purpose == ProviderEndpointPurpose::generation && !configuration.endpointPath.empty())
    {
        auto legacyEndpoint = fromUtf8(configuration.endpointPath);
        if (!legacyEndpoint.startsWith(QLatin1Char('/')))
        {
            legacyEndpoint.prepend(QLatin1Char('/'));
        }
        base.setPath((basePath == QStringLiteral("/") ? QString{} : basePath) + legacyEndpoint);
    }
    else if (configuration.kind != AiProviderKind::ollama && (explicitPrefix || hasVersionSuffix(basePath)))
    {
        const auto leaf =
            purpose == ProviderEndpointPurpose::models ? QStringLiteral("models") : generationLeaf(configuration.kind);
        base.setPath((basePath == QStringLiteral("/") ? QString{} : basePath) + QLatin1Char('/') + leaf);
    }
    else
    {
        base.setPath((basePath == QStringLiteral("/") ? QString{} : basePath)
                     + fullDefaultPath(configuration.kind, purpose));
    }
    base.setQuery(QString{});
    base.setFragment(QString{});
    return base;
}

} // namespace ztermy::ai
