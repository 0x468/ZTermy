#include "ui/terminal/TerminalImagePainter.h"

#include <limits>

namespace ztermy::ui
{
namespace
{
TerminalImageLayer layerOf(std::int32_t z)
{
    if (z < std::numeric_limits<std::int32_t>::min() / 2)
        return TerminalImageLayer::belowBackground;
    return z < 0 ? TerminalImageLayer::belowText : TerminalImageLayer::aboveText;
}

QRectF targetRect(const terminal::TerminalImagePlacement &placement, QSizeF cell, QPointF origin)
{
    return {origin.x() + placement.column * cell.width() + placement.offsetX,
            origin.y() + placement.row * cell.height() + placement.offsetY, static_cast<qreal>(placement.width),
            static_cast<qreal>(placement.height)};
}

QImage pixelView(const terminal::TerminalImage &image)
{
    using terminal::TerminalImageFormat;
    QImage::Format format = QImage::Format_Invalid;
    std::size_t channels = 0;
    switch (image.format)
    {
        case TerminalImageFormat::rgb:
            format = QImage::Format_RGB888;
            channels = 3;
            break;
        case TerminalImageFormat::rgba:
            format = QImage::Format_RGBA8888;
            channels = 4;
            break;
        case TerminalImageFormat::gray:
            format = QImage::Format_Grayscale8;
            channels = 1;
            break;
        case TerminalImageFormat::grayAlpha:
            channels = 2;
            break;
    }
    if (image.width == 0 || image.height == 0 || image.width > 8192 || image.height > 8192 || channels == 0
        || std::uint64_t{image.width} * image.height * channels != image.pixels.size())
        return {};
    const int width = static_cast<int>(image.width);
    const int height = static_cast<int>(image.height);
    if (image.format != TerminalImageFormat::grayAlpha)
        return {image.pixels.data(), width, height, static_cast<qsizetype>(image.width * channels), format};
    QImage expanded(width, height, QImage::Format_ARGB32_Premultiplied);
    if (expanded.isNull())
        return {};
    for (int y = 0; y < height; ++y)
    {
        auto *row = reinterpret_cast<QRgb *>(expanded.scanLine(y));
        for (int x = 0; x < width; ++x)
        {
            const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 2;
            const auto gray = image.pixels[offset];
            row[x] = qPremultiply(qRgba(gray, gray, gray, image.pixels[offset + 1]));
        }
    }
    return expanded;
}
} // namespace

void paintTerminalImages(QPainter &painter, std::span<const terminal::TerminalImagePlacement> images,
                         TerminalImageLayer layer, QSizeF cell, QPointF origin, QRectF viewport)
{
    if (images.empty())
        return;
    painter.save();
    painter.setClipRect(viewport, Qt::IntersectClip);
    // Protocol pixels are not terminal text: do not apply theme contrast or opacity transforms.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const auto &placement : images)
    {
        if (!placement.image || layerOf(placement.z) != layer)
            continue;
        const QRectF target = targetRect(placement, cell, origin);
        if (!target.intersects(viewport))
            continue;
        const auto pixels = pixelView(*placement.image);
        if (pixels.isNull())
            continue;
        painter.drawImage(target, pixels,
                          QRectF{static_cast<qreal>(placement.sourceX), static_cast<qreal>(placement.sourceY),
                                 static_cast<qreal>(placement.sourceWidth),
                                 static_cast<qreal>(placement.sourceHeight)});
    }
    painter.restore();
}

TerminalImageOverlay renderTerminalImageOverlay(std::span<const terminal::TerminalImagePlacement> images, QSizeF cell,
                                                QPointF origin, QRectF viewport, qreal devicePixelRatio)
{
    QRectF bounds;
    for (const auto &placement : images)
        if (placement.image && placement.z >= 0)
            bounds = bounds.united(targetRect(placement, cell, origin).intersected(viewport));
    if (bounds.isEmpty())
        return {};
    const int left = qFloor(bounds.left() * devicePixelRatio);
    const int top = qFloor(bounds.top() * devicePixelRatio);
    const QSize size(qCeil(bounds.right() * devicePixelRatio) - left, qCeil(bounds.bottom() * devicePixelRatio) - top);
    TerminalImageOverlay overlay{.image = QImage(size, QImage::Format_ARGB32_Premultiplied),
                                 .rectangle = {left / devicePixelRatio, top / devicePixelRatio,
                                               size.width() / devicePixelRatio, size.height() / devicePixelRatio}};
    if (overlay.image.isNull())
        return {};
    overlay.image.setDevicePixelRatio(devicePixelRatio);
    overlay.image.fill(Qt::transparent);
    QPainter painter(&overlay.image);
    painter.translate(-overlay.rectangle.topLeft());
    paintTerminalImages(painter, images, TerminalImageLayer::aboveText, cell, origin, viewport);
    return overlay;
}
} // namespace ztermy::ui
