#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace ztermy::ai
{

enum class AcpMessageKind : std::uint8_t
{
    response,
    notification,
    request,
};

struct AcpMessage final
{
    AcpMessageKind kind = AcpMessageKind::notification;
    bool hasId = false;
    QJsonValue id;
    QString method;
    QJsonObject params;
    QJsonValue result;
    QJsonObject error;
};

class AcpProtocol final
{
public:
    [[nodiscard]] std::expected<QByteArray, QString> initializeRequest(std::uint64_t id, std::string_view clientVersion,
                                                                       bool terminalCapability) const;
    [[nodiscard]] std::expected<QByteArray, QString> newSessionRequest(std::uint64_t id,
                                                                       std::string_view workingDirectory) const;
    [[nodiscard]] std::expected<QByteArray, QString> resumeSessionRequest(std::uint64_t id, std::string_view sessionId,
                                                                          std::string_view workingDirectory) const;
    [[nodiscard]] std::expected<QByteArray, QString> promptRequest(std::uint64_t id, std::string_view sessionId,
                                                                   std::string_view prompt) const;
    [[nodiscard]] std::expected<QByteArray, QString> cancelNotification(std::string_view sessionId) const;
    [[nodiscard]] std::expected<QByteArray, QString> closeSessionRequest(std::uint64_t id,
                                                                         std::string_view sessionId) const;
    [[nodiscard]] std::expected<QByteArray, QString> resultResponse(const QJsonValue &id,
                                                                    const QJsonValue &result) const;
    [[nodiscard]] std::expected<QByteArray, QString> errorResponse(const QJsonValue &id, int code,
                                                                   std::string_view message) const;
    [[nodiscard]] std::expected<std::vector<AcpMessage>, QString> append(const QByteArray &bytes);
    void reset() noexcept;

private:
    QByteArray m_buffer;
};

} // namespace ztermy::ai
