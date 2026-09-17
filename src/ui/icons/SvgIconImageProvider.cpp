#include "ui/icons/SvgIconImageProvider.h"

#include <QColor>
#include <QFile>
#include <QMutexLocker>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <utility>

namespace ztermy::ui
{
namespace
{

constexpr int defaultIconSize = 20;
constexpr int maximumIconSize = 512;
// Cache budget in bytes; a 40x40 icon costs 6.4 KB, so this holds hundreds
// of distinct icon/color/size combinations.
constexpr qsizetype iconCacheBudgetBytes = 4 * 1024 * 1024;

[[nodiscard]] bool isValidIconName(const QStringView name)
{
    if (name.isEmpty())
    {
        return false;
    }

    return std::ranges::all_of(name, [](const QChar character) {
        return character.isLower() || character.isDigit() || character == u'-';
    });
}

[[nodiscard]] QSize renderSize(const QSize &requestedSize)
{
    if (!requestedSize.isValid())
    {
        return {defaultIconSize, defaultIconSize};
    }

    return {std::clamp(requestedSize.width(), 1, maximumIconSize),
            std::clamp(requestedSize.height(), 1, maximumIconSize)};
}

} // namespace

SvgIconImageProvider::SvgIconImageProvider(QString iconDirectory)
    : QQuickImageProvider(QQuickImageProvider::Image),
      m_iconDirectory(std::move(iconDirectory)),
      m_cache(iconCacheBudgetBytes)
{
}

std::size_t SvgIconImageProvider::cachedImageCount() const
{
    const QMutexLocker locker(&m_cacheMutex);
    return static_cast<std::size_t>(m_cache.count());
}

QImage SvgIconImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const qsizetype separator = id.indexOf(u'/');
    const QStringView iconName = QStringView{id}.first(separator < 0 ? id.size() : separator);
    if (!isValidIconName(iconName))
    {
        return {};
    }

    const QSize targetSize = renderSize(requestedSize);
    const QString cacheKey =
        id + u'@' + QString::number(targetSize.width()) + u'x' + QString::number(targetSize.height());
    {
        const QMutexLocker locker(&m_cacheMutex);
        if (const QImage *cached = m_cache.object(cacheKey); cached != nullptr)
        {
            if (size != nullptr)
            {
                *size = cached->size();
            }
            return *cached;
        }
    }

    QColor iconColor(Qt::white);
    if (separator >= 0)
    {
        const QString colorName = QStringView{id}.sliced(separator + 1).toString().section(u'/', 0, 0);
        const QColor requestedColor(QStringLiteral("#") + colorName);
        if (requestedColor.isValid())
        {
            iconColor = requestedColor;
        }
    }

    QFile iconFile(m_iconDirectory + u'/' + iconName.toString() + QStringLiteral(".svg"));
    if (!iconFile.open(QIODevice::ReadOnly))
    {
        return {};
    }

    QByteArray source = iconFile.readAll();
    source.replace("currentColor", iconColor.name(QColor::HexRgb).toUtf8());
    QSvgRenderer renderer(source);
    if (!renderer.isValid())
    {
        return {};
    }

    QImage image(targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(iconColor.alphaF());
    renderer.render(&painter, QRectF(QPointF{}, targetSize));
    painter.end();

    if (size != nullptr)
    {
        *size = targetSize;
    }
    {
        const QMutexLocker locker(&m_cacheMutex);
        m_cache.insert(cacheKey, new QImage(image), image.sizeInBytes());
    }
    return image;
}

} // namespace ztermy::ui
