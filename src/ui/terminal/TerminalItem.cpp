#include "ui/terminal/TerminalItem.h"

#include <QColor>
#include <QFontMetricsF>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{

constexpr qreal horizontalPadding = 16.0;
constexpr qreal verticalPadding = 14.0;

class TerminalTextureNode final : public QSGSimpleTextureNode
{
public:
    std::uint64_t revision = 0;
    QSize pixelSize;
};

[[nodiscard]] QColor color(const ztermy::terminal::TerminalColor terminalColor)
{
    return {terminalColor.red, terminalColor.green, terminalColor.blue};
}

[[nodiscard]] QByteArray encodedKey(QKeyEvent *event)
{
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    if (control && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
    {
        return {1, static_cast<char>((event->key() - Qt::Key_A) + 1)};
    }
    if (control && event->key() == Qt::Key_Space)
    {
        return {1, '\0'};
    }

    switch (event->key())
    {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return QByteArrayLiteral("\r");
        case Qt::Key_Backspace:
            return {1, '\x7f'};
        case Qt::Key_Tab:
            return QByteArrayLiteral("\t");
        case Qt::Key_Escape:
            return QByteArrayLiteral("\x1b");
        case Qt::Key_Up:
            return QByteArrayLiteral("\x1b[A");
        case Qt::Key_Down:
            return QByteArrayLiteral("\x1b[B");
        case Qt::Key_Right:
            return QByteArrayLiteral("\x1b[C");
        case Qt::Key_Left:
            return QByteArrayLiteral("\x1b[D");
        case Qt::Key_Home:
            return QByteArrayLiteral("\x1b[H");
        case Qt::Key_End:
            return QByteArrayLiteral("\x1b[F");
        case Qt::Key_Delete:
            return QByteArrayLiteral("\x1b[3~");
        case Qt::Key_PageUp:
            return QByteArrayLiteral("\x1b[5~");
        case Qt::Key_PageDown:
            return QByteArrayLiteral("\x1b[6~");
        default:
            break;
    }

    if (control || event->text().isEmpty())
    {
        return {};
    }

    QByteArray encoded = event->text().toUtf8();
    if (alt)
    {
        encoded.prepend('\x1b');
    }
    return encoded;
}

} // namespace

namespace ztermy::ui
{

TerminalItem::TerminalItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);

    m_font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    m_font.setPixelSize(14);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
}

QString TerminalItem::statusText() const
{
    return m_statusText;
}

void TerminalItem::setSnapshot(terminal::TerminalSnapshotPtr snapshot)
{
    if (!snapshot)
    {
        return;
    }
    m_snapshot = std::move(snapshot);
    ++m_revision;
    update();
}

void TerminalItem::setStatusText(const QString &status)
{
    if (m_statusText == status)
    {
        return;
    }
    m_statusText = status;
    emit statusTextChanged();
}

void TerminalItem::requestCurrentSize()
{
    m_reportedColumns = 0;
    m_reportedRows = 0;
    reportTerminalSize();
}

QSGNode *TerminalItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<TerminalTextureNode *>(oldNode);
    if (node == nullptr)
    {
        node = new TerminalTextureNode;
        node->setOwnsTexture(true);
    }

    if (window() == nullptr || width() <= 0 || height() <= 0)
    {
        return node;
    }

    const qreal devicePixelRatio = window()->effectiveDevicePixelRatio();
    const QSize pixelSize{std::max(1, qRound(width() * devicePixelRatio)),
                          std::max(1, qRound(height() * devicePixelRatio))};
    if (node->revision == m_revision && node->pixelSize == pixelSize)
    {
        node->setRect(boundingRect());
        return node;
    }

    const terminal::TerminalColor fallbackForeground{.red = 248, .green = 250, .blue = 252};
    const terminal::TerminalColor fallbackBackground{.red = 11, .green = 16, .blue = 23};
    const terminal::TerminalColor defaultForeground = m_snapshot ? m_snapshot->defaultForeground : fallbackForeground;
    const terminal::TerminalColor defaultBackground = m_snapshot ? m_snapshot->defaultBackground : fallbackBackground;

    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(color(defaultBackground));

    if (m_snapshot)
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const qreal cellWidthValue = cellWidth();
        const qreal cellHeightValue = cellHeight();
        const QFontMetricsF metrics(m_font);

        for (quint16 row = 0; row < m_snapshot->rows; ++row)
        {
            for (quint16 column = 0; column < m_snapshot->columns; ++column)
            {
                const terminal::TerminalCell &cell = m_snapshot->cell(column, row);
                const QRectF cellRect{horizontalPadding + (column * cellWidthValue),
                                      verticalPadding + (row * cellHeightValue), cellWidthValue, cellHeightValue};
                if (cell.background != defaultBackground)
                {
                    painter.fillRect(cellRect, color(cell.background));
                }
            }
        }

        for (quint16 row = 0; row < m_snapshot->rows; ++row)
        {
            for (quint16 column = 0; column < m_snapshot->columns; ++column)
            {
                const terminal::TerminalCell &cell = m_snapshot->cell(column, row);
                if (cell.grapheme.empty() || cell.invisible)
                {
                    continue;
                }

                QFont cellFont = m_font;
                cellFont.setBold(cell.bold);
                cellFont.setItalic(cell.italic);
                cellFont.setUnderline(cell.underline);
                cellFont.setStrikeOut(cell.strikethrough);
                cellFont.setOverline(cell.overline);
                painter.setFont(cellFont);
                painter.setPen(color(cell.foreground));

                const QString grapheme =
                    QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
                const QPointF baseline{horizontalPadding + (column * cellWidthValue),
                                       verticalPadding + (row * cellHeightValue) + metrics.ascent()};
                painter.drawText(baseline, grapheme);
            }
        }

        if (m_snapshot->cursor.visible && m_snapshot->cursor.column < m_snapshot->columns
            && m_snapshot->cursor.row < m_snapshot->rows)
        {
            const QRectF cursorCell{horizontalPadding + (m_snapshot->cursor.column * cellWidthValue),
                                    verticalPadding + (m_snapshot->cursor.row * cellHeightValue), cellWidthValue,
                                    cellHeightValue};
            painter.setPen(QPen(color(m_snapshot->cursor.color), 1.0));
            switch (m_snapshot->cursor.style)
            {
                case terminal::TerminalCursorStyle::bar:
                    painter.fillRect(QRectF(cursorCell.left(), cursorCell.top(), 2.0, cursorCell.height()),
                                     color(m_snapshot->cursor.color));
                    break;
                case terminal::TerminalCursorStyle::underline:
                    painter.fillRect(QRectF(cursorCell.left(), cursorCell.bottom() - 2.0, cursorCell.width(), 2.0),
                                     color(m_snapshot->cursor.color));
                    break;
                case terminal::TerminalCursorStyle::hollowBlock:
                    painter.drawRect(cursorCell.adjusted(0.5, 0.5, -0.5, -0.5));
                    break;
                case terminal::TerminalCursorStyle::block:
                    painter.fillRect(cursorCell, QColor(color(m_snapshot->cursor.color).red(),
                                                        color(m_snapshot->cursor.color).green(),
                                                        color(m_snapshot->cursor.color).blue(), 145));
                    break;
            }
        }
    }

    QSGTexture *newTexture = window()->createTextureFromImage(image);
    node->setTexture(newTexture);
    node->setRect(boundingRect());
    node->setFiltering(QSGTexture::Linear);
    node->revision = m_revision;
    node->pixelSize = pixelSize;
    return node;
}

void TerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    reportTerminalSize();
    update();
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    const QByteArray bytes = encodedKey(event);
    if (bytes.isEmpty())
    {
        QQuickItem::keyPressEvent(event);
        return;
    }

    emit inputGenerated(bytes);
    event->accept();
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty())
    {
        emit inputGenerated(event->commitString().toUtf8());
    }
    event->accept();
}

QVariant TerminalItem::inputMethodQuery(const Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
    {
        return true;
    }
    if (query == Qt::ImCursorRectangle && m_snapshot)
    {
        return QRectF(horizontalPadding + (m_snapshot->cursor.column * cellWidth()),
                      verticalPadding + (m_snapshot->cursor.row * cellHeight()), cellWidth(), cellHeight());
    }
    return QQuickItem::inputMethodQuery(query);
}

void TerminalItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    event->accept();
}

void TerminalItem::reportTerminalSize()
{
    const qreal availableWidth = std::max(0.0, width() - (horizontalPadding * 2.0));
    const qreal availableHeight = std::max(0.0, height() - (verticalPadding * 2.0));
    const auto columns = static_cast<quint16>(std::clamp(std::floor(availableWidth / cellWidth()), 1.0,
                                                         static_cast<double>(std::numeric_limits<quint16>::max())));
    const auto rows = static_cast<quint16>(std::clamp(std::floor(availableHeight / cellHeight()), 1.0,
                                                      static_cast<double>(std::numeric_limits<quint16>::max())));
    if (columns == m_reportedColumns && rows == m_reportedRows)
    {
        return;
    }

    m_reportedColumns = columns;
    m_reportedRows = rows;
    emit sizeRequested(columns, rows, static_cast<quint32>(std::ceil(cellWidth())),
                       static_cast<quint32>(std::ceil(cellHeight())));
}

qreal TerminalItem::cellWidth() const
{
    return std::ceil(QFontMetricsF(m_font).horizontalAdvance(QLatin1Char('M')));
}

qreal TerminalItem::cellHeight() const
{
    return std::ceil(QFontMetricsF(m_font).height());
}

} // namespace ztermy::ui
