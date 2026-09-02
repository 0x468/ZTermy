#pragma once

#include "domain/ssh/SshHostKey.h"

#include <QMetaType>
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

namespace ztermy::ssh
{

using KnownHostEntries = std::vector<KnownHostEntry>;

class KnownHostsController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap lastImportSummary READ lastImportSummary NOTIFY stateChanged)
    Q_PROPERTY(QString defaultOpenSshPath READ defaultOpenSshPath CONSTANT)

public:
    explicit KnownHostsController(QString storePath, QObject *parent = nullptr);
    ~KnownHostsController() override;

    KnownHostsController(const KnownHostsController &) = delete;
    KnownHostsController &operator=(const KnownHostsController &) = delete;

    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString operationError() const;
    [[nodiscard]] QVariantMap lastImportSummary() const;
    [[nodiscard]] QString defaultOpenSshPath() const;

    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool importDefaultOpenSsh(bool silent = false);
    Q_INVOKABLE bool importOpenSshFile(const QString &localFileUrl);
    Q_INVOKABLE bool removeEntry(const QString &host, int port, const QString &algorithmToken);
    Q_INVOKABLE bool clearAll();
    [[nodiscard]] Q_INVOKABLE bool copyText(const QString &text) const;

signals:
    void entriesChanged();
    void stateChanged();

private:
    Q_SIGNAL void operationFinished(KnownHostEntries entries, const QString &error, const QVariantMap &summary);

    [[nodiscard]] bool beginOperation();
    void applyEntries(KnownHostEntries entries, const QString &error, const QVariantMap &summary = {});

    QString m_storePath;
    std::vector<KnownHostEntry> m_entries;
    QVariantList m_entryValues;
    QThreadPool m_worker;
    QString m_operationError;
    QVariantMap m_lastImportSummary;
    bool m_busy = false;
};

} // namespace ztermy::ssh

Q_DECLARE_METATYPE(ztermy::ssh::KnownHostEntries)
