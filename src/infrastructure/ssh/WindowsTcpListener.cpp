#include "infrastructure/ssh/WindowsTcpListener.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace ztermy::ssh
{
namespace
{

using Clock = std::chrono::steady_clock;

class WinsockListenerRuntime final
{
public:
    WinsockListenerRuntime() noexcept
    {
        WSADATA data{};
        m_result = WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~WinsockListenerRuntime()
    {
        if (m_result == 0)
        {
            WSACleanup();
        }
    }

    [[nodiscard]] int result() const noexcept { return m_result; }

private:
    int m_result = WSASYSNOTREADY;
};

[[nodiscard]] int ensureWinsock() noexcept
{
    static const WinsockListenerRuntime runtime;
    return runtime.result();
}

[[nodiscard]] TcpListenError mapListenError(const int nativeCode) noexcept
{
    switch (nativeCode)
    {
        case WSAEADDRINUSE:
            return {.kind = TcpListenErrorKind::AddressInUse, .nativeCode = nativeCode};
        case WSAEACCES:
            return {.kind = TcpListenErrorKind::AccessDenied, .nativeCode = nativeCode};
        default:
            return {.kind = TcpListenErrorKind::SystemError, .nativeCode = nativeCode};
    }
}

[[nodiscard]] std::optional<std::wstring> utf8ToWide(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return std::nullopt;
    }
    const int sourceSize = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceSize, nullptr, 0);
    if (required <= 0)
    {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceSize, wide.data(), required) != required)
    {
        return std::nullopt;
    }
    return wide;
}

[[nodiscard]] std::uint16_t socketPort(const SOCKET socket) noexcept
{
    sockaddr_storage address{};
    int length = sizeof(address);
    if (getsockname(socket, reinterpret_cast<sockaddr *>(&address), &length) == SOCKET_ERROR)
    {
        return 0;
    }
    if (address.ss_family == AF_INET)
    {
        return ntohs(reinterpret_cast<const sockaddr_in *>(&address)->sin_port);
    }
    if (address.ss_family == AF_INET6)
    {
        return ntohs(reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_port);
    }
    return 0;
}

} // namespace

WindowsTcpListener::WindowsTcpListener(const std::uintptr_t socket, const std::uint16_t boundPort) noexcept
    : m_socket(socket), m_boundPort(boundPort)
{
}

WindowsTcpListener::~WindowsTcpListener()
{
    close();
}

WindowsTcpListener::WindowsTcpListener(WindowsTcpListener &&other) noexcept
    : m_socket(other.release()), m_boundPort(std::exchange(other.m_boundPort, 0))
{
}

WindowsTcpListener &WindowsTcpListener::operator=(WindowsTcpListener &&other) noexcept
{
    if (this != &other)
    {
        close();
        m_socket = other.release();
        m_boundPort = std::exchange(other.m_boundPort, 0);
    }
    return *this;
}

std::expected<WindowsTcpListener, TcpListenError>
WindowsTcpListener::listen(const std::string_view host, const std::uint16_t port, const int backlog) noexcept
{
    if (host.empty() || port == 0 || backlog < 1 || backlog > 128)
    {
        return std::unexpected(TcpListenError{.kind = TcpListenErrorKind::InvalidEndpoint});
    }
    const int winsockResult = ensureWinsock();
    if (winsockResult != 0)
    {
        return std::unexpected(mapListenError(winsockResult));
    }
    try
    {
        const auto wideHost = utf8ToWide(host);
        if (!wideHost)
        {
            return std::unexpected(TcpListenError{.kind = TcpListenErrorKind::InvalidEndpoint});
        }
        const std::wstring service = std::to_wstring(port);
        ADDRINFOW hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        ADDRINFOW *rawAddresses = nullptr;
        const int resolved = GetAddrInfoW(wideHost->c_str(), service.c_str(), &hints, &rawAddresses);
        if (resolved != 0 || rawAddresses == nullptr)
        {
            return std::unexpected(mapListenError(resolved));
        }
        const auto addressDeleter = [](ADDRINFOW *addresses) noexcept {
            FreeAddrInfoW(addresses);
        };
        const std::unique_ptr<ADDRINFOW, decltype(addressDeleter)> addresses(rawAddresses, addressDeleter);

        TcpListenError lastError{.kind = TcpListenErrorKind::SystemError};
        for (const ADDRINFOW *address = addresses.get(); address != nullptr; address = address->ai_next)
        {
            const SOCKET rawSocket = WSASocketW(address->ai_family, address->ai_socktype, address->ai_protocol, nullptr,
                                                0, WSA_FLAG_NO_HANDLE_INHERIT);
            if (rawSocket == INVALID_SOCKET)
            {
                lastError = mapListenError(WSAGetLastError());
                continue;
            }
            WindowsTcpListener listener(static_cast<std::uintptr_t>(rawSocket), 0);
            BOOL exclusive = TRUE;
            if (setsockopt(rawSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
                           sizeof(exclusive))
                == SOCKET_ERROR)
            {
                lastError = mapListenError(WSAGetLastError());
                continue;
            }
            u_long nonBlocking = 1;
            if (ioctlsocket(rawSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR)
            {
                lastError = mapListenError(WSAGetLastError());
                continue;
            }
            if (::bind(rawSocket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == SOCKET_ERROR
                || ::listen(rawSocket, backlog) == SOCKET_ERROR)
            {
                lastError = mapListenError(WSAGetLastError());
                continue;
            }
            listener.m_boundPort = socketPort(rawSocket);
            if (listener.m_boundPort == 0)
            {
                lastError = mapListenError(WSAGetLastError());
                continue;
            }
            return listener;
        }
        return std::unexpected(lastError);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(
            TcpListenError{.kind = TcpListenErrorKind::SystemError, .nativeCode = ERROR_NOT_ENOUGH_MEMORY});
    }
}

bool WindowsTcpListener::valid() const noexcept
{
    return m_socket != InvalidSocket;
}

std::uint16_t WindowsTcpListener::boundPort() const noexcept
{
    return m_boundPort;
}

std::expected<void, TcpListenError> WindowsTcpListener::waitForClient(const Clock::time_point deadline,
                                                                      const std::stop_token &stopToken) noexcept
{
    if (!valid())
    {
        return std::unexpected(TcpListenError{.kind = TcpListenErrorKind::SystemError, .nativeCode = WSAENOTSOCK});
    }
    while (!stopToken.stop_requested())
    {
        const Clock::time_point now = Clock::now();
        if (now >= deadline)
        {
            return std::unexpected(TcpListenError{.kind = TcpListenErrorKind::TimedOut});
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const int timeout = static_cast<int>((std::min)(remaining, std::chrono::milliseconds{25}).count());
        WSAPOLLFD descriptor{.fd = static_cast<SOCKET>(m_socket), .events = POLLRDNORM, .revents = 0};
        const int polled = WSAPoll(&descriptor, 1, (std::max)(timeout, 1));
        if (polled > 0 && (descriptor.revents & (POLLRDNORM | POLLIN)) != 0)
        {
            return {};
        }
        if (polled == SOCKET_ERROR)
        {
            const int nativeCode = WSAGetLastError();
            if (nativeCode != WSAEINTR)
            {
                return std::unexpected(mapListenError(nativeCode));
            }
        }
    }
    return std::unexpected(TcpListenError{.kind = TcpListenErrorKind::Cancelled});
}

std::expected<std::optional<WindowsTcpSocket>, TcpListenError> WindowsTcpListener::accept() noexcept
{
    if (!valid())
    {
        return std::unexpected(TcpListenError{.kind = TcpListenErrorKind::SystemError, .nativeCode = WSAENOTSOCK});
    }
    const SOCKET accepted = ::accept(static_cast<SOCKET>(m_socket), nullptr, nullptr);
    if (accepted == INVALID_SOCKET)
    {
        const int nativeCode = WSAGetLastError();
        if (nativeCode == WSAEWOULDBLOCK)
        {
            return std::optional<WindowsTcpSocket>{};
        }
        return std::unexpected(mapListenError(nativeCode));
    }
    WindowsTcpSocket socket(static_cast<std::uintptr_t>(accepted));
    u_long nonBlocking = 1;
    if (ioctlsocket(accepted, FIONBIO, &nonBlocking) == SOCKET_ERROR)
    {
        return std::unexpected(mapListenError(WSAGetLastError()));
    }
    if (!SetHandleInformation(reinterpret_cast<HANDLE>(accepted), HANDLE_FLAG_INHERIT, 0))
    {
        return std::unexpected(
            TcpListenError{.kind = TcpListenErrorKind::SystemError, .nativeCode = static_cast<int>(GetLastError())});
    }
    return std::optional<WindowsTcpSocket>{std::move(socket)};
}

void WindowsTcpListener::close() noexcept
{
    const std::uintptr_t socket = release();
    if (socket != InvalidSocket)
    {
        closesocket(static_cast<SOCKET>(socket));
    }
    m_boundPort = 0;
}

std::uintptr_t WindowsTcpListener::release() noexcept
{
    return std::exchange(m_socket, InvalidSocket);
}

} // namespace ztermy::ssh
