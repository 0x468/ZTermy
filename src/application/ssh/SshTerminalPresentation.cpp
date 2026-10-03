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
    // The context-bound timer delivers on the owner thread and is cancelled
    // when this session dies; a second queued wrapper is unnecessary.
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
}

void SshTerminalSession::scheduleLatestSnapshotDelivery()
{
    if (!m_viewAvailable.load() && !m_running.load())
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
    if (!m_viewAvailable.load() && !m_running.load())
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
    if (m_engineDirty.load(std::memory_order_acquire) && (m_viewAvailable.load() || m_running.load()))
    {
        std::scoped_lock lock(m_commandMutex);
        if (m_commands.empty() || !std::holds_alternative<SnapshotRequestCommand>(m_commands.back()))
        {
            m_commands.emplace_back(SnapshotRequestCommand{});
            signalCommandWake();
        }
    }
}

void SshTerminalSession::readOnlyLoop(const std::stop_token &stopToken)
{
    while (!stopToken.stop_requested() && m_viewAvailable.load())
    {
        Command command;
        {
            std::unique_lock lock(m_commandMutex);
            if (!m_viewCommandAvailable.wait(lock, stopToken, [this] {
                    return !m_commands.empty();
                }))
                break;
            command = std::move(m_commands.front());
            m_commands.pop_front();
            if (const auto *input = std::get_if<InputCommand>(&command))
                m_queuedInputBytes -= static_cast<std::size_t>(input->bytes.size());
            else if (const auto *paste = std::get_if<PasteCommand>(&command))
                m_queuedInputBytes -= static_cast<std::size_t>(paste->bytes.size());
        }
        if (std::holds_alternative<SnapshotRequestCommand>(command))
            publishSnapshotIfDirty();
        else if (const auto *geometry = std::get_if<terminal::TerminalGeometry>(&command))
        {
            if (!m_engine->resize(*geometry))
                publishSnapshot();
        }
        else if (const auto *colors = std::get_if<ColorSchemeCommand>(&command))
        {
            if (!m_engine->setColorScheme(colors->scheme))
                publishSnapshot();
            (void)m_engine->takePtyWrite();
        }
        else
            (void)handleViewCommand(command);
        // All live I/O commands are dropped. This loop owns no SSH transport.
    }
}

bool SshTerminalSession::handleViewCommand(const Command &command)
{
    if (const auto *scroll = std::get_if<ScrollCommand>(&command))
    {
        m_engine->scrollViewport(scroll->rows);
        publishSnapshot();
        return true;
    }

    if (const auto *selection = std::get_if<SelectionCommand>(&command))
    {
        if (const std::error_code error = m_engine->setSelection(selection->selection))
        {
            postStatus(tr("SSH terminal selection failed: %1").arg(QString::fromStdString(error.message())));
            return true;
        }
        publishSnapshot();
        return true;
    }

    if (const auto *gesture = std::get_if<SelectionGestureCommand>(&command))
    {
        const auto changed = m_engine->applySelectionGesture(gesture->gesture);
        if (!changed)
        {
            postStatus(
                tr("SSH terminal selection gesture failed: %1").arg(QString::fromStdString(changed.error().message())));
            return true;
        }
        if (*changed)
        {
            publishSnapshot();
        }
        return true;
    }

    if (const auto *copyMode = std::get_if<CopyModeCommand>(&command))
    {
        const auto changed = m_engine->applyCopyModeAction(copyMode->action);
        if (!changed)
        {
            postStatus(tr("SSH terminal Copy Mode failed: %1").arg(QString::fromStdString(changed.error().message())));
            return true;
        }
        if (*changed)
        {
            publishSnapshot();
        }
        return true;
    }

    if (std::holds_alternative<SelectAllCommand>(command))
    {
        if (const std::error_code error = m_engine->selectAll())
        {
            postStatus(tr("SSH terminal select all failed: %1").arg(QString::fromStdString(error.message())));
            return true;
        }
        publishSnapshot();
        return true;
    }

    if (std::holds_alternative<CopyCommand>(command))
    {
        auto selectedText = m_engine->selectedText();
        if (!selectedText)
        {
            postStatus(tr("SSH terminal copy failed: %1").arg(QString::fromStdString(selectedText.error().message())));
        }
        else if (*selectedText)
        {
            const QString text =
                QString::fromUtf8((*selectedText)->data(), static_cast<qsizetype>((*selectedText)->size()));
            postClipboardText(text);
        }
        return true;
    }

    if (std::holds_alternative<SelectedTextCommand>(command))
    {
        auto selectedText = m_engine->selectedText();
        if (!selectedText)
        {
            postStatus(tr("SSH terminal selection read failed: %1")
                           .arg(QString::fromStdString(selectedText.error().message())));
        }
        else
        {
            const QString text = *selectedText ? QString::fromUtf8((*selectedText)->data(),
                                                                   static_cast<qsizetype>((*selectedText)->size()))
                                               : QString{};
            postSelectedText(text);
        }
        return true;
    }

    if (const auto *search = std::get_if<SearchCommand>(&command))
    {
        auto result = m_engine->search(
            std::string_view(search->query.constData(), static_cast<std::size_t>(search->query.size())),
            search->direction, search->caseSensitive);
        if (!result)
        {
            postStatus(tr("SSH terminal search failed: %1").arg(QString::fromStdString(result.error().message())));
            return true;
        }
        const QString query = QString::fromUtf8(search->query);
        const terminal::TerminalSearchResult searchResult = *result;
        postSearchResult(query, searchResult.current, searchResult.total, searchResult.wrapped);
        publishSnapshot();
        return true;
    }
    if (std::holds_alternative<ClearSearchCommand>(command))
    {
        if (const std::error_code error = m_engine->clearSearch())
        {
            postStatus(tr("SSH terminal search clear failed: %1").arg(QString::fromStdString(error.message())));
            return true;
        }
        postSearchResult({}, 0, 0, false);
        publishSnapshot();
        return true;
    }
    return false;
}

} // namespace ztermy::ssh
