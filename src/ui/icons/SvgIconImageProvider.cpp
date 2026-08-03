#include "ui/icons/SvgIconImageProvider.h"

#include <QColor>
#include <QFile>
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
    : QQuickImageProvider(QQuickImageProvider::Image), m_iconDirectory(std::move(iconDirectory))
{
}

QImage SvgIconImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const qsizetype separator = id.indexOf(u'/');
    const QStringView iconName = QStringView{id}.first(separator < 0 ? id.size() : separator);
    if (!isValidIconName(iconName))
    {
        return {};
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

    const QSize targetSize = renderSize(requestedSize);
    QImage image(targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(iconColor.alphaF());
    renderer.render(&painter, QRectF(QPointF{}, targetSize));

    if (size != nullptr)
    {
        *size = targetSize;
    }
    return image;
}

} // namespace ztermy::ui
