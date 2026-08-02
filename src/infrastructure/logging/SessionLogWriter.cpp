#include "infrastructure/logging/SessionLogWriter.h"

#include <QFile>
#include <QIODevice>

#include <algorithm>

namespace ztermy::logging
{

SessionLogWriter::SessionLogWriter(QObject *parent) : QObject(parent) {}

SessionLogWriter::~SessionLogWriter()
{
    stop();
}

bool SessionLogWriter::start(const QString &path)
{
    const QString normalized = path.trimmed();
    if (normalized.isEmpty() || m_state.load(std::memory_order_acquire) == SessionLogState::Starting
        || m_state.load(std::memory_order_acquire) == SessionLogState::Active)
    {
        return false;
    }
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    {
        std::scoped_lock lock(m_mutex);
        m_queue.clear();
        m_queuedBytes = 0;
        m_path = normalized;
        m_error.clear();
    }
    m_droppedBytes.store(0, std::memory_order_release);
    m_state.store(SessionLogState::Starting, std::memory_order_release);
    emit stateChanged();
    m_worker = std::jthread([this, normalized](const std::stop_token &stopToken) {
        run(normalized, stopToken);
    });
    return true;
}

void SessionLogWriter::stop() noexcept
{
    if (m_worker.joinable())
    {
        m_worker.request_stop();
        m_available.notify_all();
        m_worker.join();
    }
    if (m_state.exchange(SessionLogState::Idle, std::memory_order_acq_rel) != SessionLogState::Idle)
    {
        emit stateChanged();
    }
}

void SessionLogWriter::append(const std::span<const std::byte> bytes) noexcept
{
    const SessionLogState current = m_state.load(std::memory_order_acquire);
    if (bytes.empty() || (current != SessionLogState::Starting && current != SessionLogState::Active))
    {
        return;
    }
    try
    {
        std::scoped_lock lock(m_mutex);
        if (bytes.size() > maximumQueuedBytes - std::min(m_queuedBytes, maximumQueuedBytes))
        {
            const auto previous = m_droppedBytes.fetch_add(bytes.size(), std::memory_order_acq_rel);
            if (previous == 0)
            {
                emit stateChanged();
            }
            return;
        }
        m_queue.emplace_back(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
        m_queuedBytes += bytes.size();
    }
    catch (...)
    {
        const auto previous = m_droppedBytes.fetch_add(bytes.size(), std::memory_order_acq_rel);
        if (previous == 0)
        {
            emit stateChanged();
        }
        return;
    }
    m_available.notify_one();
}

SessionLogState SessionLogWriter::state() const noexcept
{
    return m_state.load(std::memory_order_acquire);
}

QString SessionLogWriter::path() const
{
    std::scoped_lock lock(m_mutex);
    return m_path;
}

QString SessionLogWriter::errorString() const
{
    std::scoped_lock lock(m_mutex);
    return m_error;
}

std::uint64_t SessionLogWriter::droppedBytes() const noexcept
{
    return m_droppedBytes.load(std::memory_order_acquire);
}

void SessionLogWriter::run(QString path, const std::stop_token &stopToken) noexcept
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        publishState(SessionLogState::Failed, file.errorString());
        return;
    }
    publishState(SessionLogState::Active);

    while (true)
    {
        QByteArray chunk;
        {
            std::unique_lock lock(m_mutex);
            m_available.wait(lock, stopToken, [this] {
                return !m_queue.empty();
            });
            if (m_queue.empty())
            {
                if (stopToken.stop_requested())
                {
                    break;
                }
                continue;
            }
            chunk = std::move(m_queue.front());
            m_queue.pop_front();
            m_queuedBytes -= static_cast<std::size_t>(chunk.size());
        }

        qsizetype offset = 0;
        while (offset < chunk.size())
        {
            const qint64 written = file.write(chunk.constData() + offset, chunk.size() - offset);
            if (written <= 0)
            {
                publishState(SessionLogState::Failed, file.errorString());
                return;
            }
            offset += static_cast<qsizetype>(written);
        }
    }

    if (!file.flush())
    {
        publishState(SessionLogState::Failed, file.errorString());
    }
}

void SessionLogWriter::publishState(const SessionLogState state, QString error)
{
    {
        std::scoped_lock lock(m_mutex);
        m_error = std::move(error);
    }
    m_state.store(state, std::memory_order_release);
    emit stateChanged();
}

} // namespace ztermy::logging
