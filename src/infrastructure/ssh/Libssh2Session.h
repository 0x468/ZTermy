#pragma once

#include "domain/sftp/SftpTypes.h"
#include "domain/ssh/SshHostKey.h"
#include "infrastructure/ssh/Libssh2Runtime.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ssh
{

enum class SshTransportErrorKind : std::uint8_t
{
    InitializationFailed,
    InvalidArgument,
    InvalidState,
    TimedOut,
    Cancelled,
    ConnectionLost,
    AuthenticationRejected,
    AuthenticationUnavailable,
    ProtocolError,
};

struct SshTransportError final
{
    SshTransportErrorKind kind = SshTransportErrorKind::ProtocolError;
    int libssh2Code = 0;
    int nativeCode = 0;

    [[nodiscard]] friend bool operator==(const SshTransportError &, const SshTransportError &) = default;
};

enum class AuxiliaryCommandProgress : std::uint8_t
{
    Pending,
    Output,
    Completed,
};

struct AuxiliaryCommandPollResult final
{
    AuxiliaryCommandProgress progress = AuxiliaryCommandProgress::Pending;
    std::size_t bytesRead = 0;
    int exitStatus = 0;
};

enum class SftpWriteDisposition : std::uint8_t
{
    CreateNew,
    Replace,
};

enum class SftpRenameDisposition : std::uint8_t
{
    NoReplace,
    ReplaceAtomically,
};

class Libssh2Session final
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<Libssh2Session>, SshTransportError> create() noexcept;

    ~Libssh2Session();

    Libssh2Session(const Libssh2Session &) = delete;
    Libssh2Session &operator=(const Libssh2Session &) = delete;
    Libssh2Session(Libssh2Session &&) = delete;
    Libssh2Session &operator=(Libssh2Session &&) = delete;

    [[nodiscard]] std::expected<void, SshTransportError> handshake(WindowsTcpSocket &socket,
                                                                   std::chrono::milliseconds timeout,
                                                                   const std::stop_token &stopToken = {}) noexcept;

    [[nodiscard]] bool handshakeComplete() const noexcept;
    [[nodiscard]] std::expected<ObservedHostKey, SshTransportError> hostKey() const noexcept;
    [[nodiscard]] std::expected<HostKeyTrust, SshTransportError>
    verifyHostKey(const SshEndpoint &endpoint, std::span<const KnownHostEntry> knownHosts) noexcept;

    [[nodiscard]] std::expected<void, SshTransportError>
    authenticateWithPassword(WindowsTcpSocket &socket, std::string_view username, std::string_view password,
                             std::chrono::milliseconds timeout, const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError>
    authenticateWithPrivateKeyFile(WindowsTcpSocket &socket, std::string_view username, std::string_view privateKeyPath,
                                   std::string_view passphrase, std::chrono::milliseconds timeout,
                                   const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] bool authenticated() const noexcept;

    [[nodiscard]] std::expected<void, SshTransportError> openTerminal(WindowsTcpSocket &socket, std::uint32_t columns,
                                                                      std::uint32_t rows, std::string_view terminalType,
                                                                      std::chrono::milliseconds timeout,
                                                                      const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<std::size_t, SshTransportError>
    readTerminal(WindowsTcpSocket &socket, std::span<char> output, std::chrono::milliseconds timeout,
                 const std::stop_token &stopToken = {}, std::uintptr_t interruptEvent = 0) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError> writeTerminal(WindowsTcpSocket &socket,
                                                                       std::span<const char> input,
                                                                       std::chrono::milliseconds timeout,
                                                                       const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError> resizeTerminal(WindowsTcpSocket &socket, std::uint32_t columns,
                                                                        std::uint32_t rows,
                                                                        std::chrono::milliseconds timeout,
                                                                        const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError> closeTerminal(WindowsTcpSocket &socket,
                                                                       std::chrono::milliseconds timeout,
                                                                       const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] bool terminalOpen() const noexcept;

    [[nodiscard]] std::expected<void, SshTransportError> openSftp(WindowsTcpSocket &socket,
                                                                  std::chrono::milliseconds timeout,
                                                                  const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<std::string, SshTransportError>
    canonicalizeSftpPath(WindowsTcpSocket &socket, std::string_view remotePath, std::chrono::milliseconds timeout,
                         const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<std::vector<sftp::DirectoryEntry>, SshTransportError>
    listSftpDirectory(WindowsTcpSocket &socket, std::string_view remotePath, std::chrono::milliseconds timeout,
                      const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError>
    createSftpDirectory(WindowsTcpSocket &socket, std::string_view remotePath, std::chrono::milliseconds timeout,
                        const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError>
    renameSftpEntry(WindowsTcpSocket &socket, std::string_view sourcePath, std::string_view destinationPath,
                    SftpRenameDisposition disposition, std::chrono::milliseconds timeout,
                    const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError> removeSftpFile(WindowsTcpSocket &socket,
                                                                        std::string_view remotePath,
                                                                        std::chrono::milliseconds timeout,
                                                                        const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError>
    removeSftpDirectory(WindowsTcpSocket &socket, std::string_view remotePath, std::chrono::milliseconds timeout,
                        const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError>
    openSftpFileForRead(WindowsTcpSocket &socket, std::string_view remotePath, std::chrono::milliseconds timeout,
                        const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError>
    openSftpFileForWrite(WindowsTcpSocket &socket, std::string_view remotePath, SftpWriteDisposition disposition,
                         std::chrono::milliseconds timeout, const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<std::size_t, SshTransportError>
    readSftpFile(WindowsTcpSocket &socket, std::span<char> output, std::chrono::milliseconds timeout,
                 const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError> writeSftpFile(WindowsTcpSocket &socket,
                                                                       std::span<const char> input,
                                                                       std::chrono::milliseconds timeout,
                                                                       const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] std::expected<void, SshTransportError> closeSftpFile(WindowsTcpSocket &socket,
                                                                       std::chrono::milliseconds timeout,
                                                                       const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] bool sftpFileOpen() const noexcept;
    [[nodiscard]] std::expected<void, SshTransportError> closeSftp(WindowsTcpSocket &socket,
                                                                   std::chrono::milliseconds timeout,
                                                                   const std::stop_token &stopToken = {}) noexcept;
    [[nodiscard]] bool sftpOpen() const noexcept;

    // Auxiliary exec channels are advanced one non-blocking step at a time so
    // callers can interleave background queries with the interactive PTY.
    [[nodiscard]] std::expected<void, SshTransportError> startAuxiliaryCommand(std::string_view command) noexcept;
    [[nodiscard]] std::expected<AuxiliaryCommandPollResult, SshTransportError>
    pollAuxiliaryCommand(std::span<char> output) noexcept;
    void cancelAuxiliaryCommand() noexcept;
    [[nodiscard]] bool auxiliaryCommandActive() const noexcept;

private:
    enum class AuxiliaryCommandPhase : std::uint8_t
    {
        Idle,
        Opening,
        Requesting,
        Reading,
        Closing,
        Freeing,
    };

    Libssh2Session(std::unique_ptr<Libssh2Runtime> runtime, void *session) noexcept;

    std::unique_ptr<Libssh2Runtime> m_runtime;
    void *m_session = nullptr;
    void *m_terminalChannel = nullptr;
    void *m_auxiliaryChannel = nullptr;
    void *m_sftp = nullptr;
    void *m_sftpFile = nullptr;
    std::string m_auxiliaryCommand;
    AuxiliaryCommandPhase m_auxiliaryPhase = AuxiliaryCommandPhase::Idle;
    int m_auxiliaryExitStatus = 0;
    bool m_handshakeComplete = false;
    bool m_hostKeyVerified = false;
    bool m_authenticated = false;
};

} // namespace ztermy::ssh
