#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>

namespace ztermy::persistence
{

enum class PayloadValidation : std::uint8_t
{
    valid,
    invalid,
    unsupportedVersion,
};

enum class LastKnownGoodError : std::uint8_t
{
    invalidPath,
    io,
    invalidFormat,
    unsupportedVersion,
};

struct LastKnownGoodPayload final
{
    QByteArray bytes;
    bool recoveredFromBackup = false;
};

using PayloadValidator = std::function<PayloadValidation(QByteArrayView)>;

[[nodiscard]] std::expected<std::optional<LastKnownGoodPayload>, LastKnownGoodError>
loadLastKnownGood(const QString &filePath, qsizetype maximumBytes, const PayloadValidator &validator);

[[nodiscard]] std::expected<void, LastKnownGoodError> saveLastKnownGood(const QString &filePath, QByteArrayView payload,
                                                                        qsizetype maximumBytes,
                                                                        const PayloadValidator &validator);

} // namespace ztermy::persistence
