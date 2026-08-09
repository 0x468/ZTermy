#include "application/forwarding/PortForwardingJob.h"

#include "domain/forwarding/Socks5Protocol.h"
#include "infrastructure/ssh/WindowsTcpListener.h"
#include "infrastructure/ssh/WindowsTcpSocket.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace ztermy::forwarding
{
namespace
{

constexpr std::size_t maximumBufferedBytesPerDirection = std::size_t{256} * 1'024U;
constexpr std::size_t transferBufferSize = std::size_t{32} * 1'024U;
constexpr auto channelOperationTimeout = 3s;
constexpr auto idlePollInterval = 8ms;

class ByteQueue final
{
public:
    [[nodiscard]] std::size_t size() const noexcept { return m_bytes.size() - m_offset; }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] std::span<const char> view() const noexcept { return std::span(m_bytes).subspan(m_offset); }

    [[nodiscard]] bool append(const std::span<const char> bytes) noexcept
    {
        if (bytes.size() > maximumBufferedBytesPerDirection - size())
        {
            return false;
        }
        try
        {
            compact();
            m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
            return true;
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
    }

    [[nodiscard]] bool append(const std::span<const std::byte> bytes) noexcept
    {
        return append(std::span(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    }

    void consume(const std::size_t count) noexcept
    {
        m_offset += (std::min)(count, size());
        if (m_offset == m_bytes.size())
        {
            m_bytes.clear();
            m_offset = 0;
        }
    }

private:
    void compact()
    {
        if (m_offset == 0)
        {
            return;
        }
        if (m_offset >= m_bytes.size() / 2)
        {
            m_bytes.erase(m_bytes.begin(), m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset));
            m_offset = 0;
        }
    }

    std::vector<char> m_bytes;
    std::size_t m_offset = 0;
};

struct ForwardingClient final
{
    ssh::WindowsTcpSocket socket;
    std::optional<ssh::SshForwardingChannel> channel;
    std::optional<Socks5Handshake> socksHandshake;
    ByteQueue toRemote;
    ByteQueue toClient;
    bool clientReadClosed = false;
    bool clientWriteClosed = false;
    bool channelEofSent = false;
    bool closeAfterWrite = false;
};

[[nodiscard]] bool wouldBlock(const ssh::SshByteTransportError &error) noexcept
{
    return error.kind == ssh::SshByteTransportErrorKind::WouldBlock;
}

[[nodiscard]] bool closed(const ssh::SshByteTransportError &error) noexcept
{
    return error.kind == ssh::SshByteTransportErrorKind::Closed;
}

void rejectSocksClient(ForwardingClient &client, const Socks5ReplyCode replyCode) noexcept
{
    const auto reply = Socks5Handshake::reply(replyCode);
    if (!client.toClient.append(reply))
    {
        client.socket.close();
    }
    client.clientReadClosed = true;
    client.closeAfterWrite = true;
}

[[nodiscard]] bool flushClientOutput(ForwardingClient &client, std::uint64_t &bytesToClients) noexcept
{
    while (!client.toClient.empty())
    {
        auto written = client.socket.write(client.toClient.view());
        if (written)
        {
            if (*written == 0)
            {
                return false;
            }
            client.toClient.consume(*written);
            bytesToClients += *written;
            continue;
        }
        if (wouldBlock(written.error()))
        {
            return true;
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool pumpEstablishedClient(ForwardingClient &client, ssh::AuthenticatedSshConnection &connection,
                                         std::uint64_t &bytesFromClients, std::uint64_t &bytesToClients,
                                         const std::stop_token &stopToken) noexcept
{
    std::array<char, transferBufferSize> buffer{};
    if (!client.clientReadClosed && client.toRemote.size() < maximumBufferedBytesPerDirection)
    {
        auto read = client.socket.read(buffer);
        if (read)
        {
            if (*read == 0)
            {
                client.clientReadClosed = true;
            }
            else if (!client.toRemote.append(std::span(buffer).first(*read)))
            {
                return false;
            }
            else
            {
                bytesFromClients += *read;
            }
        }
        else if (closed(read.error()))
        {
            client.clientReadClosed = true;
        }
        else if (!wouldBlock(read.error()))
        {
            return false;
        }
    }

    while (!client.toRemote.empty())
    {
        auto written = connection.session->writeForwardingChannel(*client.channel, client.toRemote.view());
        if (written)
        {
            if (*written == 0)
            {
                return false;
            }
            client.toRemote.consume(*written);
            continue;
        }
        if (wouldBlock(written.error()))
        {
            break;
        }
        return false;
    }

    if (client.clientReadClosed && client.toRemote.empty() && !client.channelEofSent)
    {
        auto sent =
            connection.session->sendForwardingChannelEof(*connection.transport, *client.channel, 100ms, stopToken);
        if (sent)
        {
            client.channelEofSent = true;
        }
        else if (sent.error().kind != ssh::SshTransportErrorKind::TimedOut)
        {
            return false;
        }
    }

    while (client.toClient.size() < maximumBufferedBytesPerDirection)
    {
        auto read = connection.session->readForwardingChannel(*client.channel, buffer);
        if (read)
        {
            if (*read == 0)
            {
                break;
            }
            if (!client.toClient.append(std::span(buffer).first(*read)))
            {
                return false;
            }
            continue;
        }
        if (wouldBlock(read.error()))
        {
            break;
        }
        if (closed(read.error()))
        {
            break;
        }
        return false;
    }

    if (!flushClientOutput(client, bytesToClients))
    {
        return false;
    }
    if (connection.session->forwardingChannelEof(*client.channel) && client.toClient.empty()
        && !client.clientWriteClosed)
    {
        auto shutdown = client.socket.shutdownWrite();
        if (!shutdown && !closed(shutdown.error()))
        {
            return false;
        }
        client.clientWriteClosed = true;
    }
    return !(client.clientReadClosed && client.clientWriteClosed && client.toRemote.empty() && client.toClient.empty());
}

void closeClient(ForwardingClient &client, ssh::AuthenticatedSshConnection &connection) noexcept
{
    if (client.channel)
    {
        [[maybe_unused]] const auto closeResult =
            connection.session->closeForwardingChannel(*connection.transport, *client.channel, 100ms);
        client.channel.reset();
    }
    client.socket.close();
}

[[nodiscard]] bool completeSocksHandshake(ForwardingClient &client, ssh::AuthenticatedSshConnection &connection,
                                          std::uint64_t &rejectedClients, const std::stop_token &stopToken) noexcept
{
    std::array<char, 1'024> buffer{};
    auto read = client.socket.read(buffer);
    if (!read)
    {
        if (wouldBlock(read.error()))
        {
            return true;
        }
        return false;
    }
    if (*read == 0)
    {
        return false;
    }
    Socks5Handshake *handshake = client.socksHandshake ? &*client.socksHandshake : nullptr;
    if (handshake == nullptr)
    {
        return false;
    }
    auto consumed = handshake->consume(std::as_bytes(std::span(buffer).first(*read)));
    while (true)
    {
        if (!consumed)
        {
            ++rejectedClients;
            rejectSocksClient(client, Socks5ReplyCode::GeneralFailure);
            return true;
        }
        if (!consumed->response.empty() && !client.toClient.append(consumed->response))
        {
            return false;
        }
        if (consumed->status == Socks5HandshakeStatus::Rejected)
        {
            ++rejectedClients;
            client.clientReadClosed = true;
            client.closeAfterWrite = true;
            return true;
        }
        if (consumed->status == Socks5HandshakeStatus::DestinationReady)
        {
            if (!consumed->destination)
            {
                ++rejectedClients;
                rejectSocksClient(client, Socks5ReplyCode::GeneralFailure);
                return true;
            }
            const Socks5Destination &destination = *consumed->destination;
            auto channel =
                connection.session->openForwardingChannel(*connection.transport, destination.host, destination.port,
                                                          "127.0.0.1", 0, channelOperationTimeout, stopToken);
            if (!channel)
            {
                ++rejectedClients;
                rejectSocksClient(client, Socks5ReplyCode::HostUnreachable);
                return true;
            }
            client.channel = *channel;
            client.socksHandshake.reset();
            const auto success = Socks5Handshake::reply(Socks5ReplyCode::Succeeded);
            return client.toClient.append(success) && client.toRemote.append(consumed->remainingData);
        }
        if (consumed->status == Socks5HandshakeStatus::NeedMore)
        {
            return true;
        }
        consumed = handshake->consume({});
    }
}

[[nodiscard]] bool processClients(std::vector<ForwardingClient> &clients, ssh::AuthenticatedSshConnection &connection,
                                  std::uint64_t &bytesFromClients, std::uint64_t &bytesToClients,
                                  std::uint64_t &rejectedClients, const std::stop_token &stopToken) noexcept
{
    for (auto client = clients.begin(); client != clients.end();)
    {
        bool keep = true;
        if (client->socksHandshake)
        {
            keep = completeSocksHandshake(*client, connection, rejectedClients, stopToken);
            if (keep)
            {
                keep = flushClientOutput(*client, bytesToClients);
            }
            if (keep && client->closeAfterWrite && client->toClient.empty())
            {
                keep = false;
            }
        }
        else if (client->channel)
        {
            keep = pumpEstablishedClient(*client, connection, bytesFromClients, bytesToClients, stopToken);
        }
        if (!keep)
        {
            closeClient(*client, connection);
            client = clients.erase(client);
        }
        else
        {
            ++client;
        }
    }
    return true;
}

} // namespace

PortForwardingJob::~PortForwardingJob()
{
    stop();
}

std::expected<void, PortForwardingJobStartError> PortForwardingJob::start(PortForwardingRule rule,
                                                                          ssh::SshConnectionRequest request,
                                                                          ssh::SshConnectionCallbacks callbacks,
                                                                          SnapshotCallback snapshotCallback) noexcept
{
    if (!validPortForwardingRule(rule) || !ssh::validSshConnectionRequest(request))
    {
        return std::unexpected(PortForwardingJobStartError::InvalidConfiguration);
    }
    {
        std::scoped_lock lock(m_mutex);
        if (m_worker.joinable())
        {
            return std::unexpected(PortForwardingJobStartError::AlreadyRunning);
        }
        m_snapshot = PortForwardingJobSnapshot{.state = PortForwardingJobState::Starting};
        try
        {
            m_snapshotCallback = std::make_shared<const SnapshotCallback>(std::move(snapshotCallback));
        }
        catch (const std::bad_alloc &)
        {
            m_snapshot = {};
            return std::unexpected(PortForwardingJobStartError::WorkerUnavailable);
        }
    }
    publish(PortForwardingJobState::Starting, PortForwardingJobFailure::None);
    try
    {
        m_worker = std::jthread([this, rule = std::move(rule), request = std::move(request),
                                 callbacks = std::move(callbacks)](const std::stop_token &stopToken) mutable {
            run(rule, request, callbacks, stopToken);
        });
    }
    catch (const std::system_error &)
    {
        publish(PortForwardingJobState::Failed, PortForwardingJobFailure::ResourceLimit);
        return std::unexpected(PortForwardingJobStartError::WorkerUnavailable);
    }
    return {};
}

void PortForwardingJob::stop() noexcept
{
    if (m_worker.joinable())
    {
        m_worker.request_stop();
        if (m_worker.get_id() != std::this_thread::get_id())
        {
            m_worker.join();
        }
    }
}

PortForwardingJobSnapshot PortForwardingJob::snapshot() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_snapshot;
}

void PortForwardingJob::publish(const PortForwardingJobState state, const PortForwardingJobFailure failure) noexcept
{
    std::shared_ptr<const SnapshotCallback> callback;
    PortForwardingJobSnapshot value;
    {
        std::scoped_lock lock(m_mutex);
        m_snapshot.state = state;
        m_snapshot.failure = failure;
        if (state != PortForwardingJobState::Running)
        {
            m_snapshot.activeClients = 0;
        }
        value = m_snapshot;
        callback = m_snapshotCallback;
    }
    if (callback && *callback)
    {
        try
        {
            (*callback)(value);
        }
        catch (...)
        {
            return;
        }
    }
}

void PortForwardingJob::publishCounters(const std::size_t activeClients) noexcept
{
    std::shared_ptr<const SnapshotCallback> callback;
    PortForwardingJobSnapshot value;
    {
        std::scoped_lock lock(m_mutex);
        m_snapshot.activeClients = activeClients;
        value = m_snapshot;
        callback = m_snapshotCallback;
    }
    if (callback && *callback)
    {
        try
        {
            (*callback)(value);
        }
        catch (...)
        {
            return;
        }
    }
}

void PortForwardingJob::run(const PortForwardingRule &rule, ssh::SshConnectionRequest &request,
                            const ssh::SshConnectionCallbacks &callbacks, const std::stop_token &stopToken) noexcept
{
    try
    {
        auto connection = ssh::establishAuthenticatedSshConnection(request, callbacks, stopToken);
        if (!connection)
        {
            publish(stopToken.stop_requested() ? PortForwardingJobState::Stopped : PortForwardingJobState::Failed,
                    stopToken.stop_requested() ? PortForwardingJobFailure::None : PortForwardingJobFailure::Connection);
            return;
        }

        std::optional<ssh::WindowsTcpListener> localListener;
        std::optional<ssh::SshRemoteForwardListener> remoteListener;
        if (rule.type == PortForwardingType::Remote)
        {
            auto opened = connection->session->openRemoteForwardListener(
                *connection->transport, rule.bind.host, rule.bind.port,
                static_cast<std::uint32_t>(maximumPortForwardingClientsPerRule), channelOperationTimeout, stopToken);
            if (!opened)
            {
                publish(PortForwardingJobState::Failed, PortForwardingJobFailure::RemoteListener);
                return;
            }
            remoteListener = *opened;
        }
        else
        {
            auto opened = ssh::WindowsTcpListener::listen(rule.bind.host, rule.bind.port,
                                                          static_cast<int>(maximumPortForwardingClientsPerRule));
            if (!opened)
            {
                publish(PortForwardingJobState::Failed, PortForwardingJobFailure::Listener);
                return;
            }
            localListener = std::move(*opened);
        }

        publish(PortForwardingJobState::Running, PortForwardingJobFailure::None);
        std::vector<ForwardingClient> clients;
        clients.reserve(maximumPortForwardingClientsPerRule);
        std::uint64_t bytesFromClients = 0;
        std::uint64_t bytesToClients = 0;
        std::uint64_t rejectedClients = 0;
        std::size_t publishedClientCount = 0;
        auto nextCounterPublish = std::chrono::steady_clock::now();

        while (!stopToken.stop_requested())
        {
            bool listenerFailed = false;
            if (localListener)
            {
                while (clients.size() < maximumPortForwardingClientsPerRule)
                {
                    auto accepted = localListener->accept();
                    if (!accepted)
                    {
                        listenerFailed = true;
                        break;
                    }
                    if (!*accepted)
                    {
                        break;
                    }
                    ForwardingClient client{.socket = std::move(**accepted)};
                    if (rule.type == PortForwardingType::Dynamic)
                    {
                        client.socksHandshake.emplace();
                    }
                    else
                    {
                        auto channel = connection->session->openForwardingChannel(
                            *connection->transport, rule.destination.host, rule.destination.port, "127.0.0.1", 0,
                            channelOperationTimeout, stopToken);
                        if (!channel)
                        {
                            ++rejectedClients;
                            continue;
                        }
                        client.channel = *channel;
                    }
                    clients.push_back(std::move(client));
                }
            }
            else if (remoteListener)
            {
                while (clients.size() < maximumPortForwardingClientsPerRule)
                {
                    auto accepted = connection->session->acceptRemoteForwardingChannel(*remoteListener);
                    if (!accepted)
                    {
                        listenerFailed = true;
                        break;
                    }
                    if (!*accepted)
                    {
                        break;
                    }
                    auto socket = ssh::WindowsTcpSocket::connect(rule.destination.host, rule.destination.port,
                                                                 channelOperationTimeout, stopToken);
                    if (!socket)
                    {
                        ++rejectedClients;
                        [[maybe_unused]] const auto closeResult = connection->session->closeForwardingChannel(
                            *connection->transport, **accepted, 100ms, stopToken);
                        continue;
                    }
                    ForwardingClient client{.socket = std::move(*socket)};
                    client.channel = **accepted;
                    clients.push_back(std::move(client));
                }
            }

            if (listenerFailed)
            {
                for (auto &client : clients)
                {
                    closeClient(client, *connection);
                }
                publish(PortForwardingJobState::Failed,
                        localListener ? PortForwardingJobFailure::Listener : PortForwardingJobFailure::RemoteListener);
                return;
            }

            (void)processClients(clients, *connection, bytesFromClients, bytesToClients, rejectedClients, stopToken);
            {
                std::scoped_lock lock(m_mutex);
                m_snapshot.bytesFromClients = bytesFromClients;
                m_snapshot.bytesToClients = bytesToClients;
                m_snapshot.rejectedClients = rejectedClients;
            }
            const auto now = std::chrono::steady_clock::now();
            if (clients.size() != publishedClientCount || now >= nextCounterPublish)
            {
                publishCounters(clients.size());
                publishedClientCount = clients.size();
                nextCounterPublish = now + 250ms;
            }

            auto waited = connection->session->waitForwardingIo(
                *connection->transport, std::chrono::steady_clock::now() + idlePollInterval, stopToken);
            if (!waited && waited.error().kind != ssh::SshByteTransportErrorKind::TimedOut
                && waited.error().kind != ssh::SshByteTransportErrorKind::WouldBlock
                && waited.error().kind != ssh::SshByteTransportErrorKind::Cancelled)
            {
                for (auto &client : clients)
                {
                    closeClient(client, *connection);
                }
                publish(PortForwardingJobState::Failed, PortForwardingJobFailure::Transport);
                return;
            }
        }

        for (auto &client : clients)
        {
            closeClient(client, *connection);
        }
        if (remoteListener)
        {
            [[maybe_unused]] const auto closeResult =
                connection->session->closeRemoteForwardListener(*connection->transport, *remoteListener, 100ms);
        }
        publish(PortForwardingJobState::Stopped, PortForwardingJobFailure::None);
    }
    catch (...)
    {
        publish(PortForwardingJobState::Failed, PortForwardingJobFailure::ResourceLimit);
    }
}

} // namespace ztermy::forwarding
