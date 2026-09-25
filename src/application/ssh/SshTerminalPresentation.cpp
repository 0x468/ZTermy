#include "application/ssh/SshTerminalSession.h"
#include "domain/terminal/GhosttyTerminalEngine.h"

#include <QMetaObject>
#include <chrono>
#include <utility>

namespace ztermy::ssh
{

void SshTerminalSession::publishSnapshot(const bool hostInteraction)
{
    if (hostInteraction)
    {
        m_engine->cancelSynchronizedOutput();
        m_synchronizedOutputStartedNanoseconds.store(0, std::memory_order_release);
    }
    m_engineDirty.store(true, std::memory_order_release);
    publishSnapshotIfDirty();
}

void SshTerminalSession::publishSnapshotIfDirty()
{
    if (!m_engineDirty.load(std::memory_order_acquire))
    {
        return;
    }
    // At most one snapshot waits for delivery; output that lands meanwhile
    // only marks the engine dirty and deliverLatestSnapshot() asks the worker
    // for the next build once the pending frame has gone out.
    {
        std::scoped_lock lock(m_snapshotMutex);
        if (m_pendingSnapshot)
        {
            return;
        }
    }
    buildSnapshot();
}

void SshTerminalSession::buildSnapshot(const bool force)
{
    const auto started = m_synchronizedOutputStartedNanoseconds.load(std::memory_order_acquire);
    if (!force && started != 0 && m_engine->synchronizedOutput()
        && std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                       .count()
                   - started
               < 1'000'000'000)
    {
        return;
    }
    if (started != 0)
    {
        m_engine->cancelSynchronizedOutput();
        m_synchronizedOutputStartedNanoseconds.store(0, std::memory_order_release);
    }
    m_engineDirty.store(false, std::memory_order_release);
    auto result = m_engine->snapshot();
    if (!result)
    {
        postStatus(tr("SSH terminal snapshot failed"));
        return;
    }

    {
        std::scoped_lock lock(m_snapshotMutex);
        m_pendingSnapshot = std::make_shared<const terminal::TerminalSnapshot>(std::move(*result));
    }
    if (!m_snapshotDeliveryScheduled.exchange(true))
    {
        (void)QMetaObject::invokeMethod(this, "scheduleLatestSnapshotDelivery", Qt::QueuedConnection);
    }
}

void SshTerminalSession::scheduleSynchronizedOutputFallback(const std::int64_t startedNanoseconds)
{
    (void)QMetaObject::invokeMethod(
        this,
        [this, startedNanoseconds] {
            QTimer::singleShot(1000, Qt::PreciseTimer, this, [this, startedNanoseconds] {
                if (!m_running.load(std::memory_order_acquire)
                    || m_synchronizedOutputStartedNanoseconds.load(std::memory_order_acquire) != startedNanoseconds
                    || !m_engineDirty.load(std::memory_order_acquire))
                {
                    return;
                }
                std::scoped_lock lock(m_commandMutex);
                if (m_commands.empty() || !std::holds_alternative<SnapshotRequestCommand>(m_commands.back()))
                {
                    m_commands.emplace_back(SnapshotRequestCommand{});
                    signalCommandWake();
                }
            });
        },
        Qt::QueuedConnection);
}

void SshTerminalSession::scheduleLatestSnapshotDelivery()
{
    if (!m_running.load())
    {
        m_snapshotDeliveryScheduled.store(false);
        return;
    }
    if (!m_snapshotDeliveryTimer.isActive())
    {
        m_snapshotDeliveryTimer.start();
    }
}

void SshTerminalSession::deliverLatestSnapshot()
{
    if (!m_running.load())
    {
        std::scoped_lock lock(m_snapshotMutex);
        m_pendingSnapshot.reset();
        m_snapshotDeliveryScheduled.store(false);
        return;
    }
    terminal::TerminalSnapshotPtr snapshot;
    {
        std::scoped_lock lock(m_snapshotMutex);
        snapshot = std::move(m_pendingSnapshot);
    }
    m_snapshotDeliveryScheduled.store(false);
    if (snapshot)
    {
        emit snapshotReady(std::move(snapshot));
    }

    {
        std::scoped_lock lock(m_snapshotMutex);
        if (m_pendingSnapshot)
        {
            if (!m_snapshotDeliveryScheduled.exchange(true))
            {
                scheduleLatestSnapshotDelivery();
            }
            return;
        }
    }
    if (m_engineDirty.load(std::memory_order_acquire) && m_running.load())
    {
        std::scoped_lock lock(m_commandMutex);
        if (m_commands.empty() || !std::holds_alternative<SnapshotRequestCommand>(m_commands.back()))
        {
            m_commands.emplace_back(SnapshotRequestCommand{});
            signalCommandWake();
        }
    }
}

} // namespace ztermy::ssh
