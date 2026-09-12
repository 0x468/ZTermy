#pragma once

#include "domain/logging/ConnectionHistory.h"

#include <QObject>
#include <QThreadPool>
#include <QVariantList>

namespace ztermy::logging
{

class ConnectionHistoryController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int visibleLimit READ visibleLimit NOTIFY changed)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)

public:
    explicit ConnectionHistoryController(QString storePath, QObject *parent = nullptr);
    ~ConnectionHistoryController() override;

    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] QString error() const;
    [[nodiscard]] int visibleLimit() const noexcept;
    [[nodiscard]] bool hasMore() const noexcept;

    void recordStarted(ConnectionHistoryEntry entry);
    void recordPhase(const QString &sessionId, const QString &phase, const QString &status,
                     const QString &failure = {});
    void recordEnded(const QString &sessionId, const QString &status = QStringLiteral("disconnected"));
    void setRawLogPath(const QString &sessionId, const QString &path);
    void setRecordingEnabled(bool enabled);

    Q_INVOKABLE void setFilter(const QString &search, const QString &date, const QString &host, bool savedOnly);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE bool toggleSaved(const QString &id);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool clearUnsaved();

signals:
    void changed();

private:
    Q_SIGNAL void persistenceFailed();

    void persist();
    void rebuildProjection();

    QString m_storePath;
    ConnectionHistory m_history;
    QVariantList m_entries;
    QThreadPool m_worker;
    QString m_error;
    QString m_search;
    QString m_date;
    QString m_host;
    int m_visibleLimit = 30;
    bool m_savedOnly = false;
    bool m_hasMore = false;
    bool m_recordingEnabled = true;
};

} // namespace ztermy::logging
