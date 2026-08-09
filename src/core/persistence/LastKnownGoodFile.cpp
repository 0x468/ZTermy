#include "core/persistence/LastKnownGoodFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <utility>

namespace ztermy::persistence
{
namespace
{

enum class ReadError : std::uint8_t
{
    missing,
    io,
    tooLarge,
};

[[nodiscard]] std::expected<QByteArray, ReadError> readBounded(const QString &path, const qsizetype maximumBytes)
{
    QFile file(path);
    if (!file.exists())
    {
        return std::unexpected(ReadError::missing);
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(ReadError::io);
    }
    if (file.size() < 0)
    {
        return std::unexpected(ReadError::io);
    }
    if (file.size() > maximumBytes)
    {
        return std::unexpected(ReadError::tooLarge);
    }
    const QByteArray payload = file.readAll();
    if (payload.size() != file.size())
    {
        return std::unexpected(ReadError::io);
    }
    return payload;
}

[[nodiscard]] std::expected<void, LastKnownGoodError> writeAtomic(const QString &path, const QByteArrayView payload)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload.data(), payload.size()) != payload.size()
        || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(LastKnownGoodError::io);
    }
    return {};
}

[[nodiscard]] LastKnownGoodError mapReadError(const ReadError error)
{
    return error == ReadError::tooLarge ? LastKnownGoodError::invalidFormat : LastKnownGoodError::io;
}

[[nodiscard]] LastKnownGoodError mapValidation(const PayloadValidation validation)
{
    return validation == PayloadValidation::unsupportedVersion ? LastKnownGoodError::unsupportedVersion
                                                               : LastKnownGoodError::invalidFormat;
}

[[nodiscard]] std::optional<QByteArray> validBackup(const QString &path, const qsizetype maximumBytes,
                                                    const PayloadValidator &validator)
{
    auto backup = readBounded(path, maximumBytes);
    if (!backup || validator(*backup) != PayloadValidation::valid)
    {
        return std::nullopt;
    }
    return std::move(*backup);
}

} // namespace

std::expected<std::optional<LastKnownGoodPayload>, LastKnownGoodError>
loadLastKnownGood(const QString &filePath, const qsizetype maximumBytes, const PayloadValidator &validator)
{
    if (filePath.isEmpty() || maximumBytes <= 0 || !validator)
    {
        return std::unexpected(LastKnownGoodError::invalidPath);
    }

    const QString backupPath = filePath + QStringLiteral(".bak");
    auto primary = readBounded(filePath, maximumBytes);
    if (!primary)
    {
        if (auto backup = validBackup(backupPath, maximumBytes, validator))
        {
            return LastKnownGoodPayload{.bytes = std::move(*backup), .recoveredFromBackup = true};
        }
        if (primary.error() == ReadError::missing)
        {
            return std::optional<LastKnownGoodPayload>{};
        }
        return std::unexpected(mapReadError(primary.error()));
    }

    const PayloadValidation primaryValidation = validator(*primary);
    if (primaryValidation == PayloadValidation::valid)
    {
        return LastKnownGoodPayload{.bytes = std::move(*primary)};
    }
    if (primaryValidation == PayloadValidation::unsupportedVersion)
    {
        return std::unexpected(LastKnownGoodError::unsupportedVersion);
    }
    if (auto backup = validBackup(backupPath, maximumBytes, validator))
    {
        return LastKnownGoodPayload{.bytes = std::move(*backup), .recoveredFromBackup = true};
    }
    return std::unexpected(mapValidation(primaryValidation));
}

std::expected<void, LastKnownGoodError> saveLastKnownGood(const QString &filePath, const QByteArrayView payload,
                                                          const qsizetype maximumBytes,
                                                          const PayloadValidator &validator)
{
    if (filePath.isEmpty() || maximumBytes <= 0 || !validator)
    {
        return std::unexpected(LastKnownGoodError::invalidPath);
    }
    if (payload.size() > maximumBytes)
    {
        return std::unexpected(LastKnownGoodError::invalidFormat);
    }
    const PayloadValidation newValidation = validator(payload);
    if (newValidation != PayloadValidation::valid)
    {
        return std::unexpected(mapValidation(newValidation));
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.absoluteDir().mkpath(QStringLiteral(".")))
    {
        return std::unexpected(LastKnownGoodError::io);
    }

    auto primary = readBounded(filePath, maximumBytes);
    if (primary)
    {
        const PayloadValidation primaryValidation = validator(*primary);
        if (primaryValidation == PayloadValidation::unsupportedVersion)
        {
            return std::unexpected(LastKnownGoodError::unsupportedVersion);
        }
        if (primaryValidation == PayloadValidation::valid)
        {
            if (auto backedUp = writeAtomic(filePath + QStringLiteral(".bak"), *primary); !backedUp)
            {
                return backedUp;
            }
        }
    }
    else if (primary.error() == ReadError::io)
    {
        return std::unexpected(LastKnownGoodError::io);
    }

    return writeAtomic(filePath, payload);
}

} // namespace ztermy::persistence
