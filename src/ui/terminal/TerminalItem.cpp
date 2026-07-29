#include "ui/terminal/TerminalItem.h"

#include "ui/terminal/TerminalTextLayout.h"

#include <QClipboard>
#include <QColor>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace
{

Q_LOGGING_CATEGORY(terminalRenderLog, "ztermy.terminal.render")

constexpr qreal horizontalPadding = 16.0;
constexpr qreal verticalPadding = 14.0;

class TerminalTextureNode final : public QSGSimpleTextureNode
{
public:
#if !defined(NDEBUG)
    void recordTiming(const qint64 paintNanoseconds, const qint64 textureNanoseconds, const QSize &frameSize,
                      const ztermy::terminal::TerminalDamageKind damage, const std::size_t damagedRowCount)
    {
        paintTimes[sampleCount] = paintNanoseconds;
        textureTimes[sampleCount] = textureNanoseconds;
        ++sampleCount;
        damagedRows += damagedRowCount;
        fullFrames += damage == ztermy::terminal::TerminalDamageKind::full ? 1U : 0U;
        partialFrames += damage == ztermy::terminal::TerminalDamageKind::partial ? 1U : 0U;
        cleanFrames += damage == ztermy::terminal::TerminalDamageKind::none ? 1U : 0U;
        if (sampleCount != paintTimes.size())
        {
            return;
        }

        auto sortedPaintTimes = paintTimes;
        auto sortedTextureTimes = textureTimes;
        std::ranges::sort(sortedPaintTimes);
        std::ranges::sort(sortedTextureTimes);
        constexpr std::size_t percentile95Index = ((timingSampleCount * 95U) - 1U) / 100U;
        constexpr double nanosecondsPerMillisecond = 1'000'000.0;
        qCDebug(terminalRenderLog)
            << "renderer timing"
            << "samples=" << timingSampleCount << "pixels=" << frameSize
            << "paintP95Ms=" << (static_cast<double>(sortedPaintTimes[percentile95Index]) / nanosecondsPerMillisecond)
            << "textureCreateP95Ms="
            << (static_cast<double>(sortedTextureTimes[percentile95Index]) / nanosecondsPerMillisecond)
            << "fullFrames=" << fullFrames << "partialFrames=" << partialFrames << "cleanFrames=" << cleanFrames
            << "damagedRows=" << damagedRows;

        sampleCount = 0;
        damagedRows = 0;
        fullFrames = 0;
        partialFrames = 0;
        cleanFrames = 0;
    }
#endif

    std::uint64_t revision = 0;
    QSize pixelSize;

private:
#if !defined(NDEBUG)
    static constexpr std::size_t timingSampleCount = 120;
    std::array<qint64, timingSampleCount> paintTimes{};
    std::array<qint64, timingSampleCount> textureTimes{};
    std::size_t sampleCount = 0;
    std::size_t damagedRows = 0;
    std::size_t fullFrames = 0;
    std::size_t partialFrames = 0;
    std::size_t cleanFrames = 0;
#endif
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
    notifyInputMethod();
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

void TerminalItem::setClipboardText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
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

#if !defined(NDEBUG)
    QElapsedTimer frameTimer;
    frameTimer.start();
#endif

    const terminal::TerminalColor fallbackForeground{.red = 248, .green = 250, .blue = 252};
    const terminal::TerminalColor fallbackBackground{.red = 11, .green = 16, .blue = 23};
    const QColor selectionBackground(42, 91, 145);
    const QColor selectionForeground(255, 255, 255);
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
        const std::vector<PreeditCluster> preeditClusters = layoutPreeditText(m_preeditText, m_font, cellWidthValue);
        const int insertedColumns = preeditColumnCount(preeditClusters);
        const int snapshotColumns = static_cast<int>(m_snapshot->columns);

        for (quint16 row = 0; row < m_snapshot->rows; ++row)
        {
            for (quint16 column = 0; column < m_snapshot->columns; ++column)
            {
                const terminal::TerminalCell &cell = m_snapshot->cell(column, row);
                const int displayColumn =
                    !preeditClusters.empty() && row == m_snapshot->cursor.row
                        ? shiftedTerminalColumn(column, m_snapshot->cursor.column, insertedColumns)
                        : column;
                if (displayColumn >= snapshotColumns)
                {
                    continue;
                }
                const QRectF cellRect{horizontalPadding + (displayColumn * cellWidthValue),
                                      verticalPadding + (row * cellHeightValue), cellWidthValue, cellHeightValue};
                if (cell.selected)
                {
                    painter.fillRect(cellRect, selectionBackground);
                }
                else if (cell.background != defaultBackground)
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
                const int displayColumn =
                    !preeditClusters.empty() && row == m_snapshot->cursor.row
                        ? shiftedTerminalColumn(column, m_snapshot->cursor.column, insertedColumns)
                        : column;
                if (displayColumn >= snapshotColumns)
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
                painter.setPen(cell.selected ? selectionForeground : color(cell.foreground));

                const QString grapheme =
                    QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
                const QPointF baseline{horizontalPadding + (displayColumn * cellWidthValue),
                                       verticalPadding + (row * cellHeightValue) + metrics.ascent()};
                painter.drawText(baseline, grapheme);
            }
        }

        if (!preeditClusters.empty() && m_snapshot->cursor.column < m_snapshot->columns
            && m_snapshot->cursor.row < m_snapshot->rows)
        {
            const qreal compositionLeft = horizontalPadding + (m_snapshot->cursor.column * cellWidthValue);
            const qreal compositionTop = verticalPadding + (m_snapshot->cursor.row * cellHeightValue);
            QFont compositionFont = m_font;
            compositionFont.setUnderline(true);
            painter.setFont(compositionFont);
            const PreeditCursorCell cursorCell = preeditCursorCell(preeditClusters, m_preeditCursorPosition);
            for (const PreeditCluster &cluster : preeditClusters)
            {
                const QRectF clusterRect{
                    compositionLeft + (cluster.column * cellWidthValue),
                    compositionTop,
                    cluster.width * cellWidthValue,
                    cellHeightValue,
                };
                const bool cursorCluster = m_preeditCursorVisible && cluster.column == cursorCell.column;
                painter.fillRect(clusterRect, cursorCluster ? QColor(255, 255, 255) : QColor(42, 91, 145, 180));
                painter.setPen(cursorCluster ? QColor(11, 16, 23) : QColor(255, 255, 255));
                painter.drawText(QPointF(clusterRect.left(), compositionTop + metrics.ascent()), cluster.text);
            }
            if (m_preeditCursorVisible && cursorCell.column == insertedColumns)
            {
                painter.fillRect(QRectF(compositionLeft + (cursorCell.column * cellWidthValue), compositionTop,
                                        cursorCell.width * cellWidthValue, cellHeightValue),
                                 QColor(255, 255, 255));
            }
        }
        else if (m_snapshot->cursor.visible && m_snapshot->cursor.column < m_snapshot->columns
                 && m_snapshot->cursor.row < m_snapshot->rows)
        {
            const QRectF cursorCell{horizontalPadding + (m_snapshot->cursor.column * cellWidthValue),
                                    verticalPadding + (m_snapshot->cursor.row * cellHeightValue),
                                    m_snapshot->cursor.width * cellWidthValue, cellHeightValue};
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

#if !defined(NDEBUG)
    const qint64 paintNanoseconds = frameTimer.nsecsElapsed();
#endif
    QSGTexture *newTexture = window()->createTextureFromImage(image);
#if !defined(NDEBUG)
    const qint64 textureNanoseconds = frameTimer.nsecsElapsed() - paintNanoseconds;
    node->recordTiming(paintNanoseconds, textureNanoseconds, pixelSize,
                       m_snapshot ? m_snapshot->damage : terminal::TerminalDamageKind::full,
                       m_snapshot ? m_snapshot->damagedRows.size() : std::size_t{0});
#endif
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
    notifyInputMethod();
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    if (control && shift && event->key() == Qt::Key_C)
    {
        emit copyRequested();
        event->accept();
        return;
    }
    if (control && shift && event->key() == Qt::Key_V)
    {
        const QByteArray bytes = QGuiApplication::clipboard()->text(QClipboard::Clipboard).toUtf8();
        if (!bytes.isEmpty())
        {
            emit pasteRequested(bytes);
        }
        event->accept();
        return;
    }

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
    m_preeditText = event->preeditString();
    m_preeditCursorPosition = m_preeditText.size();
    m_preeditCursorVisible = true;
    for (const QInputMethodEvent::Attribute &attribute : event->attributes())
    {
        if (attribute.type == QInputMethodEvent::Cursor)
        {
            m_preeditCursorPosition = std::clamp<qsizetype>(attribute.start, qsizetype{0}, m_preeditText.size());
            m_preeditCursorVisible = attribute.length != 0;
            break;
        }
    }

    if (!event->commitString().isEmpty())
    {
        emit inputGenerated(event->commitString().toUtf8());
    }

    ++m_revision;
    update();
    notifyInputMethod();
    event->accept();
}

QVariant TerminalItem::inputMethodQuery(const Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
    {
        return true;
    }
    if (query == Qt::ImCursorRectangle)
    {
        return inputCursorRectangle();
    }
    if (query == Qt::ImSurroundingText || query == Qt::ImCurrentSelection)
    {
        return QString{};
    }
    if (query == Qt::ImCursorPosition || query == Qt::ImAnchorPosition || query == Qt::ImAbsolutePosition)
    {
        return 0;
    }
    return QQuickItem::inputMethodQuery(query);
}

void TerminalItem::focusOutEvent(QFocusEvent *event)
{
    clearPreedit();
    QQuickItem::focusOutEvent(event);
}

void TerminalItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    const auto point = terminalPoint(event->position());
    if (event->button() != Qt::LeftButton || !point)
    {
        QQuickItem::mousePressEvent(event);
        return;
    }

    m_selectionAnchor = *point;
    m_selecting = true;
    m_selectionMoved = false;
    emit clearSelectionRequested();
    event->accept();
}

void TerminalItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting || !event->buttons().testFlag(Qt::LeftButton))
    {
        QQuickItem::mouseMoveEvent(event);
        return;
    }
    if (const auto point = terminalPoint(event->position()))
    {
        m_selectionMoved = true;
        emit selectionRequested(m_selectionAnchor.column, m_selectionAnchor.row, point->column, point->row,
                                event->modifiers().testFlag(Qt::AltModifier));
    }
    event->accept();
}

void TerminalItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_selecting || event->button() != Qt::LeftButton)
    {
        QQuickItem::mouseReleaseEvent(event);
        return;
    }
    if (m_selectionMoved)
    {
        if (const auto point = terminalPoint(event->position()))
        {
            emit selectionRequested(m_selectionAnchor.column, m_selectionAnchor.row, point->column, point->row,
                                    event->modifiers().testFlag(Qt::AltModifier));
        }
    }
    m_selecting = false;
    event->accept();
}

void TerminalItem::wheelEvent(QWheelEvent *event)
{
    m_wheelRemainder += event->angleDelta().y();
    const int steps = m_wheelRemainder / 120;
    m_wheelRemainder -= steps * 120;
    if (steps != 0)
    {
        emit scrollRequested(-steps * 3);
    }
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

std::optional<terminal::TerminalPoint> TerminalItem::terminalPoint(const QPointF &position) const
{
    if (!m_snapshot || m_snapshot->columns == 0 || m_snapshot->rows == 0)
    {
        return std::nullopt;
    }

    const qreal columnValue = std::floor((position.x() - horizontalPadding) / cellWidth());
    const qreal rowValue = std::floor((position.y() - verticalPadding) / cellHeight());
    return terminal::TerminalPoint{
        .column = static_cast<quint16>(std::clamp(columnValue, 0.0, static_cast<qreal>(m_snapshot->columns - 1))),
        .row = static_cast<quint16>(std::clamp(rowValue, 0.0, static_cast<qreal>(m_snapshot->rows - 1)))};
}

QRectF TerminalItem::inputCursorRectangle() const
{
    qreal cursorX = horizontalPadding;
    qreal cursorY = verticalPadding;
    if (m_snapshot)
    {
        cursorX += m_snapshot->cursor.column * cellWidth();
        cursorY += m_snapshot->cursor.row * cellHeight();
    }
    if (!m_preeditText.isEmpty())
    {
        const std::vector<PreeditCluster> clusters = layoutPreeditText(m_preeditText, m_font, cellWidth());
        const PreeditCursorCell cursorCell = preeditCursorCell(clusters, m_preeditCursorPosition);
        cursorX += cursorCell.column * cellWidth();
        return {cursorX, cursorY, cursorCell.width * cellWidth(), cellHeight()};
    }
    const qreal cursorWidth = m_snapshot ? m_snapshot->cursor.width * cellWidth() : cellWidth();
    return {cursorX, cursorY, cursorWidth, cellHeight()};
}

void TerminalItem::clearPreedit()
{
    if (m_preeditText.isEmpty())
    {
        return;
    }
    m_preeditText.clear();
    m_preeditCursorPosition = 0;
    m_preeditCursorVisible = true;
    ++m_revision;
    update();
}

void TerminalItem::notifyInputMethod() const
{
    if (hasActiveFocus() && QGuiApplication::inputMethod() != nullptr)
    {
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImSurroundingText | Qt::ImCursorPosition
                                               | Qt::ImAnchorPosition);
    }
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
