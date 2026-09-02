#pragma once

#include <QString>

#include <cstdint>
#include <expected>
#include <vector>

namespace ztermy::ssh
{

struct OpenSshHostConfig final
{
    QString alias;
    QString hostName;
    QString user;
    QString identityFile;
    QString proxyJump;
    std::uint16_t port = 22;

    bool operator==(const OpenSshHostConfig &) const = default;
};

enum class OpenSshConfigImportError : std::uint8_t
{
    invalidPath,
    io,
    tooLarge,
    invalidFormat,
};

struct OpenSshConfigImport final
{
    std::vector<OpenSshHostConfig> hosts;
    std::size_t parsedFiles = 0;
    std::size_t skippedHosts = 0;
};

[[nodiscard]] std::expected<OpenSshConfigImport, OpenSshConfigImportError> loadOpenSshConfig(const QString &filePath);

} // namespace ztermy::ssh
