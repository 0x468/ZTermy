#pragma once

#include "domain/terminal/TerminalImage.h"

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <span>

namespace ztermy::ui
{
enum class TerminalImageLayer : std::uint8_t
{
    belowBackground,
    belowText,
    aboveText
};

void paintTerminalImages(QPainter &painter, std::span<const terminal::TerminalImagePlacement> images,
                         TerminalImageLayer layer, QSizeF cell, QPointF origin, QRectF viewport);

struct TerminalImageOverlay
{
    QImage image;
    QRectF rectangle;
};

[[nodiscard]] TerminalImageOverlay renderTerminalImageOverlay(std::span<const terminal::TerminalImagePlacement> images,
                                                              QSizeF cell, QPointF origin, QRectF viewport,
                                                              qreal devicePixelRatio);
} // namespace ztermy::ui
