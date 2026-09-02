#include "application/workbench/LocalFileBrowserController.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace
{

[[nodiscard]] QString localPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return QDir::cleanPath(url.isLocalFile() ? url.toLocalFile() : pathOrUrl);
}

} // namespace

namespace ztermy::workbench
{

LocalFileBrowserController::LocalFileBrowserController(QObject *parent) : QObject(parent), m_path(QDir::homePath())
{
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(15'000);
    QObject::connect(this, &LocalFileBrowserController::listingReady, this, &LocalFileBrowserController::applyListing,
                     Qt::QueuedConnection);
    refresh();
}

LocalFileBrowserController::~LocalFileBrowserController()
{
    m_worker.clear();
    m_worker.waitForDone();
}

QString LocalFileBrowserController::path() const
{
    return m_path;
}

QVariantList LocalFileBrowserController::entries() const
{
    return m_entries;
}

bool LocalFileBrowserController::busy() const noexcept
{
    return m_busy;
}

QString LocalFileBrowserController::error() const
{
    return m_error;
}

void LocalFileBrowserController::navigate(const QString &pathOrUrl)
{
    const QString requested = localPath(pathOrUrl);
    const QFileInfo info(requested);
    if (!info.exists() || !info.isDir())
    {
        m_error = tr("The local directory is unavailable.");
        emit changed();
        return;
    }
    m_path = info.absoluteFilePath();
    refresh();
}

void LocalFileBrowserController::navigateUp()
{
    navigate(QFileInfo(m_path).dir().absolutePath() + QStringLiteral("/.."));
}

void LocalFileBrowserController::refresh()
{
    const quint64 generation = ++m_generation;
    const QString requested = m_path;
    m_busy = true;
    m_error.clear();
    emit changed();
    const QPointer<LocalFileBrowserController> self(this);
    m_worker.start([self, generation, requested]() noexcept {
        QVariantList entries;
        QString error;
        try
        {
            const QDir directory(requested);
            if (!directory.exists())
                error = QStringLiteral("unavailable");
            else
            {
                const QFileInfoList listing =
                    directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
                entries.reserve(listing.size());
                for (const QFileInfo &entry : listing)
                {
                    entries.push_back(
                        QVariantMap{{QStringLiteral("name"), entry.fileName()},
                                    {QStringLiteral("path"), entry.absoluteFilePath()},
                                    {QStringLiteral("directory"), entry.isDir()},
                                    {QStringLiteral("size"), entry.size()},
                                    {QStringLiteral("modifiedUtcMs"), entry.lastModified().toMSecsSinceEpoch()}});
                }
            }
        }
        catch (...)
        {
            error = QStringLiteral("resource");
        }
        if (self)
            emit self->listingReady(generation, requested, std::move(entries), std::move(error));
    });
}

bool LocalFileBrowserController::openPath(const QString &pathOrUrl)
{
    const QString path = localPath(pathOrUrl);
    return !path.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void LocalFileBrowserController::applyListing(const quint64 generation, QString path, QVariantList entries,
                                              const QString &error)
{
    if (generation != m_generation)
        return;
    m_path = std::move(path);
    m_entries = std::move(entries);
    m_error = error.isEmpty() ? QString{} : tr("The local directory could not be read.");
    m_busy = false;
    emit changed();
}

} // namespace ztermy::workbench
