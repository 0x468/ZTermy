#include "infrastructure/ai/AiConversationStore.h"

#include "core/persistence/LastKnownGoodFile.h"
#include "core/security/SensitiveByteArray.h"
#include "domain/ai/AiProviderReplayCodec.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUrl>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <utility>

namespace ztermy::ai
{
namespace
{
constexpr int schemaVersion = 1;
constexpr int keySize = 32;
constexpr int nonceSize = 12;
constexpr int tagSize = 16;
constexpr qsizetype maximumEnvelopeBytes = qsizetype{8} * 1024 * 1024;
constexpr std::size_t maximumSourcesPerMessage = 24;
constexpr std::size_t maximumSourceUrlBytes = std::size_t{8} * 1024;
constexpr std::size_t maximumSourceTitleBytes = 1024;
constexpr std::size_t maximumSourceCitationBytes = std::size_t{4} * 1024;
constexpr std::size_t maximumToolActivitiesPerMessage = 32;
constexpr std::size_t maximumToolActivityIdBytes = 256;
constexpr std::size_t maximumToolActivityNameBytes = 128;
constexpr std::size_t maximumToolActivitySummaryBytes = 4096;
constexpr std::size_t maximumToolActivityArgumentsBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumToolActivityResultBytes = std::size_t{24} * 1024;
constexpr std::size_t maximumToolActivityStateBytes = 64;
constexpr std::size_t maximumToolActivityResultCodeBytes = 128;
constexpr std::size_t maximumCostCatalogDateBytes = 64;
constexpr std::string_view keyReference = "ai-conversation-history";

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

struct EncryptionResult final
{
    QByteArray ciphertext;
    QByteArray tag;
};

struct LoadedStore final
{
    std::vector<AiStoredConversation> conversations;
    std::uint64_t generation = 0;
};

[[nodiscard]] bool validSourceUrl(const std::string &url)
{
    if (url.empty() || url.size() > maximumSourceUrlBytes)
    {
        return false;
    }
    const QUrl value = QUrl::fromEncoded(QByteArray(url.data(), static_cast<qsizetype>(url.size())));
    return value.isValid() && (value.scheme() == QStringLiteral("https") || value.scheme() == QStringLiteral("http"));
}

[[nodiscard]] std::size_t sourceStorageBytes(const std::span<const AiWebSource> sources) noexcept
{
    std::size_t bytes = 0;
    for (const auto &source : sources)
    {
        bytes += source.url.size() + source.title.size() + source.citedText.size();
    }
    return bytes;
}

[[nodiscard]] std::size_t toolActivityStorageBytes(const std::span<const AiToolActivity> activities) noexcept
{
    std::size_t bytes = 0;
    for (const auto &activity : activities)
    {
        bytes += activity.id.size() + activity.name.size() + activity.summary.size() + activity.argumentsJson.size()
                 + activity.resultJson.size() + activity.state.size() + activity.resultCode.size();
    }
    return bytes;
}

[[nodiscard]] bool validToolActivity(const AiToolActivity &activity) noexcept
{
    return !activity.id.empty() && activity.id.size() <= maximumToolActivityIdBytes && !activity.name.empty()
           && activity.name.size() <= maximumToolActivityNameBytes
           && activity.summary.size() <= maximumToolActivitySummaryBytes
           && activity.argumentsJson.size() <= maximumToolActivityArgumentsBytes
           && activity.resultJson.size() <= maximumToolActivityResultBytes
           && activity.state.size() <= maximumToolActivityStateBytes
           && activity.resultCode.size() <= maximumToolActivityResultCodeBytes;
}

void clearBytes(QByteArray &bytes) noexcept
{
    if (!bytes.isEmpty())
    {
        OPENSSL_cleanse(bytes.data(), static_cast<std::size_t>(bytes.size()));
        bytes.clear();
        bytes.squeeze();
    }
}

[[nodiscard]] QByteArray additionalAuthenticatedData(const std::uint64_t generation)
{
    return QByteArrayLiteral("ztermy-ai-conversation-store|schema=1|generation=") + QByteArray::number(generation);
}

[[nodiscard]] persistence::PayloadValidation validateEnvelope(const QByteArrayView payload)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return persistence::PayloadValidation::invalid;
    }
    const auto object = document.object();
    if (!object.value(QStringLiteral("version")).isDouble())
    {
        return persistence::PayloadValidation::invalid;
    }
    if (object.value(QStringLiteral("version")).toInt() != schemaVersion)
    {
        return persistence::PayloadValidation::unsupportedVersion;
    }
    return object.value(QStringLiteral("generation")).isDouble() && object.value(QStringLiteral("nonce")).isString()
                   && object.value(QStringLiteral("ciphertext")).isString()
                   && object.value(QStringLiteral("tag")).isString()
               ? persistence::PayloadValidation::valid
               : persistence::PayloadValidation::invalid;
}

[[nodiscard]] AiConversationStoreError mapPersistenceError(const persistence::LastKnownGoodError error)
{
    switch (error)
    {
        case persistence::LastKnownGoodError::invalidPath:
            return AiConversationStoreError::invalidPath;
        case persistence::LastKnownGoodError::unsupportedVersion:
            return AiConversationStoreError::unsupportedVersion;
        case persistence::LastKnownGoodError::invalidFormat:
            return AiConversationStoreError::invalidData;
        case persistence::LastKnownGoodError::io:
        default:
            return AiConversationStoreError::ioError;
    }
}

[[nodiscard]] AiConversationStoreError mapVaultError(const security::CredentialVaultError error)
{
    switch (error)
    {
        case security::CredentialVaultError::Locked:
            return AiConversationStoreError::locked;
        case security::CredentialVaultError::NotFound:
            return AiConversationStoreError::keyMissing;
        case security::CredentialVaultError::UnsupportedVersion:
            return AiConversationStoreError::unsupportedVersion;
        case security::CredentialVaultError::AuthenticationFailed:
            return AiConversationStoreError::authenticationFailed;
        case security::CredentialVaultError::CryptoError:
            return AiConversationStoreError::cryptoError;
        case security::CredentialVaultError::IoError:
            return AiConversationStoreError::ioError;
        default:
            return AiConversationStoreError::unavailable;
    }
}

[[nodiscard]] security::CredentialKey conversationKey()
{
    return {.profileId = std::string(keyReference), .kind = security::CredentialKind::AiConversationKey};
}

[[nodiscard]] std::expected<security::SensitiveByteArray, AiConversationStoreError>
readOrCreateKey(security::CredentialVault &vault, const bool allowCreate)
{
    if (!vault.persistent())
    {
        return std::unexpected(AiConversationStoreError::unavailable);
    }
    auto existing = vault.read(conversationKey());
    if (existing)
    {
        if (existing->view().size() != keySize)
        {
            return std::unexpected(AiConversationStoreError::keyMissing);
        }
        return std::move(*existing);
    }
    if (existing.error() != security::CredentialVaultError::NotFound || !allowCreate)
    {
        return std::unexpected(mapVaultError(existing.error()));
    }

    QByteArray generated(keySize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(generated.data()), static_cast<int>(generated.size())) != 1)
    {
        clearBytes(generated);
        return std::unexpected(AiConversationStoreError::cryptoError);
    }
    security::SensitiveByteArray key(std::move(generated));
    QByteArray stored(key.view().data(), static_cast<qsizetype>(key.view().size()));
    if (auto result = vault.store(conversationKey(), security::SensitiveByteArray(std::move(stored))); !result)
    {
        return std::unexpected(mapVaultError(result.error()));
    }
    return key;
}

[[nodiscard]] std::expected<EncryptionResult, AiConversationStoreError> encrypt(const QByteArray &plaintext,
                                                                                const std::string_view key,
                                                                                const QByteArray &nonce,
                                                                                const std::uint64_t generation)
{
    if (key.size() != keySize || nonce.size() != nonceSize || plaintext.size() > std::numeric_limits<int>::max())
    {
        return std::unexpected(AiConversationStoreError::cryptoError);
    }
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    QByteArray tag(tagSize, Qt::Uninitialized);
    const QByteArray aad = additionalAuthenticatedData(generation);
    int aadLength = 0;
    int outputLength = 0;
    int finalLength = 0;
    const bool success =
        context != nullptr && EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1
        && EVP_EncryptInit_ex(context.get(), nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.data()),
                              reinterpret_cast<const unsigned char *>(nonce.constData()))
               == 1
        && EVP_EncryptUpdate(context.get(), nullptr, &aadLength,
                             reinterpret_cast<const unsigned char *>(aad.constData()), static_cast<int>(aad.size()))
               == 1
        && EVP_EncryptUpdate(context.get(), reinterpret_cast<unsigned char *>(ciphertext.data()), &outputLength,
                             reinterpret_cast<const unsigned char *>(plaintext.constData()),
                             static_cast<int>(plaintext.size()))
               == 1
        && EVP_EncryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(ciphertext.data()) + outputLength,
                               &finalLength)
               == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) == 1;
    if (!success)
    {
        clearBytes(ciphertext);
        clearBytes(tag);
        return std::unexpected(AiConversationStoreError::cryptoError);
    }
    ciphertext.resize(outputLength + finalLength);
    return EncryptionResult{.ciphertext = std::move(ciphertext), .tag = std::move(tag)};
}

[[nodiscard]] std::expected<QByteArray, AiConversationStoreError>
decrypt(const QByteArray &ciphertext, const QByteArray &tag, const std::string_view key, const QByteArray &nonce,
        const std::uint64_t generation)
{
    if (key.size() != keySize || nonce.size() != nonceSize || tag.size() != tagSize
        || ciphertext.size() > std::numeric_limits<int>::max())
    {
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    const QByteArray aad = additionalAuthenticatedData(generation);
    int aadLength = 0;
    int outputLength = 0;
    int finalLength = 0;
    const bool initialized =
        context != nullptr && EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1
        && EVP_DecryptInit_ex(context.get(), nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.data()),
                              reinterpret_cast<const unsigned char *>(nonce.constData()))
               == 1
        && EVP_DecryptUpdate(context.get(), nullptr, &aadLength,
                             reinterpret_cast<const unsigned char *>(aad.constData()), static_cast<int>(aad.size()))
               == 1
        && EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()), &outputLength,
                             reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                             static_cast<int>(ciphertext.size()))
               == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()),
                               const_cast<char *>(tag.constData()))
               == 1;
    if (!initialized
        || EVP_DecryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()) + outputLength,
                               &finalLength)
               != 1)
    {
        clearBytes(plaintext);
        return std::unexpected(AiConversationStoreError::authenticationFailed);
    }
    plaintext.resize(outputLength + finalLength);
    return plaintext;
}

[[nodiscard]] bool validConversation(const AiStoredConversation &conversation, const AiConversationStoreLimits &limits)
{
    if (conversation.id.trimmed().isEmpty() || conversation.id.size() > 128 || conversation.title.size() > 256
        || !conversation.updatedAtUtc.isValid() || conversation.messages.size() > limits.maximumMessagesPerConversation)
    {
        return false;
    }
    return std::ranges::all_of(conversation.messages, [&limits](const AiStoredMessage &message) {
        const bool assistant = message.role == QStringLiteral("assistant");
        const bool validRole =
            message.role == QStringLiteral("user") || assistant || message.role == QStringLiteral("evidence");
        const std::size_t storedBytes = static_cast<std::size_t>(message.text.toUtf8().size())
                                        + static_cast<std::size_t>(message.reasoning.toUtf8().size())
                                        + toolActivityStorageBytes(message.toolActivities)
                                        + sourceStorageBytes(message.sources) + message.providerReplayJson.size()
                                        + static_cast<std::size_t>(message.costCatalogDate.toUtf8().size());
        return validRole && std::cmp_less_equal(message.text.toUtf8().size(), limits.maximumMessageBytes)
               && (assistant
                   || (message.reasoning.isEmpty() && message.toolActivities.empty() && message.sources.empty()
                       && message.providerReplayJson.empty() && !message.usage.has_value()
                       && !message.metrics.has_value() && !message.estimatedCostUsd.has_value()
                       && message.costCatalogDate.isEmpty() && !message.longContextRates && !message.truncated))
               && (message.providerReplayJson.empty()
                   || AiProviderReplayCodec::decode(message.providerReplayJson).has_value())
               && message.toolActivities.size() <= maximumToolActivitiesPerMessage
               && std::ranges::all_of(message.toolActivities, validToolActivity)
               && message.sources.size() <= maximumSourcesPerMessage
               && std::ranges::all_of(message.sources,
                                      [](const AiWebSource &source) {
                                          return validSourceUrl(source.url)
                                                 && source.title.size() <= maximumSourceTitleBytes
                                                 && source.citedText.size() <= maximumSourceCitationBytes;
                                      })
               && std::cmp_less_equal(message.costCatalogDate.toUtf8().size(), maximumCostCatalogDateBytes)
               && (!message.estimatedCostUsd.has_value()
                   || (std::isfinite(*message.estimatedCostUsd) && *message.estimatedCostUsd >= 0.0))
               && (!message.usage.has_value()
                   || (message.usage->inputTokens <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
                       && message.usage->outputTokens <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
                       && message.usage->cachedInputTokens
                              <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
                       && message.usage->reasoningTokens
                              <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())))
               && (!message.metrics.has_value()
                   || (message.metrics->wallTimeMilliseconds
                           <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max())
                       && (!message.metrics->firstTokenMilliseconds.has_value()
                           || *message.metrics->firstTokenMilliseconds
                                  <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max()))))
               && storedBytes <= limits.maximumMessageBytes;
    });
}

[[nodiscard]] QByteArray serializePlaintext(const std::vector<AiStoredConversation> &conversations)
{
    QJsonArray values;
    for (const auto &conversation : conversations)
    {
        QJsonArray messages;
        for (const auto &message : conversation.messages)
        {
            QJsonArray sources;
            for (const auto &source : message.sources)
            {
                sources.push_back(QJsonObject{{QStringLiteral("url"), QString::fromUtf8(source.url)},
                                              {QStringLiteral("title"), QString::fromUtf8(source.title)},
                                              {QStringLiteral("citedText"), QString::fromUtf8(source.citedText)}});
            }
            QJsonObject value{{QStringLiteral("role"), message.role}, {QStringLiteral("text"), message.text}};
            if (!message.reasoning.isEmpty())
            {
                value.insert(QStringLiteral("reasoning"), message.reasoning);
            }
            if (!message.toolActivities.empty())
            {
                QJsonArray activities;
                for (const auto &activity : message.toolActivities)
                {
                    activities.push_back(
                        QJsonObject{{QStringLiteral("id"), QString::fromUtf8(activity.id)},
                                    {QStringLiteral("name"), QString::fromUtf8(activity.name)},
                                    {QStringLiteral("summary"), QString::fromUtf8(activity.summary)},
                                    {QStringLiteral("argumentsJson"), QString::fromUtf8(activity.argumentsJson)},
                                    {QStringLiteral("resultJson"), QString::fromUtf8(activity.resultJson)},
                                    {QStringLiteral("state"), QString::fromUtf8(activity.state)},
                                    {QStringLiteral("resultCode"), QString::fromUtf8(activity.resultCode)},
                                    {QStringLiteral("sideEffecting"), activity.sideEffecting},
                                    {QStringLiteral("highRisk"), activity.highRisk}});
                }
                value.insert(QStringLiteral("toolActivities"), activities);
            }
            if (!sources.isEmpty())
            {
                value.insert(QStringLiteral("sources"), sources);
            }
            if (!message.providerReplayJson.empty())
            {
                value.insert(
                    QStringLiteral("providerReplay"),
                    QJsonDocument::fromJson(QByteArray(message.providerReplayJson.data(),
                                                       static_cast<qsizetype>(message.providerReplayJson.size())))
                        .object());
            }
            if (message.usage.has_value())
            {
                const AiTokenUsage usage = message.usage.value_or(AiTokenUsage{});
                value.insert(
                    QStringLiteral("usage"),
                    QJsonObject{{QStringLiteral("inputTokens"), static_cast<qint64>(usage.inputTokens)},
                                {QStringLiteral("outputTokens"), static_cast<qint64>(usage.outputTokens)},
                                {QStringLiteral("cachedInputTokens"), static_cast<qint64>(usage.cachedInputTokens)},
                                {QStringLiteral("reasoningTokens"), static_cast<qint64>(usage.reasoningTokens)}});
            }
            if (message.metrics.has_value())
            {
                const AiTurnMetrics turnMetrics = message.metrics.value_or(AiTurnMetrics{});
                QJsonObject metrics{
                    {QStringLiteral("wallTimeMilliseconds"), static_cast<qint64>(turnMetrics.wallTimeMilliseconds)},
                    {QStringLiteral("retryCount"), static_cast<qint64>(turnMetrics.retryCount)}};
                if (turnMetrics.firstTokenMilliseconds.has_value())
                {
                    metrics.insert(QStringLiteral("firstTokenMilliseconds"),
                                   static_cast<qint64>(turnMetrics.firstTokenMilliseconds.value_or(0)));
                }
                value.insert(QStringLiteral("metrics"), metrics);
            }
            if (message.estimatedCostUsd.has_value())
            {
                value.insert(QStringLiteral("estimatedCostUsd"), *message.estimatedCostUsd);
            }
            if (!message.costCatalogDate.isEmpty())
            {
                value.insert(QStringLiteral("costCatalogDate"), message.costCatalogDate);
            }
            if (message.longContextRates)
            {
                value.insert(QStringLiteral("longContextRates"), true);
            }
            if (message.truncated)
            {
                value.insert(QStringLiteral("truncated"), true);
            }
            messages.push_back(value);
        }
        values.push_back(
            QJsonObject{{QStringLiteral("id"), conversation.id},
                        {QStringLiteral("title"), conversation.title},
                        {QStringLiteral("updatedAtUtc"), conversation.updatedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
                        {QStringLiteral("messages"), messages}});
    }
    return QJsonDocument(
               QJsonObject{{QStringLiteral("schemaVersion"), schemaVersion}, {QStringLiteral("conversations"), values}})
        .toJson(QJsonDocument::Compact);
}

[[nodiscard]] std::expected<std::vector<AiStoredConversation>, AiConversationStoreError>
parsePlaintext(const QByteArray &plaintext, const AiConversationStoreLimits &limits)
{
    if (std::cmp_greater(plaintext.size(), limits.maximumPlaintextBytes))
    {
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(plaintext, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != schemaVersion)
    {
        return std::unexpected(AiConversationStoreError::unsupportedVersion);
    }
    const auto array = root.value(QStringLiteral("conversations")).toArray();
    if (std::cmp_greater(array.size(), limits.maximumConversations))
    {
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    std::vector<AiStoredConversation> conversations;
    conversations.reserve(static_cast<std::size_t>(array.size()));
    for (const auto &value : array)
    {
        const auto object = value.toObject();
        AiStoredConversation conversation{
            .id = object.value(QStringLiteral("id")).toString(),
            .title = object.value(QStringLiteral("title")).toString(),
            .updatedAtUtc =
                QDateTime::fromString(object.value(QStringLiteral("updatedAtUtc")).toString(), Qt::ISODateWithMs)};
        const auto messages = object.value(QStringLiteral("messages")).toArray();
        conversation.messages.reserve(static_cast<std::size_t>(messages.size()));
        for (const auto &messageValue : messages)
        {
            const auto message = messageValue.toObject();
            AiStoredMessage stored{.role = message.value(QStringLiteral("role")).toString(),
                                   .text = message.value(QStringLiteral("text")).toString(),
                                   .reasoning = message.value(QStringLiteral("reasoning")).toString(),
                                   .costCatalogDate = message.value(QStringLiteral("costCatalogDate")).toString(),
                                   .longContextRates = message.value(QStringLiteral("longContextRates")).toBool(),
                                   .truncated = message.value(QStringLiteral("truncated")).toBool()};
            const auto activities = message.value(QStringLiteral("toolActivities")).toArray();
            stored.toolActivities.reserve(static_cast<std::size_t>(activities.size()));
            for (const auto &activityValue : activities)
            {
                if (!activityValue.isObject())
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                const auto activity = activityValue.toObject();
                const QJsonValue argumentsJson = activity.value(QStringLiteral("argumentsJson"));
                const QJsonValue resultJson = activity.value(QStringLiteral("resultJson"));
                if (!activity.value(QStringLiteral("id")).isString()
                    || !activity.value(QStringLiteral("name")).isString()
                    || !activity.value(QStringLiteral("summary")).isString()
                    || (!argumentsJson.isUndefined() && !argumentsJson.isString())
                    || (!resultJson.isUndefined() && !resultJson.isString())
                    || !activity.value(QStringLiteral("state")).isString()
                    || !activity.value(QStringLiteral("resultCode")).isString()
                    || !activity.value(QStringLiteral("sideEffecting")).isBool()
                    || !activity.value(QStringLiteral("highRisk")).isBool())
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                stored.toolActivities.push_back(
                    {.id = activity.value(QStringLiteral("id")).toString().toUtf8().toStdString(),
                     .name = activity.value(QStringLiteral("name")).toString().toUtf8().toStdString(),
                     .summary = activity.value(QStringLiteral("summary")).toString().toUtf8().toStdString(),
                     .argumentsJson = argumentsJson.toString().toUtf8().toStdString(),
                     .resultJson = resultJson.toString().toUtf8().toStdString(),
                     .state = activity.value(QStringLiteral("state")).toString().toUtf8().toStdString(),
                     .resultCode = activity.value(QStringLiteral("resultCode")).toString().toUtf8().toStdString(),
                     .sideEffecting = activity.value(QStringLiteral("sideEffecting")).toBool(),
                     .highRisk = activity.value(QStringLiteral("highRisk")).toBool()});
            }
            const QJsonValue replay = message.value(QStringLiteral("providerReplay"));
            if (!replay.isUndefined())
            {
                if (!replay.isObject())
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                const QByteArray encoded = QJsonDocument(replay.toObject()).toJson(QJsonDocument::Compact);
                stored.providerReplayJson = {encoded.constData(), static_cast<std::size_t>(encoded.size())};
            }
            const auto usageValue = message.value(QStringLiteral("usage"));
            if (!usageValue.isUndefined())
            {
                if (!usageValue.isObject())
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                const auto usage = usageValue.toObject();
                const qint64 input = usage.value(QStringLiteral("inputTokens")).toInteger(-1);
                const qint64 output = usage.value(QStringLiteral("outputTokens")).toInteger(-1);
                const qint64 cached = usage.value(QStringLiteral("cachedInputTokens")).toInteger(-1);
                const qint64 reasoning = usage.value(QStringLiteral("reasoningTokens")).toInteger(-1);
                if (input < 0 || output < 0 || cached < 0 || reasoning < 0)
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                stored.usage = AiTokenUsage{.inputTokens = static_cast<std::uint64_t>(input),
                                            .outputTokens = static_cast<std::uint64_t>(output),
                                            .reasoningTokens = static_cast<std::uint64_t>(reasoning),
                                            .cachedInputTokens = static_cast<std::uint64_t>(cached)};
            }
            const auto metricsValue = message.value(QStringLiteral("metrics"));
            if (!metricsValue.isUndefined())
            {
                if (!metricsValue.isObject())
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                const auto metrics = metricsValue.toObject();
                const qint64 wallTime = metrics.value(QStringLiteral("wallTimeMilliseconds")).toInteger(-1);
                const qint64 retryCount = metrics.value(QStringLiteral("retryCount")).toInteger(-1);
                const auto firstTokenValue = metrics.value(QStringLiteral("firstTokenMilliseconds"));
                const qint64 firstToken = firstTokenValue.isUndefined() ? -1 : firstTokenValue.toInteger(-2);
                if (wallTime < 0 || retryCount < 0
                    || std::cmp_greater(retryCount, std::numeric_limits<std::uint32_t>::max()) || firstToken < -1)
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                stored.metrics =
                    AiTurnMetrics{.wallTimeMilliseconds = static_cast<std::uint64_t>(wallTime),
                                  .firstTokenMilliseconds =
                                      firstTokenValue.isUndefined()
                                          ? std::nullopt
                                          : std::optional<std::uint64_t>{static_cast<std::uint64_t>(firstToken)},
                                  .retryCount = static_cast<std::uint32_t>(retryCount)};
            }
            const auto costValue = message.value(QStringLiteral("estimatedCostUsd"));
            if (!costValue.isUndefined())
            {
                if (!costValue.isDouble() || !std::isfinite(costValue.toDouble()) || costValue.toDouble() < 0.0)
                {
                    return std::unexpected(AiConversationStoreError::invalidData);
                }
                stored.estimatedCostUsd = costValue.toDouble();
            }
            const auto sources = message.value(QStringLiteral("sources")).toArray();
            stored.sources.reserve(static_cast<std::size_t>(sources.size()));
            for (const auto &sourceValue : sources)
            {
                const auto source = sourceValue.toObject();
                stored.sources.push_back(AiWebSource{
                    .url = source.value(QStringLiteral("url")).toString().toUtf8().toStdString(),
                    .title = source.value(QStringLiteral("title")).toString().toUtf8().toStdString(),
                    .citedText = source.value(QStringLiteral("citedText")).toString().toUtf8().toStdString()});
            }
            conversation.messages.push_back(std::move(stored));
        }
        if (!validConversation(conversation, limits))
        {
            return std::unexpected(AiConversationStoreError::invalidData);
        }
        conversations.push_back(std::move(conversation));
    }
    return conversations;
}

[[nodiscard]] std::expected<LoadedStore, AiConversationStoreError>
decryptEnvelope(const QByteArray &envelopeBytes, security::CredentialVault &vault,
                const AiConversationStoreLimits &limits)
{
    const auto envelope = QJsonDocument::fromJson(envelopeBytes).object();
    const auto generationValue = envelope.value(QStringLiteral("generation"));
    const qint64 signedGeneration = generationValue.toInteger(-1);
    if (signedGeneration < 1 || generationValue.toDouble() != static_cast<double>(signedGeneration))
    {
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    auto key = readOrCreateKey(vault, false);
    if (!key)
    {
        return std::unexpected(key.error());
    }
    const QByteArray nonce = QByteArray::fromBase64(envelope.value(QStringLiteral("nonce")).toString().toLatin1());
    QByteArray ciphertext = QByteArray::fromBase64(envelope.value(QStringLiteral("ciphertext")).toString().toLatin1());
    const QByteArray tag = QByteArray::fromBase64(envelope.value(QStringLiteral("tag")).toString().toLatin1());
    auto plaintext = decrypt(ciphertext, tag, key->view(), nonce, static_cast<std::uint64_t>(signedGeneration));
    clearBytes(ciphertext);
    if (!plaintext)
    {
        return std::unexpected(plaintext.error());
    }
    auto conversations = parsePlaintext(*plaintext, limits);
    clearBytes(*plaintext);
    if (!conversations)
    {
        return std::unexpected(conversations.error());
    }
    return LoadedStore{.conversations = std::move(*conversations),
                       .generation = static_cast<std::uint64_t>(signedGeneration)};
}

[[nodiscard]] std::expected<LoadedStore, AiConversationStoreError> loadStore(const QString &filePath,
                                                                             security::CredentialVault &vault,
                                                                             const AiConversationStoreLimits &limits,
                                                                             bool *recoveredFromBackup = nullptr)
{
    auto loaded = persistence::loadLastKnownGood(filePath, maximumEnvelopeBytes, validateEnvelope);
    if (!loaded)
    {
        return std::unexpected(mapPersistenceError(loaded.error()));
    }
    if (!loaded->has_value())
    {
        return LoadedStore{};
    }
    auto decrypted = decryptEnvelope((*loaded)->bytes, vault, limits);
    if (!decrypted)
    {
        // The envelope is structurally valid but failed authentication (a
        // single flipped byte is enough). Try the last-known-good backup
        // before declaring the whole history unrecoverable.
        if (decrypted.error() == AiConversationStoreError::authenticationFailed
            || decrypted.error() == AiConversationStoreError::invalidData)
        {
            QFile backup(filePath + QStringLiteral(".bak"));
            if (backup.open(QIODevice::ReadOnly) && backup.size() >= 0 && backup.size() <= maximumEnvelopeBytes)
            {
                const QByteArray backupBytes = backup.readAll();
                if (backupBytes.size() == backup.size()
                    && validateEnvelope(backupBytes) == persistence::PayloadValidation::valid)
                {
                    auto recovered = decryptEnvelope(backupBytes, vault, limits);
                    if (recovered)
                    {
                        if (recoveredFromBackup != nullptr)
                        {
                            *recoveredFromBackup = true;
                        }
                        return recovered;
                    }
                }
            }
        }
        return std::unexpected(decrypted.error());
    }
    if (recoveredFromBackup != nullptr)
    {
        *recoveredFromBackup = (*loaded)->recoveredFromBackup;
    }
    return decrypted;
}

[[nodiscard]] std::expected<void, AiConversationStoreError>
saveStore(const QString &filePath, security::CredentialVault &vault, const AiConversationStoreLimits &limits,
          const std::vector<AiStoredConversation> &conversations, const std::uint64_t previousGeneration)
{
    if (previousGeneration == std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    QByteArray plaintext = serializePlaintext(conversations);
    if (std::cmp_greater(plaintext.size(), limits.maximumPlaintextBytes))
    {
        clearBytes(plaintext);
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    auto key = readOrCreateKey(vault, !QFileInfo::exists(filePath));
    if (!key)
    {
        clearBytes(plaintext);
        return std::unexpected(key.error());
    }
    QByteArray nonce(nonceSize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(nonce.data()), static_cast<int>(nonce.size())) != 1)
    {
        clearBytes(plaintext);
        clearBytes(nonce);
        return std::unexpected(AiConversationStoreError::cryptoError);
    }
    const std::uint64_t generation = previousGeneration + 1;
    auto encrypted = encrypt(plaintext, key->view(), nonce, generation);
    clearBytes(plaintext);
    if (!encrypted)
    {
        clearBytes(nonce);
        return std::unexpected(encrypted.error());
    }
    const QByteArray envelope =
        QJsonDocument(QJsonObject{{QStringLiteral("version"), schemaVersion},
                                  {QStringLiteral("generation"), static_cast<double>(generation)},
                                  {QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64())},
                                  {QStringLiteral("ciphertext"), QString::fromLatin1(encrypted->ciphertext.toBase64())},
                                  {QStringLiteral("tag"), QString::fromLatin1(encrypted->tag.toBase64())}})
            .toJson(QJsonDocument::Compact);
    clearBytes(nonce);
    clearBytes(encrypted->ciphertext);
    clearBytes(encrypted->tag);
    if (auto saved = persistence::saveLastKnownGood(filePath, envelope, maximumEnvelopeBytes, validateEnvelope); !saved)
    {
        return std::unexpected(mapPersistenceError(saved.error()));
    }
    return {};
}

void applyRetention(std::vector<AiStoredConversation> &conversations, const AiConversationStoreLimits &limits)
{
    const QDateTime oldest = QDateTime::currentDateTimeUtc().addDays(-limits.retentionDays);
    std::erase_if(conversations, [&oldest](const AiStoredConversation &conversation) {
        return conversation.updatedAtUtc < oldest;
    });
    std::ranges::sort(conversations, std::greater{}, &AiStoredConversation::updatedAtUtc);
    if (conversations.size() > limits.maximumConversations)
    {
        conversations.resize(limits.maximumConversations);
    }
}
} // namespace

namespace
{
// Cross-process mutual exclusion for the read-modify-write cycles. The
// in-process caller (AiConversationHistoryModel) already serializes on one
// worker; the lock protects a second application instance from silently
// overwriting this instance's updates (or racing the data-key creation).
// QLockFile derives from QObject and is neither copyable nor movable, so the
// held lock is owned through a pointer that releases on scope exit.
[[nodiscard]] std::unique_ptr<QLockFile> acquireLock(const QString &filePath)
{
    auto lock = std::make_unique<QLockFile>(filePath + QStringLiteral(".lock"));
    lock->setStaleLockTime(30'000);
    if (!lock->tryLock(5'000))
    {
        return nullptr;
    }
    return lock;
}
} // namespace

AiConversationStore::AiConversationStore(QString filePath, security::CredentialVault &vault,
                                         AiConversationStoreLimits limits)
    : m_filePath(std::move(filePath)), m_vault(vault), m_limits(limits)
{
}

const QString &AiConversationStore::filePath() const noexcept
{
    return m_filePath;
}

const AiConversationStoreLimits &AiConversationStore::limits() const noexcept
{
    return m_limits;
}

bool AiConversationStore::lastLoadRecoveredFromBackup() const noexcept
{
    return m_lastLoadRecoveredFromBackup;
}

std::expected<std::vector<AiStoredConversation>, AiConversationStoreError> AiConversationStore::load() const
{
    auto lock = acquireLock(m_filePath);
    if (!lock)
    {
        return std::unexpected(AiConversationStoreError::ioError);
    }
    auto loaded = loadStore(m_filePath, m_vault, m_limits, &m_lastLoadRecoveredFromBackup);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    return std::move(loaded->conversations);
}

std::expected<void, AiConversationStoreError> AiConversationStore::upsert(AiStoredConversation conversation) const
{
    if (!validConversation(conversation, m_limits))
    {
        return std::unexpected(AiConversationStoreError::invalidData);
    }
    auto lock = acquireLock(m_filePath);
    if (!lock)
    {
        return std::unexpected(AiConversationStoreError::ioError);
    }
    conversation.updatedAtUtc = conversation.updatedAtUtc.toUTC();
    auto loaded = loadStore(m_filePath, m_vault, m_limits, &m_lastLoadRecoveredFromBackup);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    auto existing = std::ranges::find(loaded->conversations, conversation.id, &AiStoredConversation::id);
    if (existing == loaded->conversations.end())
    {
        loaded->conversations.push_back(std::move(conversation));
    }
    else
    {
        *existing = std::move(conversation);
    }
    applyRetention(loaded->conversations, m_limits);
    return saveStore(m_filePath, m_vault, m_limits, loaded->conversations, loaded->generation);
}

std::expected<void, AiConversationStoreError> AiConversationStore::erase(const QString &conversationId) const
{
    auto lock = acquireLock(m_filePath);
    if (!lock)
    {
        return std::unexpected(AiConversationStoreError::ioError);
    }
    auto loaded = loadStore(m_filePath, m_vault, m_limits, &m_lastLoadRecoveredFromBackup);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    std::erase_if(loaded->conversations, [&conversationId](const AiStoredConversation &conversation) {
        return conversation.id == conversationId;
    });
    return saveStore(m_filePath, m_vault, m_limits, loaded->conversations, loaded->generation);
}

std::expected<void, AiConversationStoreError> AiConversationStore::clear() const
{
    auto lock = acquireLock(m_filePath);
    if (!lock)
    {
        return std::unexpected(AiConversationStoreError::ioError);
    }
    bool removed = true;
    for (const QString &path : {m_filePath, m_filePath + QStringLiteral(".bak")})
    {
        if (QFileInfo::exists(path) && !QFile::remove(path))
        {
            removed = false;
        }
    }
    auto keyRemoved = m_vault.remove(conversationKey());
    if (!removed)
    {
        return std::unexpected(AiConversationStoreError::ioError);
    }
    if (!keyRemoved && keyRemoved.error() != security::CredentialVaultError::NotFound)
    {
        return std::unexpected(mapVaultError(keyRemoved.error()));
    }
    return {};
}

std::expected<void, AiConversationStoreError> AiConversationStore::exportDecrypted(const QString &destinationPath) const
{
    auto lock = acquireLock(m_filePath);
    if (!lock)
    {
        return std::unexpected(AiConversationStoreError::ioError);
    }
    auto loaded = loadStore(m_filePath, m_vault, m_limits, &m_lastLoadRecoveredFromBackup);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    QByteArray plaintext = serializePlaintext(loaded->conversations);
    QSaveFile file(destinationPath);
    if (destinationPath.trimmed().isEmpty() || !file.open(QIODevice::WriteOnly)
        || file.write(plaintext) != plaintext.size() || !file.commit())
    {
        clearBytes(plaintext);
        return std::unexpected(AiConversationStoreError::ioError);
    }
    clearBytes(plaintext);
    return {};
}

} // namespace ztermy::ai
