#include "application/logging/ConnectionHistoryController.h"

#include "infrastructure/logging/ConnectionHistoryStore.h"

#include <QDateTime>
#include <QPointer>
#include <QTimeZone>

#include <algorithm>
#include <memory>
#include <ranges>
#include <utility>

namespace
{

[[nodiscard]] QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string text(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QVariantMap projection(const ztermy::logging::ConnectionHistoryEntry &entry)
{
    return {{QStringLiteral("id"), text(entry.id)},
            {QStringLiteral("sessionId"), text(entry.sessionId)},
            {QStringLiteral("profileId"), text(entry.profileId)},
            {QStringLiteral("hostLabel"), text(entry.hostLabel)},
            {QStringLiteral("hostname"), text(entry.hostname)},
            {QStringLiteral("username"), text(entry.username)},
            {QStringLiteral("protocol"), text(entry.protocol)},
            {QStringLiteral("localUsername"), text(entry.localUsername)},
            {QStringLiteral("localHostname"), text(entry.localHostname)},
            {QStringLiteral("status"), text(entry.status)},
            {QStringLiteral("phase"), text(entry.phase)},
            {QStringLiteral("failure"), text(entry.failure)},
            {QStringLiteral("rawLogPath"), text(entry.rawLogPath)},
            {QStringLiteral("startedUtcMs"), entry.startedUtcMs},
            {QStringLiteral("endedUtcMs"), entry.endedUtcMs},
            {QStringLiteral("saved"), entry.saved}};
}

} // namespace

namespace ztermy::logging
{

ConnectionHistoryController::ConnectionHistoryController(QString storePath, QObject *parent)
    : QObject(parent), m_storePath(std::move(storePath))
{
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(15'000);
    QObject::connect(
        this, &ConnectionHistoryController::persistenceFailed, this,
        [this] {
            m_error = tr("Connection history could not be saved.");
            emit changed();
        },
        Qt::QueuedConnection);
    if (auto loaded = ConnectionHistoryStore(m_storePath).load())
    {
        m_history = std::move(*loaded);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool recovered = false;
        for (ConnectionHistoryEntry &entry : m_history.entries)
        {
            if (entry.endedUtcMs == 0)
            {
                entry.endedUtcMs = (std::max)(entry.startedUtcMs, now);
                entry.status = "interrupted";
                entry.phase = "disconnected";
                recovered = true;
            }
        }
        if (recovered)
        {
            persist();
        }
    }
    else
    {
        m_error = tr("Connection history could not be loaded.");
    }
    rebuildProjection();
}

ConnectionHistoryController::~ConnectionHistoryController()
{
    m_worker.clear();
    m_worker.waitForDone();
}

QVariantList ConnectionHistoryController::entries() const
{
    return m_entries;
}
QString ConnectionHistoryController::error() const
{
    return m_error;
}
int ConnectionHistoryController::visibleLimit() const noexcept
{
    return m_visibleLimit;
}
bool ConnectionHistoryController::hasMore() const noexcept
{
    return m_hasMore;
}

void ConnectionHistoryController::recordStarted(ConnectionHistoryEntry entry)
{
    if (!validConnectionHistoryEntry(entry))
    {
        return;
    }
    std::erase_if(m_history.entries, [&entry](const ConnectionHistoryEntry &candidate) {
        return candidate.sessionId == entry.sessionId;
    });
    m_history.entries.insert(m_history.entries.begin(), std::move(entry));
    pruneConnectionHistory(m_history);
    rebuildProjection();
    persist();
}

void ConnectionHistoryController::recordPhase(const QString &sessionId, const QString &phase, const QString &status,
                                              const QString &failure)
{
    const std::string id = text(sessionId);
    const auto entry = std::ranges::find(m_history.entries, id, &ConnectionHistoryEntry::sessionId);
    if (entry == m_history.entries.end())
    {
        return;
    }
    entry->phase = text(phase);
    entry->status = text(status);
    entry->failure = text(failure);
    rebuildProjection();
    persist();
}

void ConnectionHistoryController::recordEnded(const QString &sessionId, const QString &status)
{
    const std::string id = text(sessionId);
    const auto entry = std::ranges::find(m_history.entries, id, &ConnectionHistoryEntry::sessionId);
    if (entry == m_history.entries.end() || entry->endedUtcMs != 0)
    {
        return;
    }
    entry->endedUtcMs = (std::max)(entry->startedUtcMs, QDateTime::currentMSecsSinceEpoch());
    entry->status = text(status);
    entry->phase = "disconnected";
    rebuildProjection();
    persist();
}

void ConnectionHistoryController::setRawLogPath(const QString &sessionId, const QString &path)
{
    const auto entry = std::ranges::find(m_history.entries, text(sessionId), &ConnectionHistoryEntry::sessionId);
    if (entry != m_history.entries.end())
    {
        entry->rawLogPath = text(path);
        rebuildProjection();
        persist();
    }
}

void ConnectionHistoryController::setFilter(const QString &search, const QString &date, const QString &host,
                                            const bool savedOnly)
{
    m_search = search.trimmed();
    m_date = date.trimmed();
    m_host = host.trimmed();
    m_savedOnly = savedOnly;
    m_visibleLimit = 30;
    rebuildProjection();
}

void ConnectionHistoryController::loadMore()
{
    m_visibleLimit += 30;
    rebuildProjection();
}

bool ConnectionHistoryController::toggleSaved(const QString &id)
{
    const auto entry = std::ranges::find(m_history.entries, text(id), &ConnectionHistoryEntry::id);
    if (entry == m_history.entries.end())
    {
        return false;
    }
    entry->saved = !entry->saved;
    rebuildProjection();
    persist();
    return true;
}

bool ConnectionHistoryController::remove(const QString &id)
{
    const std::size_t before = m_history.entries.size();
    std::erase_if(m_history.entries, [&id](const ConnectionHistoryEntry &entry) {
        return text(entry.id) == id;
    });
    if (m_history.entries.size() == before)
    {
        return false;
    }
    rebuildProjection();
    persist();
    return true;
}

bool ConnectionHistoryController::clearUnsaved()
{
    std::erase_if(m_history.entries, [](const ConnectionHistoryEntry &entry) {
        return !entry.saved && entry.endedUtcMs != 0;
    });
    rebuildProjection();
    persist();
    return true;
}

void ConnectionHistoryController::persist()
{
    const QString path = m_storePath;
    const auto snapshot = std::make_shared<const ConnectionHistory>(m_history);
    const QPointer<ConnectionHistoryController> self(this);
    m_worker.start([path, snapshot, self]() noexcept {
        try
        {
            if (!ConnectionHistoryStore(path).save(*snapshot) && self)
            {
                emit self->persistenceFailed();
            }
        }
        catch (...)
        {
            if (self)
            {
                emit self->persistenceFailed();
            }
        }
    });
}

void ConnectionHistoryController::rebuildProjection()
{
    QVariantList filtered;
    const QString query = m_search.toCaseFolded();
    for (const ConnectionHistoryEntry &entry : m_history.entries)
    {
        const QDate date = QDateTime::fromMSecsSinceEpoch(entry.startedUtcMs, QTimeZone::UTC).date();
        const QString dateToken = date.toString(Qt::ISODate);
        const QString haystack = QStringLiteral("%1 %2 %3 %4 %5 %6")
                                     .arg(text(entry.hostLabel), text(entry.hostname), text(entry.username),
                                          text(entry.localUsername), text(entry.localHostname), text(entry.status))
                                     .toCaseFolded();
        if ((!query.isEmpty() && !haystack.contains(query)) || (!m_date.isEmpty() && m_date != dateToken)
            || (!m_host.isEmpty() && m_host != text(entry.hostname)) || (m_savedOnly && !entry.saved))
        {
            continue;
        }
        if (filtered.size() < m_visibleLimit)
        {
            filtered.push_back(projection(entry));
        }
        else
        {
            m_hasMore = true;
        }
    }
    m_hasMore = false;
    int matches = 0;
    for (const ConnectionHistoryEntry &entry : m_history.entries)
    {
        const QString haystack = QStringLiteral("%1 %2 %3 %4 %5 %6")
                                     .arg(text(entry.hostLabel), text(entry.hostname), text(entry.username),
                                          text(entry.localUsername), text(entry.localHostname), text(entry.status))
                                     .toCaseFolded();
        const QString dateToken =
            QDateTime::fromMSecsSinceEpoch(entry.startedUtcMs, QTimeZone::UTC).date().toString(Qt::ISODate);
        if ((query.isEmpty() || haystack.contains(query)) && (m_date.isEmpty() || m_date == dateToken)
            && (m_host.isEmpty() || m_host == text(entry.hostname)) && (!m_savedOnly || entry.saved))
        {
            ++matches;
        }
    }
    m_hasMore = matches > filtered.size();
    m_entries = std::move(filtered);
    emit changed();
}

} // namespace ztermy::logging
