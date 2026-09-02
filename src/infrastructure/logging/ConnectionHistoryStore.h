#pragma once

#include "domain/logging/ConnectionHistory.h"

#include <QString>

#include <expected>

namespace ztermy::logging
{

enum class ConnectionHistoryStoreError : std::uint8_t
{
    Io,
    Corrupt,
    UnsupportedVersion,
};

class ConnectionHistoryStore final
{
public:
    explicit ConnectionHistoryStore(QString path);

    [[nodiscard]] std::expected<ConnectionHistory, ConnectionHistoryStoreError> load() const;
    [[nodiscard]] std::expected<void, ConnectionHistoryStoreError> save(const ConnectionHistory &history) const;

private:
    QString m_path;
};

} // namespace ztermy::logging
