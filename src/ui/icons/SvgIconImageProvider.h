#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QString>

#include <cstddef>

namespace ztermy::ui
{

class SvgIconImageProvider final : public QQuickImageProvider
{
public:
    explicit SvgIconImageProvider(QString iconDirectory = QStringLiteral(":/ztermy/icons"));

    // Rendered icons are cached by id and target size so re-created items
    // (new tabs, panes, menus) do not re-read and re-rasterize the SVG.
    [[nodiscard]] QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    [[nodiscard]] std::size_t cachedImageCount() const;

private:
    QString m_iconDirectory;
    mutable QMutex m_cacheMutex;
    QCache<QString, QImage> m_cache;
};

} // namespace ztermy::ui
