#include "application/sftp/SftpFilenameCodec.h"

#include <QString>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace ztermy::sftp
{
namespace
{

[[nodiscard]] std::expected<std::string, ssh::SshTransportError> toWire(const std::string_view value,
                                                                        const std::string_view encoding)
{
    if (encoding == "utf-8")
    {
        return std::string(value);
    }
#ifdef Q_OS_WIN
    const QString text = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    const auto *wide = reinterpret_cast<LPCWCH>(text.utf16());
    const int required =
        WideCharToMultiByte(54936, 0, wide, static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::InvalidArgument,
                                                      .nativeCode = static_cast<int>(GetLastError())});
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(54936, 0, wide, static_cast<int>(text.size()), result.data(), required, nullptr, nullptr)
        != required)
    {
        return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::InvalidArgument,
                                                      .nativeCode = static_cast<int>(GetLastError())});
    }
    return result;
#else
    return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::InvalidArgument});
#endif
}

[[nodiscard]] std::expected<std::string, ssh::SshTransportError> fromWire(const std::string_view value,
                                                                          const std::string_view encoding)
{
    if (encoding == "utf-8")
    {
        return std::string(value);
    }
#ifdef Q_OS_WIN
    const int required =
        MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::ProtocolError,
                                                      .nativeCode = static_cast<int>(GetLastError())});
    }
    QString result(required, Qt::Uninitialized);
    if (MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            reinterpret_cast<LPWSTR>(result.data()), required)
        != required)
    {
        return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::ProtocolError,
                                                      .nativeCode = static_cast<int>(GetLastError())});
    }
    return result.toUtf8().toStdString();
#else
    return std::unexpected(ssh::SshTransportError{.kind = ssh::SshTransportErrorKind::InvalidArgument});
#endif
}

class FilenameEncodingClient final : public SftpClient
{
public:
    FilenameEncodingClient(std::unique_ptr<SftpClient> client, std::string encoding)
        : m_client(std::move(client)), m_encoding(std::move(encoding))
    {
    }

    std::expected<std::string, ssh::SshTransportError> canonicalizePath(const std::string_view path,
                                                                        const std::stop_token &stopToken) override
    {
        auto wire = toWire(path, m_encoding);
        if (!wire)
        {
            return std::unexpected(wire.error());
        }
        auto result = m_client->canonicalizePath(*wire, stopToken);
        return result ? fromWire(*result, m_encoding) : std::unexpected(result.error());
    }

    std::expected<std::vector<DirectoryEntry>, ssh::SshTransportError>
    listDirectory(const std::string_view path, const std::stop_token &stopToken) override
    {
        auto wire = toWire(path, m_encoding);
        if (!wire)
        {
            return std::unexpected(wire.error());
        }
        auto entries = m_client->listDirectory(*wire, stopToken);
        if (!entries)
        {
            return std::unexpected(entries.error());
        }
        for (DirectoryEntry &entry : *entries)
        {
            auto name = fromWire(entry.name, m_encoding);
            auto remotePath = fromWire(entry.remotePath, m_encoding);
            if (!name || !remotePath)
            {
                return std::unexpected(name ? remotePath.error() : name.error());
            }
            entry.name = std::move(*name);
            entry.remotePath = std::move(*remotePath);
        }
        return entries;
    }

    std::expected<std::optional<DirectoryEntry>, ssh::SshTransportError>
    statEntry(const std::string_view path, const std::stop_token &stopToken) override
    {
        auto parent = listDirectory(parentRemotePath(path), stopToken);
        if (!parent)
        {
            return std::unexpected(parent.error());
        }
        const auto separator = path.find_last_of('/');
        const std::string_view name = path.substr(separator == std::string_view::npos ? 0 : separator + 1);
        const auto found = std::ranges::find(*parent, name, &DirectoryEntry::name);
        return found == parent->end() ? std::optional<DirectoryEntry>{}
                                      : std::optional<DirectoryEntry>{std::move(*found)};
    }

    std::expected<void, ssh::SshTransportError> createDirectory(const std::string_view path,
                                                                const std::stop_token &token) override
    {
        return pathCall(path, token, &SftpClient::createDirectory);
    }
    std::expected<void, ssh::SshTransportError> removeEntry(const std::string_view path, const bool directory,
                                                            const std::stop_token &token) override
    {
        auto wire = toWire(path, m_encoding);
        return wire ? m_client->removeEntry(*wire, directory, token) : std::unexpected(wire.error());
    }
    std::expected<void, ssh::SshTransportError> renameEntry(const std::string_view source,
                                                            const std::string_view destination, const bool replace,
                                                            const std::stop_token &token) override
    {
        auto sourceWire = toWire(source, m_encoding);
        auto destinationWire = toWire(destination, m_encoding);
        if (!sourceWire || !destinationWire)
        {
            return std::unexpected(sourceWire ? destinationWire.error() : sourceWire.error());
        }
        return m_client->renameEntry(*sourceWire, *destinationWire, replace, token);
    }
    std::expected<void, ssh::SshTransportError> openFileForRead(const std::string_view path,
                                                                const std::stop_token &token) override
    {
        return pathCall(path, token, &SftpClient::openFileForRead);
    }
    std::expected<void, ssh::SshTransportError> openFileForWrite(const std::string_view path, const bool replace,
                                                                 const std::stop_token &token) override
    {
        auto wire = toWire(path, m_encoding);
        return wire ? m_client->openFileForWrite(*wire, replace, token) : std::unexpected(wire.error());
    }
    std::expected<void, ssh::SshTransportError> openFileForResume(const std::string_view path,
                                                                  const std::stop_token &token) override
    {
        return pathCall(path, token, &SftpClient::openFileForResume);
    }
    std::expected<void, ssh::SshTransportError> seekFile(const std::uint64_t offset) override
    {
        return m_client->seekFile(offset);
    }
    std::expected<std::size_t, ssh::SshTransportError> readFile(const std::span<char> output,
                                                                const std::stop_token &token) override
    {
        return m_client->readFile(output, token);
    }
    std::expected<void, ssh::SshTransportError> writeFile(const std::span<const char> input,
                                                          const std::stop_token &token) override
    {
        return m_client->writeFile(input, token);
    }
    std::expected<void, ssh::SshTransportError> closeFile(const std::stop_token &token) override
    {
        return m_client->closeFile(token);
    }

private:
    using PathMethod = std::expected<void, ssh::SshTransportError> (SftpClient::*)(std::string_view,
                                                                                   const std::stop_token &);
    std::expected<void, ssh::SshTransportError> pathCall(const std::string_view path, const std::stop_token &token,
                                                         const PathMethod method)
    {
        auto wire = toWire(path, m_encoding);
        return wire ? ((*m_client).*method)(*wire, token) : std::unexpected(wire.error());
    }

    std::unique_ptr<SftpClient> m_client;
    std::string m_encoding;
};

} // namespace

std::unique_ptr<SftpClient> withFilenameEncoding(std::unique_ptr<SftpClient> client, std::string encoding)
{
    if (!client || encoding == "utf-8")
    {
        return client;
    }
    return std::make_unique<FilenameEncodingClient>(std::move(client), std::move(encoding));
}

} // namespace ztermy::sftp
