#pragma once

#include <QObject>
#include <QThreadPool>
#include <QVariantList>

namespace ztermy::workbench
{

class LocalFileBrowserController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit LocalFileBrowserController(QObject *parent = nullptr);
    ~LocalFileBrowserController() override;

    [[nodiscard]] QString path() const;
    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString error() const;

    Q_INVOKABLE void navigate(const QString &pathOrUrl);
    Q_INVOKABLE void navigateUp();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool openPath(const QString &pathOrUrl);

signals:
    void changed();

private:
    Q_SIGNAL void listingReady(quint64 generation, QString path, QVariantList entries, QString error);
    void applyListing(quint64 generation, QString path, QVariantList entries, const QString &error);

    QString m_path;
    QVariantList m_entries;
    QString m_error;
    QThreadPool m_worker;
    quint64 m_generation = 0;
    bool m_busy = false;
};

} // namespace ztermy::workbench
