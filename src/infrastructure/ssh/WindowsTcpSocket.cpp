#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace
{

using Clock = std::chrono::steady_clock;
using ztermy::ssh::SocketIoInterest;
using ztermy::ssh::TcpConnectError;
using ztermy::ssh::TcpConnectErrorKind;

class WinsockRuntime final
{
public:
    WinsockRuntime() noexcept
    {
        WSADATA data{};
        m_result = WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~WinsockRuntime()
    {
        if (m_result == 0)
        {
            WSACleanup();
        }
    }

    WinsockRuntime(const WinsockRuntime &) = delete;
    WinsockRuntime &operator=(const WinsockRuntime &) = delete;

    [[nodiscard]] int result() const noexcept { return m_result; }

private:
    int m_result = WSASYSNOTREADY;
};

[[nodiscard]] int ensureWinsock() noexcept
{
    static WinsockRuntime runtime;
    return runtime.result();
}

class UniqueEvent final
{
public:
    explicit UniqueEvent(const HANDLE event) noexcept : m_event(event) {}
    ~UniqueEvent()
    {
        if (m_event != nullptr && m_event != WSA_INVALID_EVENT)
        {
            CloseHandle(m_event);
        }
    }

    UniqueEvent(const UniqueEvent &) = delete;
    UniqueEvent &operator=(const UniqueEvent &) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return m_event; }

private:
    HANDLE m_event = nullptr;
};

class SocketEventSelection final
{
public:
    explicit SocketEventSelection(const SOCKET socket) noexcept : m_socket(socket) {}
    ~SocketEventSelection()
    {
        if (m_active)
        {
            WSAEventSelect(m_socket, nullptr, 0);
        }
    }

    SocketEventSelection(const SocketEventSelection &) = delete;
    SocketEventSelection &operator=(const SocketEventSelection &) = delete;

    [[nodiscard]] bool activate(const WSAEVENT event, const long networkEvents) noexcept
    {
        m_active = WSAEventSelect(m_socket, event, networkEvents) != SOCKET_ERROR;
        return m_active;
    }

private:
    SOCKET m_socket = INVALID_SOCKET;
    bool m_active = false;
};

struct AddrInfoDeleter final
{
    void operator()(ADDRINFOEXW *addresses) const noexcept
    {
        if (addresses != nullptr)
        {
            FreeAddrInfoExW(addresses);
        }
    }
};

using UniqueAddrInfo = std::unique_ptr<ADDRINFOEXW, AddrInfoDeleter>;

[[nodiscard]] constexpr TcpConnectError mapSocketError(const int error) noexcept
{
    switch (error)
    {
        case WSAECONNREFUSED:
            return {.kind = TcpConnectErrorKind::ConnectionRefused, .nativeCode = error};
        case WSAETIMEDOUT:
            return {.kind = TcpConnectErrorKind::TimedOut, .nativeCode = error};
        case WSAENETUNREACH:
        case WSAEHOSTUNREACH:
            return {.kind = TcpConnectErrorKind::NetworkUnreachable, .nativeCode = error};
        default:
            return {.kind = TcpConnectErrorKind::SystemError, .nativeCode = error};
    }
}

static_assert(mapSocketError(WSAECONNREFUSED).kind == TcpConnectErrorKind::ConnectionRefused);
static_assert(mapSocketError(WSAETIMEDOUT).kind == TcpConnectErrorKind::TimedOut);
static_assert(mapSocketError(WSAENETUNREACH).kind == TcpConnectErrorKind::NetworkUnreachable);
static_assert(mapSocketError(WSAEHOSTUNREACH).kind == TcpConnectErrorKind::NetworkUnreachable);

[[nodiscard]] std::expected<std::wstring, TcpConnectError> utf8ToWide(const std::string_view text)
{
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::InvalidEndpoint});
    }

    const int inputLength = static_cast<int>(text.size());
    // MultiByteToWideChar receives the explicit byte count; string_view need not be null terminated.
    // NOLINTBEGIN(bugprone-suspicious-stringview-data-usage)
    const int requiredLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputLength, nullptr, 0);
    if (requiredLength <= 0)
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::InvalidEndpoint,
                                               .nativeCode = static_cast<int>(GetLastError())});
    }

    std::wstring result(static_cast<std::size_t>(requiredLength), L'\0');
    const int convertedLength =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputLength, result.data(), requiredLength);
    // NOLINTEND(bugprone-suspicious-stringview-data-usage)
    if (convertedLength != requiredLength)
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::InvalidEndpoint,
                                               .nativeCode = static_cast<int>(GetLastError())});
    }

    return result;
}

enum class WaitOutcome : std::uint8_t
{
    Ready,
    TimedOut,
    Cancelled,
    Failed,
};

struct WaitResult final
{
    WaitOutcome outcome = WaitOutcome::Failed;
    int nativeCode = 0;
};

[[nodiscard]] DWORD remainingWaitSlice(const Clock::time_point deadline) noexcept
{
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (remaining <= std::chrono::milliseconds::zero())
    {
        return 0;
    }

    constexpr auto cancellationSlice = std::chrono::milliseconds{25};
    return static_cast<DWORD>((std::min)(remaining, cancellationSlice).count());
}

[[nodiscard]] WaitResult waitForEvent(const HANDLE event, const Clock::time_point deadline,
                                      const std::stop_token &stopToken) noexcept
{
    while (true)
    {
        if (stopToken.stop_requested())
        {
            return {.outcome = WaitOutcome::Cancelled};
        }

        const DWORD waitMilliseconds = remainingWaitSlice(deadline);
        if (waitMilliseconds == 0)
        {
            return {.outcome = WaitOutcome::TimedOut};
        }

        const DWORD result = WaitForSingleObject(event, waitMilliseconds);
        if (result == WAIT_OBJECT_0)
        {
            return {.outcome = WaitOutcome::Ready};
        }
        if (result == WAIT_FAILED)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = static_cast<int>(GetLastError())};
        }
    }
}

[[nodiscard]] std::expected<UniqueAddrInfo, TcpConnectError> resolveAddresses(const std::wstring &host,
                                                                              const std::uint16_t port,
                                                                              const Clock::time_point deadline,
                                                                              const std::stop_token &stopToken)
{
    ADDRINFOEXW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    UniqueEvent completionEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (completionEvent.get() == nullptr)
    {
        return std::unexpected(
            TcpConnectError{.kind = TcpConnectErrorKind::SystemError, .nativeCode = static_cast<int>(GetLastError())});
    }

    WSAOVERLAPPED overlapped{};
    overlapped.hEvent = completionEvent.get();
    PADDRINFOEXW rawAddresses = nullptr;
    HANDLE cancelHandle = nullptr;
    const std::wstring service = std::to_wstring(port);

    int result = GetAddrInfoExW(host.c_str(), service.c_str(), NS_DNS, nullptr, &hints, &rawAddresses, nullptr,
                                &overlapped, nullptr, &cancelHandle);
    if (result == WSA_IO_PENDING)
    {
        const WaitResult waitResult = waitForEvent(completionEvent.get(), deadline, stopToken);
        if (waitResult.outcome != WaitOutcome::Ready)
        {
            if (cancelHandle != nullptr)
            {
                GetAddrInfoExCancel(&cancelHandle);
            }
            WaitForSingleObject(completionEvent.get(), INFINITE);
            UniqueAddrInfo discardedAddresses(rawAddresses);

            if (waitResult.outcome == WaitOutcome::Cancelled)
            {
                return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::Cancelled});
            }
            if (waitResult.outcome == WaitOutcome::TimedOut)
            {
                return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::TimedOut});
            }
            return std::unexpected(
                TcpConnectError{.kind = TcpConnectErrorKind::SystemError, .nativeCode = waitResult.nativeCode});
        }
        result = GetAddrInfoExOverlappedResult(&overlapped);
    }

    UniqueAddrInfo addresses(rawAddresses);
    if (result != 0 || !addresses)
    {
        return std::unexpected(
            TcpConnectError{.kind = TcpConnectErrorKind::NameResolutionFailed, .nativeCode = result});
    }
    return addresses;
}

[[nodiscard]] WaitResult waitForConnect(const SOCKET socket, const Clock::time_point deadline,
                                        const std::stop_token &stopToken) noexcept
{
    while (true)
    {
        if (stopToken.stop_requested())
        {
            return {.outcome = WaitOutcome::Cancelled};
        }

        const DWORD waitMilliseconds = remainingWaitSlice(deadline);
        if (waitMilliseconds == 0)
        {
            return {.outcome = WaitOutcome::TimedOut};
        }

        WSAPOLLFD descriptor{
            .fd = socket,
            .events = POLLWRNORM,
            .revents = 0,
        };
        const int result = WSAPoll(&descriptor, 1, static_cast<int>(waitMilliseconds));
        if (result == SOCKET_ERROR)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = WSAGetLastError()};
        }
        if (result == 0)
        {
            continue;
        }

        int socketError = 0;
        int socketErrorLength = sizeof(socketError);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socketError), &socketErrorLength)
            == SOCKET_ERROR)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = WSAGetLastError()};
        }
        if (socketError != 0)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = socketError};
        }
        if ((descriptor.revents & POLLNVAL) != 0)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = WSAENOTSOCK};
        }
        return {.outcome = WaitOutcome::Ready};
    }
}

[[nodiscard]] WaitResult waitForInterruptibleIo(const SOCKET socket, const SocketIoInterest interest,
                                                const Clock::time_point deadline, const std::stop_token &stopToken,
                                                const std::uintptr_t interruptEvent) noexcept
{
    long networkEvents = FD_CLOSE;
    if (interest == SocketIoInterest::Read || interest == SocketIoInterest::ReadWrite)
    {
        networkEvents |= FD_READ;
    }
    if (interest == SocketIoInterest::Write || interest == SocketIoInterest::ReadWrite)
    {
        networkEvents |= FD_WRITE;
    }

    UniqueEvent socketEvent(WSACreateEvent());
    if (socketEvent.get() == WSA_INVALID_EVENT)
    {
        return {.outcome = WaitOutcome::Failed, .nativeCode = WSAGetLastError()};
    }

    UniqueEvent stopEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (stopEvent.get() == nullptr)
    {
        return {.outcome = WaitOutcome::Failed, .nativeCode = static_cast<int>(GetLastError())};
    }

    SocketEventSelection selection(socket);
    if (!selection.activate(socketEvent.get(), networkEvents))
    {
        return {.outcome = WaitOutcome::Failed, .nativeCode = WSAGetLastError()};
    }

    const HANDLE stopEventHandle = stopEvent.get();
    std::stop_callback stopCallback(stopToken, [stopEventHandle] {
        SetEvent(stopEventHandle);
    });
    const std::array handles{
        socketEvent.get(),
        reinterpret_cast<HANDLE>(interruptEvent), // NOLINT(performance-no-int-to-ptr)
        stopEvent.get(),
    };

    while (true)
    {
        const DWORD waitMilliseconds = remainingWaitSlice(deadline);
        if (waitMilliseconds == 0)
        {
            return {.outcome = WaitOutcome::TimedOut};
        }

        const DWORD result =
            WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, waitMilliseconds);
        if (result == WAIT_OBJECT_0 + 1U || result == WAIT_OBJECT_0 + 2U)
        {
            return {.outcome = WaitOutcome::Cancelled};
        }
        if (result == WAIT_FAILED)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = static_cast<int>(GetLastError())};
        }
        if (result != WAIT_OBJECT_0)
        {
            continue;
        }

        WSANETWORKEVENTS occurred{};
        if (WSAEnumNetworkEvents(socket, socketEvent.get(), &occurred) == SOCKET_ERROR)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = WSAGetLastError()};
        }
        for (const int error : occurred.iErrorCode)
        {
            if (error != 0)
            {
                return {.outcome = WaitOutcome::Failed, .nativeCode = error};
            }
        }
        return {.outcome = WaitOutcome::Ready};
    }
}

[[nodiscard]] WaitResult waitForIo(const SOCKET socket, const SocketIoInterest interest,
                                   const Clock::time_point deadline, const std::stop_token &stopToken,
                                   const std::uintptr_t interruptEvent) noexcept
{
    if (interruptEvent != 0)
    {
        return waitForInterruptibleIo(socket, interest, deadline, stopToken, interruptEvent);
    }

    short events = 0;
    if (interest == SocketIoInterest::Read || interest == SocketIoInterest::ReadWrite)
    {
        events |= POLLRDNORM;
    }
    if (interest == SocketIoInterest::Write || interest == SocketIoInterest::ReadWrite)
    {
        events |= POLLWRNORM;
    }

    while (true)
    {
        if (stopToken.stop_requested())
        {
            return {.outcome = WaitOutcome::Cancelled};
        }

        const DWORD waitMilliseconds = remainingWaitSlice(deadline);
        if (waitMilliseconds == 0)
        {
            return {.outcome = WaitOutcome::TimedOut};
        }

        WSAPOLLFD descriptor{
            .fd = socket,
            .events = events,
            .revents = 0,
        };
        const int result = WSAPoll(&descriptor, 1, static_cast<int>(waitMilliseconds));
        if (result == SOCKET_ERROR)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = WSAGetLastError()};
        }
        if (result == 0)
        {
            continue;
        }

        if ((descriptor.revents & POLLNVAL) != 0)
        {
            return {.outcome = WaitOutcome::Failed, .nativeCode = WSAENOTSOCK};
        }
        if ((descriptor.revents & (POLLERR | POLLHUP)) != 0)
        {
            int socketError = 0;
            int socketErrorLength = sizeof(socketError);
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socketError), &socketErrorLength)
                == SOCKET_ERROR)
            {
                socketError = WSAGetLastError();
            }
            return {.outcome = WaitOutcome::Failed, .nativeCode = socketError == 0 ? WSAECONNRESET : socketError};
        }
        return {.outcome = WaitOutcome::Ready};
    }
}

} // namespace

namespace ztermy::ssh
{

WindowsWaitEvent::WindowsWaitEvent() noexcept
    : m_event(reinterpret_cast<std::uintptr_t>(CreateEventW(nullptr, TRUE, FALSE, nullptr)))
{
}

WindowsWaitEvent::~WindowsWaitEvent()
{
    if (valid())
    {
        CloseHandle(reinterpret_cast<HANDLE>(m_event)); // NOLINT(performance-no-int-to-ptr)
    }
}

bool WindowsWaitEvent::valid() const noexcept
{
    return m_event != 0;
}

std::uintptr_t WindowsWaitEvent::nativeHandle() const noexcept
{
    return m_event;
}

bool WindowsWaitEvent::signal() noexcept
{
    return valid() && SetEvent(reinterpret_cast<HANDLE>(m_event)) != FALSE; // NOLINT(performance-no-int-to-ptr)
}

bool WindowsWaitEvent::reset() noexcept
{
    return valid() && ResetEvent(reinterpret_cast<HANDLE>(m_event)) != FALSE; // NOLINT(performance-no-int-to-ptr)
}

WindowsTcpSocket::WindowsTcpSocket(const std::uintptr_t socket) noexcept : m_socket(socket) {}

WindowsTcpSocket::~WindowsTcpSocket()
{
    close();
}

WindowsTcpSocket::WindowsTcpSocket(WindowsTcpSocket &&other) noexcept : m_socket(other.release()) {}

WindowsTcpSocket &WindowsTcpSocket::operator=(WindowsTcpSocket &&other) noexcept
{
    if (this != &other)
    {
        close();
        m_socket = other.release();
    }
    return *this;
}

std::expected<WindowsTcpSocket, TcpConnectError> WindowsTcpSocket::connect(const std::string_view host,
                                                                           const std::uint16_t port,
                                                                           const std::chrono::milliseconds timeout,
                                                                           const std::stop_token &stopToken) noexcept
{
    if (port == 0)
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::InvalidEndpoint});
    }
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::TimedOut});
    }
    if (stopToken.stop_requested())
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::Cancelled});
    }

    const int winsockResult = ensureWinsock();
    if (winsockResult != 0)
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::SystemError, .nativeCode = winsockResult});
    }

    try
    {
        auto wideHost = utf8ToWide(host);
        if (!wideHost)
        {
            return std::unexpected(wideHost.error());
        }

        const Clock::time_point deadline = Clock::now() + timeout;
        auto addresses = resolveAddresses(*wideHost, port, deadline, stopToken);
        if (!addresses)
        {
            return std::unexpected(addresses.error());
        }

        TcpConnectError lastError{.kind = TcpConnectErrorKind::NetworkUnreachable};
        for (const ADDRINFOEXW *address = addresses->get(); address != nullptr; address = address->ai_next)
        {
            if (stopToken.stop_requested())
            {
                return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::Cancelled});
            }
            if (Clock::now() >= deadline)
            {
                return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::TimedOut});
            }

            const SOCKET rawSocket = WSASocketW(address->ai_family, address->ai_socktype, address->ai_protocol, nullptr,
                                                0, WSA_FLAG_NO_HANDLE_INHERIT);
            if (rawSocket == (std::numeric_limits<SOCKET>::max)())
            {
                lastError = mapSocketError(WSAGetLastError());
                continue;
            }
            WindowsTcpSocket socket(static_cast<std::uintptr_t>(rawSocket));

            u_long nonBlocking = 1;
            if (ioctlsocket(rawSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR)
            {
                lastError = mapSocketError(WSAGetLastError());
                continue;
            }

            if (::connect(rawSocket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
            {
                return socket;
            }

            const int connectError = WSAGetLastError();
            if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS)
            {
                lastError = mapSocketError(connectError);
                continue;
            }

            const WaitResult waitResult = waitForConnect(rawSocket, deadline, stopToken);
            if (waitResult.outcome == WaitOutcome::Ready)
            {
                return socket;
            }
            if (waitResult.outcome == WaitOutcome::Cancelled)
            {
                return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::Cancelled});
            }
            if (waitResult.outcome == WaitOutcome::TimedOut)
            {
                return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::TimedOut});
            }
            lastError = mapSocketError(waitResult.nativeCode);
        }
        return std::unexpected(lastError);
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(TcpConnectError{
            .kind = TcpConnectErrorKind::SystemError,
            .nativeCode = ERROR_NOT_ENOUGH_MEMORY,
        });
    }
}

bool WindowsTcpSocket::valid() const noexcept
{
    return m_socket != InvalidSocket;
}

std::uintptr_t WindowsTcpSocket::nativeHandle() const noexcept
{
    return m_socket;
}

std::expected<void, TcpConnectError>
WindowsTcpSocket::waitUntilReady(const SocketIoInterest interest, const std::chrono::steady_clock::time_point deadline,
                                 const std::stop_token &stopToken, const std::uintptr_t interruptEvent) const noexcept
{
    if (!valid())
    {
        return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::SystemError, .nativeCode = WSAENOTSOCK});
    }

    const WaitResult waitResult =
        waitForIo(static_cast<SOCKET>(m_socket), interest, deadline, stopToken, interruptEvent);
    switch (waitResult.outcome)
    {
        case WaitOutcome::Ready:
            return {};
        case WaitOutcome::TimedOut:
            return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::TimedOut});
        case WaitOutcome::Cancelled:
            return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::Cancelled});
        case WaitOutcome::Failed:
            return std::unexpected(mapSocketError(waitResult.nativeCode));
    }
    return std::unexpected(TcpConnectError{.kind = TcpConnectErrorKind::SystemError});
}

void WindowsTcpSocket::close() noexcept
{
    const std::uintptr_t socket = release();
    if (socket != InvalidSocket)
    {
        closesocket(static_cast<SOCKET>(socket));
    }
}

std::uintptr_t WindowsTcpSocket::release() noexcept
{
    return std::exchange(m_socket, InvalidSocket);
}

} // namespace ztermy::ssh
