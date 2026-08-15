#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class CodexAppServerMessageKind : std::uint8_t
{
    response,
    notification,
    request,
};

struct CodexAppServerMessage final
{
    CodexAppServerMessageKind kind = CodexAppServerMessageKind::notification;
    std::optional<std::uint64_t> id;
    QString method;
    QJsonObject params;
    QJsonObject result;
    QJsonObject error;
};

class CodexAppServerProtocol final
{
public:
    [[nodiscard]] QByteArray initializeRequest(std::uint64_t id, std::string_view clientVersion) const;
    [[nodiscard]] QByteArray initializedNotification() const;
    [[nodiscard]] std::expected<QByteArray, QString> startThreadRequest(std::uint64_t id, std::string_view model,
                                                                        std::string_view localWorkingDirectory,
                                                                        std::string_view developerInstructions,
                                                                        std::span<const AiToolDefinition> tools) const;
    [[nodiscard]] std::expected<QByteArray, QString> resumeThreadRequest(std::uint64_t id, std::string_view threadId,
                                                                         std::string_view model,
                                                                         std::string_view localWorkingDirectory,
                                                                         std::string_view developerInstructions,
                                                                         std::span<const AiToolDefinition> tools) const;
    [[nodiscard]] std::expected<QByteArray, QString> startTurnRequest(std::uint64_t id, std::string_view threadId,
                                                                      std::string_view prompt) const;
    [[nodiscard]] std::expected<QByteArray, QString> interruptTurnRequest(std::uint64_t id, std::string_view threadId,
                                                                          std::string_view turnId) const;
    [[nodiscard]] std::expected<QByteArray, QString> dynamicToolResponse(std::uint64_t id, bool success,
                                                                         std::string_view output) const;
    [[nodiscard]] std::expected<std::vector<CodexAppServerMessage>, QString> append(const QByteArray &bytes);
    void reset() noexcept;

private:
    QByteArray m_buffer;
};

} // namespace ztermy::ai
