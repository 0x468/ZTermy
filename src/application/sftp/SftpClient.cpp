#include "application/sftp/SftpClient.h"

#include <algorithm>
#include <utility>

namespace ztermy::sftp
{
namespace
{

constexpr auto operationTimeout = std::chrono::seconds(30);

class Libssh2SftpClient final : public SftpClient
{
public:
    explicit Libssh2SftpClient(ssh::AuthenticatedSshConnection connection) noexcept
        : m_connection(std::move(connection))
    {
    }

    ~Libssh2SftpClient() override
    {
        if (m_connection.session != nullptr && m_connection.session->sftpOpen())
        {
            [[maybe_unused]] const auto closed = m_connection.session->closeSftp(m_connection.socket, operationTimeout);
        }
    }

    [[nodiscard]] std::expected<std::vector<DirectoryEntry>, ssh::SshTransportError>
    listDirectory(const std::string_view remotePath, const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->listSftpDirectory(m_connection.socket, remotePath, operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<std::optional<DirectoryEntry>, ssh::SshTransportError>
    statEntry(const std::string_view remotePath, const std::stop_token &stopToken) noexcept override
    {
        const std::string parent = parentRemotePath(remotePath);
        auto entries = listDirectory(parent, stopToken);
        if (!entries)
        {
            return std::unexpected(entries.error());
        }
        const std::size_t separator = remotePath.find_last_of('/');
        const std::string_view name = remotePath.substr(separator == std::string_view::npos ? 0 : separator + 1);
        const auto found = std::ranges::find(*entries, name, &DirectoryEntry::name);
        if (found == entries->end())
        {
            return std::optional<DirectoryEntry>{};
        }
        return std::optional<DirectoryEntry>{std::move(*found)};
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError>
    createDirectory(const std::string_view remotePath, const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->createSftpDirectory(m_connection.socket, remotePath, operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError>
    renameEntry(const std::string_view sourcePath, const std::string_view destinationPath, const bool replace,
                const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->renameSftpEntry(m_connection.socket, sourcePath, destinationPath,
                                                     replace ? ssh::SftpRenameDisposition::ReplaceAtomically
                                                             : ssh::SftpRenameDisposition::NoReplace,
                                                     operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError>
    removeEntry(const std::string_view remotePath, const bool directory,
                const std::stop_token &stopToken) noexcept override
    {
        if (directory)
        {
            return m_connection.session->removeSftpDirectory(m_connection.socket, remotePath, operationTimeout,
                                                             stopToken);
        }
        return m_connection.session->removeSftpFile(m_connection.socket, remotePath, operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError>
    openFileForRead(const std::string_view remotePath, const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->openSftpFileForRead(m_connection.socket, remotePath, operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError>
    openFileForWrite(const std::string_view remotePath, const bool replace,
                     const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->openSftpFileForWrite(m_connection.socket, remotePath,
                                                          replace ? ssh::SftpWriteDisposition::Replace
                                                                  : ssh::SftpWriteDisposition::CreateNew,
                                                          operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<std::size_t, ssh::SshTransportError>
    readFile(const std::span<char> output, const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->readSftpFile(m_connection.socket, output, operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError>
    writeFile(const std::span<const char> input, const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->writeSftpFile(m_connection.socket, input, operationTimeout, stopToken);
    }

    [[nodiscard]] std::expected<void, ssh::SshTransportError>
    closeFile(const std::stop_token &stopToken) noexcept override
    {
        return m_connection.session->closeSftpFile(m_connection.socket, operationTimeout, stopToken);
    }

private:
    ssh::AuthenticatedSshConnection m_connection;
};

} // namespace

std::expected<std::unique_ptr<SftpClient>, ssh::SshBootstrapError>
createSftpClient(ssh::SshConnectionRequest &request, const ssh::SshConnectionCallbacks &callbacks,
                 const std::stop_token &stopToken) noexcept
{
    auto connection = ssh::establishAuthenticatedSshConnection(request, callbacks, stopToken);
    if (!connection)
    {
        return std::unexpected(connection.error());
    }

    auto opened = connection->session->openSftp(connection->socket, operationTimeout, stopToken);
    if (!opened)
    {
        return std::unexpected(ssh::SshBootstrapError{
            .failure = ssh::sshFailureFromTransport(opened.error(), true),
            .reason = ssh::SshBootstrapErrorReason::ConnectionFailure,
        });
    }

    try
    {
        return std::make_unique<Libssh2SftpClient>(std::move(*connection));
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(ssh::SshBootstrapError{
            .failure = ssh::SshFailureKind::ProtocolError,
            .reason = ssh::SshBootstrapErrorReason::ConnectionFailure,
        });
    }
}

} // namespace ztermy::sftp
