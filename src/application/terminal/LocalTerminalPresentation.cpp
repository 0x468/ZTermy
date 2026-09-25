#include "application/terminal/LocalTerminalSession.h"
#include "domain/terminal/GhosttyTerminalEngine.h"

#include <QMetaObject>
#include <chrono>
#include <utility>

namespace ztermy::terminal
{

void LocalTerminalSession::publishSnapshot(const bool hostInteraction)
{
    if (hostInteraction)
    {
        std::scoped_lock lock(m_engineMutex);
        m_engine->cancelSynchronizedOutput();
        m_synchronizedOutputStartedNanoseconds.store(0, std::memory_order_release);
    }
    m_engineDirty.store(true, std::memory_order_release);
    publishSnapshotIfDirty();
}

void LocalTerminalSession::publishSnapshotIfDirty()
{
    if (!m_engineDirty.load(std::memory_order_acquire))
    {
        return;
    }
    // At most one snapshot waits for delivery. Output that lands while one is
    // pending only marks the engine dirty; deliverLatestSnapshot() asks the
    // write worker for the next build once the pending frame has gone out.
    {
        std::scoped_lock snapshotLock(m_snapshotMutex);
        if (m_pendingSnapshot)
        {
            m_snapshotsCoalesced.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    if (m_snapshotBuildActive.exchange(true, std::memory_order_acq_rel))
    {
        m_snapshotsCoalesced.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    buildSnapshot();
    m_snapshotBuildActive.store(false, std::memory_order_release);
}

void LocalTerminalSession::buildSnapshot(const bool force)
{
    TerminalSnapshotPtr snapshot;
    const auto buildStarted = std::chrono::steady_clock::now();
    {
        std::scoped_lock engineLock(m_engineMutex);
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
        // Clear under the engine lock: a feed that lands afterwards re-marks
        // the engine dirty and is picked up by the next delivery round.
        m_engineDirty.store(false, std::memory_order_release);
        auto snapshotResult = m_engine->snapshot();
        if (!snapshotResult)
        {
            postStatus(
                tr("Terminal snapshot failed: %1").arg(QString::fromStdString(snapshotResult.error().message())));
            return;
        }
        snapshot = std::make_shared<const TerminalSnapshot>(std::move(*snapshotResult));
    }
    const auto buildNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - buildStarted).count());
    m_snapshotBuildNanoseconds.fetch_add(buildNanoseconds, std::memory_order_relaxed);
    std::uint64_t previousMaximum = m_maxSnapshotBuildNanoseconds.load(std::memory_order_relaxed);
    while (previousMaximum < buildNanoseconds
           && !m_maxSnapshotBuildNanoseconds.compare_exchange_weak(previousMaximum, buildNanoseconds,
                                                                   std::memory_order_relaxed))
    {
    }
    m_snapshotsProduced.fetch_add(1, std::memory_order_relaxed);
    switch (snapshot->damage)
    {
        case TerminalDamageKind::none:
            m_cleanSnapshots.fetch_add(1, std::memory_order_relaxed);
            break;
        case TerminalDamageKind::partial:
            m_partialDamageSnapshots.fetch_add(1, std::memory_order_relaxed);
            break;
        case TerminalDamageKind::full:
            m_fullDamageSnapshots.fetch_add(1, std::memory_order_relaxed);
            break;
    }
    {
        std::scoped_lock snapshotLock(m_snapshotMutex);
        m_pendingSnapshot = std::move(snapshot);
    }

    if (!m_snapshotDeliveryScheduled.exchange(true))
    {
        (void)QMetaObject::invokeMethod(this, "scheduleLatestSnapshotDelivery", Qt::QueuedConnection);
    }
}

void LocalTerminalSession::scheduleSynchronizedOutputFallback(const std::int64_t startedNanoseconds)
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
                    m_commandAvailable.notify_one();
                }
            });
        },
        Qt::QueuedConnection);
}

void LocalTerminalSession::scheduleLatestSnapshotDelivery()
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

void LocalTerminalSession::deliverLatestSnapshot()
{
    if (!m_running.load())
    {
        std::scoped_lock lock(m_snapshotMutex);
        m_pendingSnapshot.reset();
        m_snapshotDeliveryScheduled.store(false);
        return;
    }
    TerminalSnapshotPtr snapshot;
    {
        std::scoped_lock lock(m_snapshotMutex);
        snapshot = std::move(m_pendingSnapshot);
    }
    m_snapshotDeliveryScheduled.store(false);
    if (snapshot)
    {
        m_snapshotsDelivered.fetch_add(1, std::memory_order_relaxed);
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
            m_commandAvailable.notify_one();
        }
    }
}

} // namespace ztermy::terminal
