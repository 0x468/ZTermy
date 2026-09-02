#pragma once

#include "domain/workbench/CommandHistoryIndex.h"

#include <QObject>
#include <QThreadPool>
#include <QVariantList>

namespace ztermy::workbench
{

class CommandHistoryController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)

public:
    explicit CommandHistoryController(QString storePath, QObject *parent = nullptr);
    ~CommandHistoryController() override;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] QString error() const;
    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] Q_INVOKABLE QVariantList suggestions(const QString &prefix, int limit = 8) const;

    void record(const QString &command, ShellKind shell, const QString &sourceId, const QString &sourceLabel,
                std::int64_t timestampUtcSeconds);

signals:
    void changed();

private:
    Q_SIGNAL void loadCompleted(ztermy::workbench::CommandHistoryIndex index, bool succeeded);
    Q_SIGNAL void persistenceFailed();

    void applyLoad(CommandHistoryIndex index, bool succeeded);
    void persist();

    QString m_storePath;
    CommandHistoryIndex m_index;
    std::vector<IndexedCommand> m_pending;
    QThreadPool m_worker;
    QString m_error;
    bool m_ready = false;
};

} // namespace ztermy::workbench

Q_DECLARE_METATYPE(ztermy::workbench::CommandHistoryIndex)
