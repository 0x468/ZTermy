#pragma once

#include "application/ssh/SshConnectionBootstrap.h"
#include "domain/sftp/SftpTypes.h"
#include "infrastructure/ssh/Libssh2Session.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace ztermy::sftp
{

class SftpClient
{
public:
    virtual ~SftpClient() = default;

    SftpClient(const SftpClient &) = delete;
    SftpClient &operator=(const SftpClient &) = delete;

    [[nodiscard]] virtual std::expected<std::string, ssh::SshTransportError>
    canonicalizePath(std::string_view remotePath, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<std::vector<DirectoryEntry>, ssh::SshTransportError>
    listDirectory(std::string_view remotePath, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<std::optional<DirectoryEntry>, ssh::SshTransportError>
    statEntry(std::string_view remotePath, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError>
    createDirectory(std::string_view remotePath, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError> renameEntry(std::string_view sourcePath,
                                                                                  std::string_view destinationPath,
                                                                                  bool replace,
                                                                                  const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError>
    removeEntry(std::string_view remotePath, bool directory, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError>
    openFileForRead(std::string_view remotePath, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError>
    openFileForWrite(std::string_view remotePath, bool replace, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError> openFileForResume(std::string_view,
                                                                                        const std::stop_token &)
    {
        return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::InvalidState});
    }
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError> seekFile(std::uint64_t)
    {
        return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::InvalidState});
    }
    [[nodiscard]] virtual std::expected<std::size_t, ssh::SshTransportError>
    readFile(std::span<char> output, const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError> writeFile(std::span<const char> input,
                                                                                const std::stop_token &stopToken) = 0;
    [[nodiscard]] virtual std::expected<void, ssh::SshTransportError> closeFile(const std::stop_token &stopToken) = 0;

protected:
    SftpClient() = default;
};

using SftpClientFactory = std::function<std::expected<std::unique_ptr<SftpClient>, ssh::SshBootstrapError>(
    ssh::SshConnectionRequest &, const ssh::SshConnectionCallbacks &, const std::stop_token &)>;

[[nodiscard]] std::expected<std::unique_ptr<SftpClient>, ssh::SshBootstrapError>
createSftpClient(ssh::SshConnectionRequest &request, const ssh::SshConnectionCallbacks &callbacks,
                 const std::stop_token &stopToken) noexcept;

} // namespace ztermy::sftp
