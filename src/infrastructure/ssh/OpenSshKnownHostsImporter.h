#pragma once

#include "domain/ssh/SshHostKey.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace ztermy::ssh
{

enum class OpenSshKnownHostsImportError : std::uint8_t
{
    InvalidPath,
    IoError,
    TooLarge,
};

struct OpenSshKnownHostsImport final
{
    std::vector<KnownHostEntry> entries;
    std::size_t parsedLines = 0;
    std::size_t skippedLines = 0;
    std::size_t unsupportedEntries = 0;
    std::size_t duplicateEntries = 0;
};

[[nodiscard]] OpenSshKnownHostsImport parseOpenSshKnownHosts(const QByteArray &contents);
[[nodiscard]] std::expected<OpenSshKnownHostsImport, OpenSshKnownHostsImportError>
loadOpenSshKnownHosts(const QString &filePath);

} // namespace ztermy::ssh
