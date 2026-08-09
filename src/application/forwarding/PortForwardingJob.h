#pragma once

#include "application/ssh/SshConnectionBootstrap.h"
#include "domain/forwarding/PortForwardingRule.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ztermy::forwarding
{

enum class PortForwardingJobState : std::uint8_t
{
    Stopped,
    Starting,
    Running,
    Failed,
};

enum class PortForwardingJobFailure : std::uint8_t
{
    None,
    Connection,
    Listener,
    RemoteListener,
    Transport,
    ResourceLimit,
};

enum class PortForwardingJobStartError : std::uint8_t
{
    InvalidConfiguration,
    AlreadyRunning,
    WorkerUnavailable,
};

struct PortForwardingJobSnapshot final
{
    PortForwardingJobState state = PortForwardingJobState::Stopped;
    PortForwardingJobFailure failure = PortForwardingJobFailure::None;
    std::size_t activeClients = 0;
    std::uint64_t bytesFromClients = 0;
    std::uint64_t bytesToClients = 0;
    std::uint64_t rejectedClients = 0;

    [[nodiscard]] friend bool operator==(const PortForwardingJobSnapshot &,
                                         const PortForwardingJobSnapshot &) = default;
};

class PortForwardingJob final
{
public:
    using SnapshotCallback = std::function<void(const PortForwardingJobSnapshot &)>;

    PortForwardingJob() = default;
    ~PortForwardingJob();

    PortForwardingJob(const PortForwardingJob &) = delete;
    PortForwardingJob &operator=(const PortForwardingJob &) = delete;

    [[nodiscard]] std::expected<void, PortForwardingJobStartError>
    start(PortForwardingRule rule, ssh::SshConnectionRequest request, ssh::SshConnectionCallbacks callbacks,
          SnapshotCallback snapshotCallback = {}) noexcept;
    void stop() noexcept;

    [[nodiscard]] PortForwardingJobSnapshot snapshot() const noexcept;

private:
    void run(const PortForwardingRule &rule, ssh::SshConnectionRequest &request,
             const ssh::SshConnectionCallbacks &callbacks, const std::stop_token &stopToken) noexcept;
    void publish(PortForwardingJobState state, PortForwardingJobFailure failure) noexcept;
    void publishCounters(std::size_t activeClients) noexcept;

    mutable std::mutex m_mutex;
    PortForwardingJobSnapshot m_snapshot;
    std::shared_ptr<const SnapshotCallback> m_snapshotCallback;
    std::jthread m_worker;
};

} // namespace ztermy::forwarding
