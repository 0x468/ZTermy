#include "ui/terminal/TerminalItem.h"

#include "ui/terminal/TerminalRowReuseAnalysis.h"
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
    QImage image;
    ztermy::terminal::TerminalSnapshotPtr previousDiagnosticSnapshot;
    std::vector<ztermy::ui::TerminalKeywordCellStyle> keywordStyles;
    QSGSimpleTextureNode *cursorNode = nullptr;
    QRectF cursorRect;

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

[[nodiscard]] bool sameColor(const ztermy::terminal::TerminalColor left,
                             const ztermy::terminal::TerminalColor right) noexcept
{
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

constexpr std::array<QFont::Tag, 4> ligatureFeatures{
    QFont::Tag{"liga"},
    QFont::Tag{"clig"},
    QFont::Tag{"calt"},
    QFont::Tag{"dlig"},
};

void configureLigatures(QFont &font, const bool enabled)
{
    for (const QFont::Tag feature : ligatureFeatures)
    {
        if (enabled)
        {
            font.unsetFeature(feature);
        }
        else
        {
            font.setFeature(feature, 0);
        }
    }
}

[[nodiscard]] bool ligatureRunCell(const ztermy::terminal::TerminalCell &cell)
{
    return !cell.invisible && cell.displayWidth == 1 && cell.grapheme.size() == 1 && cell.grapheme.front() >= U'!'
           && cell.grapheme.front() <= U'~';
}

[[nodiscard]] bool sameTextStyle(const ztermy::terminal::TerminalCell &first,
                                 const ztermy::terminal::TerminalCell &second)
{
    return first.selected == second.selected && first.bold == second.bold && first.italic == second.italic
           && first.underline == second.underline && first.strikethrough == second.strikethrough
           && first.overline == second.overline && color(first.foreground) == color(second.foreground);
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
    m_statusText = tr("Starting local terminal...");
    setFlag(ItemHasContents, true);
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);

    m_font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    m_font.setPixelSize(14);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);

    m_cursorBlinkTimer.setInterval(530);
    QObject::connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this] {
        if (!isVisible() || !m_snapshot || !m_snapshot->cursor.visible)
        {
            return;
        }
        m_cursorBlinkPhase = !m_cursorBlinkPhase;
        m_renderMetrics.recordCursorInvalidation();
        invalidateRenderer(false);
    });
    m_cursorBlinkTimer.start();
}

QString TerminalItem::statusText() const
{
    return m_statusText;
}

QString TerminalItem::fontFamily() const
{
    return m_font.families().value(0);
}

int TerminalItem::fontPixelSize() const noexcept
{
    return m_font.pixelSize();
}

bool TerminalItem::ligaturesEnabled() const noexcept
{
    return m_ligaturesEnabled;
}

qreal TerminalItem::backgroundOpacity() const noexcept
{
    return m_backgroundOpacity;
}

QString TerminalItem::cursorPreference() const
{
    return m_cursorPreference;
}

bool TerminalItem::cursorBlink() const noexcept
{
    return m_cursorBlink;
}

bool TerminalItem::copyOnSelect() const noexcept
{
    return m_copyOnSelect;
}

bool TerminalItem::confirmMultilinePaste() const noexcept
{
    return m_confirmMultilinePaste;
}

bool TerminalItem::scrollbarVisible() const noexcept
{
    return m_snapshot && m_snapshot->scrollbar.total > m_snapshot->scrollbar.visible;
}

qreal TerminalItem::scrollbarPosition() const noexcept
{
    if (!scrollbarVisible())
    {
        return 1.0;
    }
    const std::uint64_t maximumOffset = m_snapshot->scrollbar.total - m_snapshot->scrollbar.visible;
    return maximumOffset == 0 ? 1.0
                              : static_cast<qreal>(m_snapshot->scrollbar.offset) / static_cast<qreal>(maximumOffset);
}

qreal TerminalItem::scrollbarPageRatio() const noexcept
{
    if (!m_snapshot || m_snapshot->scrollbar.total == 0)
    {
        return 1.0;
    }
    return std::clamp(
        static_cast<qreal>(m_snapshot->scrollbar.visible) / static_cast<qreal>(m_snapshot->scrollbar.total), 0.0, 1.0);
}

bool TerminalItem::selectionActionVisible() const noexcept
{
    return m_selectionActionVisible;
}

QPointF TerminalItem::selectionActionPosition() const noexcept
{
    return m_selectionActionPosition;
}

QVariantList TerminalItem::keywordHighlightRules() const
{
    return m_keywordHighlightRuleValues;
}

QColor TerminalItem::foregroundOverride() const
{
    return m_foregroundOverride;
}

QColor TerminalItem::backgroundOverride() const
{
    return m_backgroundOverride;
}

void TerminalItem::setPerformanceMetricsEnabled(const bool enabled) noexcept
{
    m_renderMetrics.setEnabled(enabled);
}

void TerminalItem::resetPerformanceMetrics() noexcept
{
    m_renderMetrics.reset();
}

TerminalRenderMetricsSnapshot TerminalItem::performanceMetrics() const noexcept
{
    return m_renderMetrics.snapshot();
}

void TerminalItem::setSnapshot(terminal::TerminalSnapshotPtr snapshot)
{
    if (!snapshot)
    {
        dismissSelectionAction();
        m_snapshot.reset();
        invalidateRenderer(true);
        notifyInputMethod();
        emit scrollbarChanged();
        return;
    }
    m_renderMetrics.recordSnapshot(snapshot->damage, snapshot->damagedRows.size());
    m_snapshot = std::move(snapshot);
    invalidateRenderer(true);
    notifyInputMethod();
    emit scrollbarChanged();
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

void TerminalItem::setFontFamily(const QString &family)
{
    const QString normalized = family.trimmed();
    if (normalized.isEmpty() || normalized.size() > 128 || fontFamily() == normalized)
    {
        return;
    }
    m_font.setFamilies({normalized, QStringLiteral("Consolas")});
    m_reportedColumns = 0;
    m_reportedRows = 0;
    invalidateRenderer(true);
    reportTerminalSize();
    notifyInputMethod();
    emit fontChanged();
}

void TerminalItem::setFontPixelSize(const int pixelSize)
{
    if (pixelSize < 8 || pixelSize > 32 || m_font.pixelSize() == pixelSize)
    {
        return;
    }
    m_font.setPixelSize(pixelSize);
    m_reportedColumns = 0;
    m_reportedRows = 0;
    invalidateRenderer(true);
    reportTerminalSize();
    notifyInputMethod();
    emit fontChanged();
}

void TerminalItem::setLigaturesEnabled(const bool enabled)
{
    if (m_ligaturesEnabled == enabled)
    {
        return;
    }
    m_ligaturesEnabled = enabled;
    configureLigatures(m_font, enabled);
    invalidateRenderer(true);
    emit fontChanged();
}

void TerminalItem::setBackgroundOpacity(const qreal opacity)
{
    const qreal normalized = std::clamp(opacity, 0.0, 1.0);
    if (qFuzzyCompare(m_backgroundOpacity, normalized))
    {
        return;
    }
    m_backgroundOpacity = normalized;
    invalidateRenderer(true);
    emit backgroundOpacityChanged();
}

void TerminalItem::setCursorPreference(const QString &preference)
{
    if ((preference != QStringLiteral("terminal") && preference != QStringLiteral("block")
         && preference != QStringLiteral("bar") && preference != QStringLiteral("underline"))
        || m_cursorPreference == preference)
    {
        return;
    }
    m_cursorPreference = preference;
    invalidateRenderer(true);
    emit cursorAppearanceChanged();
}

void TerminalItem::setCursorBlink(const bool enabled)
{
    if (m_cursorBlink == enabled)
    {
        return;
    }
    m_cursorBlink = enabled;
    m_cursorBlinkPhase = true;
    enabled ? m_cursorBlinkTimer.start() : m_cursorBlinkTimer.stop();
    invalidateRenderer(true);
    emit cursorAppearanceChanged();
}

void TerminalItem::setCopyOnSelect(const bool enabled)
{
    if (m_copyOnSelect == enabled)
    {
        return;
    }
    m_copyOnSelect = enabled;
    emit copyOnSelectChanged();
}

void TerminalItem::setConfirmMultilinePaste(const bool enabled)
{
    if (m_confirmMultilinePaste == enabled)
    {
        return;
    }
    m_confirmMultilinePaste = enabled;
    if (!enabled)
    {
        m_pendingMultilinePaste.clear();
    }
    emit confirmMultilinePasteChanged();
}

void TerminalItem::setKeywordHighlightRules(const QVariantList &rules)
{
    if (m_keywordHighlightRuleValues == rules)
    {
        return;
    }
    m_keywordHighlightRuleValues = rules;
    m_keywordHighlightRules.clear();
    m_keywordHighlightRules.reserve(std::min<std::size_t>(static_cast<std::size_t>(rules.size()), maximumKeywordRules));
    for (const QVariant &value : rules)
    {
        if (m_keywordHighlightRules.size() >= maximumKeywordRules)
        {
            break;
        }
        const QVariantMap map = value.toMap();
        const QString pattern = map.value(QStringLiteral("pattern")).toString();
        if (pattern.isEmpty() || pattern.size() > maximumKeywordPatternLength)
        {
            continue;
        }
        const QColor foreground(map.value(QStringLiteral("foreground")).toString());
        const QColor background(map.value(QStringLiteral("background")).toString());
        if (!foreground.isValid() && !background.isValid())
        {
            continue;
        }
        m_keywordHighlightRules.push_back(TerminalKeywordRule{
            .id = map.value(QStringLiteral("id")).toString(),
            .pattern = pattern,
            .foreground = foreground,
            .background = background,
            .enabled = map.value(QStringLiteral("enabled"), true).toBool(),
            .caseSensitive = map.value(QStringLiteral("caseSensitive"), false).toBool(),
        });
    }
    invalidateRenderer(true);
    emit keywordHighlightRulesChanged();
}

void TerminalItem::setForegroundOverride(const QColor &value)
{
    if (m_foregroundOverride == value)
    {
        return;
    }
    m_foregroundOverride = value;
    invalidateRenderer(true);
    emit paletteOverrideChanged();
}

void TerminalItem::setBackgroundOverride(const QColor &value)
{
    if (m_backgroundOverride == value)
    {
        return;
    }
    m_backgroundOverride = value;
    invalidateRenderer(true);
    emit paletteOverrideChanged();
}

void TerminalItem::resolveMultilinePaste(const bool accepted)
{
    QByteArray pending = std::move(m_pendingMultilinePaste);
    m_pendingMultilinePaste.clear();
    if (accepted && !pending.isEmpty())
    {
        emit pasteRequested(pending);
    }
}

void TerminalItem::scrollToFraction(const qreal fraction)
{
    if (!scrollbarVisible())
    {
        return;
    }
    const std::uint64_t maximumOffset = m_snapshot->scrollbar.total - m_snapshot->scrollbar.visible;
    const qreal normalized = std::clamp(fraction, 0.0, 1.0);
    const std::uint64_t targetOffset =
        normalized <= 0.0   ? 0
        : normalized >= 1.0 ? maximumOffset
                            : static_cast<std::uint64_t>(std::llround(normalized * static_cast<qreal>(maximumOffset)));
    const std::uint64_t currentOffset = m_snapshot->scrollbar.offset;
    if (targetOffset == currentOffset)
    {
        return;
    }
    const auto maximumStep = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
    if (targetOffset > currentOffset)
    {
        emit scrollRequested(static_cast<int>(std::min(targetOffset - currentOffset, maximumStep)));
    }
    else
    {
        emit scrollRequested(-static_cast<int>(std::min(currentOffset - targetOffset, maximumStep)));
    }
}

void TerminalItem::dismissSelectionAction()
{
    if (!m_selectionActionVisible)
    {
        return;
    }
    m_selectionActionVisible = false;
    emit selectionActionChanged();
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
    if (node->revision == m_revision)
    {
        node->setRect(boundingRect());
        return node;
    }

    QElapsedTimer frameTimer;
#if !defined(NDEBUG)
    constexpr bool debugTimingEnabled = true;
#else
    constexpr bool debugTimingEnabled = false;
#endif
    const bool timingEnabled = debugTimingEnabled || m_renderMetrics.enabled();
    if (timingEnabled)
    {
        frameTimer.start();
    }

    const terminal::TerminalColor fallbackForeground{.red = 248, .green = 250, .blue = 252};
    const terminal::TerminalColor fallbackBackground{.red = 11, .green = 16, .blue = 23};
    const QColor selectionBackground(42, 91, 145);
    const QColor selectionForeground(255, 255, 255);
    const terminal::TerminalColor defaultForeground = m_snapshot ? m_snapshot->defaultForeground : fallbackForeground;
    const terminal::TerminalColor defaultBackground = m_snapshot ? m_snapshot->defaultBackground : fallbackBackground;

    QColor defaultBackgroundColor = m_backgroundOverride.isValid() ? m_backgroundOverride : color(defaultBackground);
    defaultBackgroundColor.setAlphaF(static_cast<float>(m_backgroundOpacity));
    const bool cursorOnlyPaint = !m_fullInvalidationPending && !node->image.isNull() && node->pixelSize == pixelSize
                                 && m_snapshot && m_snapshot->cursor.row < m_snapshot->rows;
    if (cursorOnlyPaint && m_preeditText.isEmpty())
    {
        if (node->cursorNode != nullptr)
        {
            node->cursorNode->setRect((!m_cursorBlink || m_cursorBlinkPhase) ? node->cursorRect : QRectF{});
        }
        const qint64 paintNanoseconds = timingEnabled ? frameTimer.nsecsElapsed() : 0;
        if (m_renderMetrics.enabled())
        {
            m_renderMetrics.recordFrame(std::chrono::nanoseconds{paintNanoseconds}, std::chrono::nanoseconds{0}, 0,
                                        terminal::TerminalDamageKind::partial, 1);
        }
#if !defined(NDEBUG)
        node->recordTiming(paintNanoseconds, 0, pixelSize, terminal::TerminalDamageKind::partial, 1);
#endif
        node->revision = m_revision;
        m_fullInvalidationPending = false;
        return node;
    }
    static const bool paintPhaseDiagnosticEnabled =
        qEnvironmentVariableIntValue("ZTERMY_PERFORMANCE_PAINT_PHASE_DIAGNOSTIC") == 1;
    const bool collectPaintPhases = paintPhaseDiagnosticEnabled && m_renderMetrics.enabled() && !cursorOnlyPaint;
    qint64 phaseMarkNanoseconds = collectPaintPhases ? frameTimer.nsecsElapsed() : 0;
    qint64 imagePreparationNanoseconds = 0;
    qint64 snapshotPreparationNanoseconds = 0;
    qint64 backgroundPaintNanoseconds = 0;
    qint64 textPaintNanoseconds = 0;
    qint64 overlayPaintNanoseconds = 0;
    if (!cursorOnlyPaint)
    {
        static const bool rowReuseDiagnosticEnabled =
            qEnvironmentVariableIntValue("ZTERMY_PERFORMANCE_ROW_REUSE_DIAGNOSTIC") == 1;
        if (rowReuseDiagnosticEnabled && m_renderMetrics.enabled() && m_snapshot && node->previousDiagnosticSnapshot)
        {
            const TerminalRowReuseAnalysis analysis =
                analyzeTerminalRowReuse(*node->previousDiagnosticSnapshot, *m_snapshot);
            m_renderMetrics.recordRowReuse(analysis.totalRows, analysis.reusableRows, analysis.shifted());
        }
        node->previousDiagnosticSnapshot = rowReuseDiagnosticEnabled ? m_snapshot : nullptr;
        node->image = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
        node->image.setDevicePixelRatio(devicePixelRatio);
        node->image.fill(defaultBackgroundColor);
    }
    if (collectPaintPhases)
    {
        const qint64 now = frameTimer.nsecsElapsed();
        imagePreparationNanoseconds = now - phaseMarkNanoseconds;
        phaseMarkNanoseconds = now;
    }
    QImage &image = node->image;

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
        const bool terminalCursorPresent = preeditClusters.empty() && m_snapshot->cursor.visible
                                           && m_snapshot->cursor.column < m_snapshot->columns
                                           && m_snapshot->cursor.row < m_snapshot->rows;
        if (!cursorOnlyPaint)
        {
            node->keywordStyles = highlightTerminalKeywords(*m_snapshot, m_keywordHighlightRules);
        }
        const std::vector<TerminalKeywordCellStyle> &keywordStyles = node->keywordStyles;
        const quint16 firstRow = cursorOnlyPaint ? m_snapshot->cursor.row : 0;
        const quint16 lastRow = cursorOnlyPaint ? static_cast<quint16>(firstRow + 1) : m_snapshot->rows;
        if (collectPaintPhases)
        {
            const qint64 now = frameTimer.nsecsElapsed();
            snapshotPreparationNanoseconds = now - phaseMarkNanoseconds;
            phaseMarkNanoseconds = now;
        }
        if (cursorOnlyPaint)
        {
            painter.fillRect(QRectF{0.0, verticalPadding + (firstRow * cellHeightValue), width(), cellHeightValue},
                             defaultBackgroundColor);
        }

        for (quint16 row = firstRow; row < lastRow; ++row)
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
                const QRectF cellRect{
                    horizontalPadding + (displayColumn * cellWidthValue),
                    verticalPadding + (row * cellHeightValue),
                    std::max<qreal>(1.0, cell.displayWidth) * cellWidthValue,
                    cellHeightValue,
                };
                if (cell.selected)
                {
                    painter.fillRect(cellRect, selectionBackground);
                }
                else if (const TerminalKeywordCellStyle &style =
                             keywordStyles[(static_cast<std::size_t>(row) * m_snapshot->columns) + column];
                         style.background.isValid())
                {
                    painter.fillRect(cellRect, style.background);
                }
                else if (cell.explicitBackground)
                {
                    painter.fillRect(cellRect, color(cell.background));
                }
            }
        }
        if (collectPaintPhases)
        {
            const qint64 now = frameTimer.nsecsElapsed();
            backgroundPaintNanoseconds = now - phaseMarkNanoseconds;
            phaseMarkNanoseconds = now;
        }

        for (quint16 row = firstRow; row < lastRow; ++row)
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
                const TerminalKeywordCellStyle &keywordStyle =
                    keywordStyles[(static_cast<std::size_t>(row) * m_snapshot->columns) + column];
                const QColor cellForeground =
                    m_foregroundOverride.isValid() && sameColor(cell.foreground, defaultForeground)
                        ? m_foregroundOverride
                        : color(cell.foreground);
                painter.setPen(cell.selected                       ? selectionForeground
                               : keywordStyle.foreground.isValid() ? keywordStyle.foreground
                                                                   : cellForeground);

                QString grapheme =
                    QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
                quint16 runEnd = column;
                int previousDisplayColumn = displayColumn;
                const bool currentCellHasCursor =
                    terminalCursorPresent && row == m_snapshot->cursor.row && column == m_snapshot->cursor.column;
                if (m_ligaturesEnabled && ligatureRunCell(cell) && !currentCellHasCursor)
                {
                    while (runEnd + 1 < m_snapshot->columns)
                    {
                        const terminal::TerminalCell &next = m_snapshot->cell(runEnd + 1, row);
                        const bool nextCellHasCursor = terminalCursorPresent && row == m_snapshot->cursor.row
                                                       && runEnd + 1 == m_snapshot->cursor.column;
                        const int nextDisplayColumn =
                            !preeditClusters.empty() && row == m_snapshot->cursor.row
                                ? shiftedTerminalColumn(runEnd + 1, m_snapshot->cursor.column, insertedColumns)
                                : runEnd + 1;
                        if (nextDisplayColumn >= snapshotColumns || nextDisplayColumn != previousDisplayColumn + 1
                            || nextCellHasCursor || !ligatureRunCell(next) || !sameTextStyle(cell, next))
                        {
                            break;
                        }
                        grapheme.append(QChar(static_cast<char16_t>(next.grapheme.front())));
                        ++runEnd;
                        previousDisplayColumn = nextDisplayColumn;
                    }
                }
                const QPointF baseline{horizontalPadding + (displayColumn * cellWidthValue),
                                       verticalPadding + (row * cellHeightValue) + metrics.ascent()};
                painter.drawText(baseline, grapheme);
                column = runEnd;
            }
        }
        if (collectPaintPhases)
        {
            const qint64 now = frameTimer.nsecsElapsed();
            textPaintNanoseconds = now - phaseMarkNanoseconds;
            phaseMarkNanoseconds = now;
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
                painter.fillRect(clusterRect, QColor(42, 91, 145, 180));
                painter.setPen(QColor(255, 255, 255));
                painter.drawText(QPointF(clusterRect.left(), compositionTop + metrics.ascent()), cluster.text);
            }
            if (m_preeditCursorVisible && (!m_cursorBlink || m_cursorBlinkPhase))
            {
                painter.fillRect(QRectF(compositionLeft + (cursorCell.column * cellWidthValue), compositionTop, 2.0,
                                        cellHeightValue),
                                 QColor(255, 255, 255));
            }
        }
        else if (terminalCursorPresent && !cursorOnlyPaint)
        {
            node->cursorRect = QRectF{horizontalPadding + (m_snapshot->cursor.column * cellWidthValue),
                                      verticalPadding + (m_snapshot->cursor.row * cellHeightValue),
                                      m_snapshot->cursor.width * cellWidthValue, cellHeightValue};
            const QSize cursorPixelSize{
                std::max(1, qRound(node->cursorRect.width() * devicePixelRatio)),
                std::max(1, qRound(node->cursorRect.height() * devicePixelRatio)),
            };
            QImage cursorImage(cursorPixelSize, QImage::Format_ARGB32_Premultiplied);
            cursorImage.setDevicePixelRatio(devicePixelRatio);
            cursorImage.fill(Qt::transparent);
            QPainter cursorPainter(&cursorImage);
            cursorPainter.setRenderHint(QPainter::TextAntialiasing);
            const QRectF cursorCell{0.0, 0.0, node->cursorRect.width(), node->cursorRect.height()};
            cursorPainter.setPen(QPen(color(m_snapshot->cursor.color), 1.0));
            switch (effectiveCursorStyle())
            {
                case terminal::TerminalCursorStyle::bar:
                    cursorPainter.fillRect(QRectF(cursorCell.left(), cursorCell.top(), 2.0, cursorCell.height()),
                                           color(m_snapshot->cursor.color));
                    break;
                case terminal::TerminalCursorStyle::underline:
                    cursorPainter.fillRect(
                        QRectF(cursorCell.left(), cursorCell.bottom() - 2.0, cursorCell.width(), 2.0),
                        color(m_snapshot->cursor.color));
                    break;
                case terminal::TerminalCursorStyle::hollowBlock:
                    cursorPainter.drawRect(cursorCell.adjusted(0.5, 0.5, -0.5, -0.5));
                    break;
                case terminal::TerminalCursorStyle::block:
                {
                    cursorPainter.fillRect(cursorCell, color(m_snapshot->cursor.color));
                    const terminal::TerminalCell &cell =
                        m_snapshot->cell(m_snapshot->cursor.column, m_snapshot->cursor.row);
                    if (!cell.grapheme.empty() && !cell.invisible)
                    {
                        QFont cursorFont = m_font;
                        cursorFont.setBold(cell.bold);
                        cursorFont.setItalic(cell.italic);
                        cursorFont.setUnderline(cell.underline);
                        cursorFont.setStrikeOut(cell.strikethrough);
                        cursorFont.setOverline(cell.overline);
                        cursorPainter.setFont(cursorFont);
                        cursorPainter.setPen(m_backgroundOverride.isValid()
                                                     && sameColor(cell.background, defaultBackground)
                                                 ? m_backgroundOverride
                                                 : color(cell.background));
                        cursorPainter.drawText(
                            QPointF(cursorCell.left(), metrics.ascent()),
                            QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size())));
                    }
                    break;
                }
            }
            if (node->cursorNode == nullptr)
            {
                node->cursorNode = new QSGSimpleTextureNode;
                node->cursorNode->setOwnsTexture(true);
                node->cursorNode->setFiltering(QSGTexture::Linear);
                node->appendChildNode(node->cursorNode);
            }
            node->cursorNode->setTexture(window()->createTextureFromImage(cursorImage));
        }
        if (node->cursorNode != nullptr)
        {
            node->cursorNode->setRect(terminalCursorPresent && (!m_cursorBlink || m_cursorBlinkPhase) ? node->cursorRect
                                                                                                      : QRectF{});
        }
        if (collectPaintPhases)
        {
            overlayPaintNanoseconds = frameTimer.nsecsElapsed() - phaseMarkNanoseconds;
        }
    }
    else if (node->cursorNode != nullptr)
    {
        node->cursorNode->setRect({});
        if (collectPaintPhases)
        {
            overlayPaintNanoseconds = frameTimer.nsecsElapsed() - phaseMarkNanoseconds;
        }
    }

    const qint64 paintNanoseconds = timingEnabled ? frameTimer.nsecsElapsed() : 0;
    if (collectPaintPhases)
    {
        m_renderMetrics.recordPaintPhases({
            .imagePreparation = std::chrono::nanoseconds{imagePreparationNanoseconds},
            .snapshotPreparation = std::chrono::nanoseconds{snapshotPreparationNanoseconds},
            .backgroundPaint = std::chrono::nanoseconds{backgroundPaintNanoseconds},
            .textPaint = std::chrono::nanoseconds{textPaintNanoseconds},
            .overlayPaint = std::chrono::nanoseconds{overlayPaintNanoseconds},
        });
    }
    QSGTexture *newTexture = window()->createTextureFromImage(image);
    const qint64 textureNanoseconds = timingEnabled ? frameTimer.nsecsElapsed() - paintNanoseconds : 0;
    const terminal::TerminalDamageKind damage = cursorOnlyPaint ? terminal::TerminalDamageKind::partial
                                                : m_snapshot    ? m_snapshot->damage
                                                                : terminal::TerminalDamageKind::full;
    const std::size_t damagedRowCount = cursorOnlyPaint ? std::size_t{1}
                                        : m_snapshot    ? m_snapshot->damagedRows.size()
                                                        : std::size_t{0};
    if (m_renderMetrics.enabled())
    {
        const auto pixelCount =
            static_cast<std::uint64_t>(pixelSize.width()) * static_cast<std::uint64_t>(pixelSize.height());
        m_renderMetrics.recordFrame(std::chrono::nanoseconds{paintNanoseconds},
                                    std::chrono::nanoseconds{textureNanoseconds}, pixelCount, damage, damagedRowCount);
    }
#if !defined(NDEBUG)
    node->recordTiming(paintNanoseconds, textureNanoseconds, pixelSize, damage, damagedRowCount);
#endif
    node->setTexture(newTexture);
    node->setRect(boundingRect());
    node->setFiltering(QSGTexture::Linear);
    node->revision = m_revision;
    node->pixelSize = pixelSize;
    m_fullInvalidationPending = false;
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
    dismissSelectionAction();
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
        const QByteArray bytes = readClipboardText().toUtf8();
        if (!bytes.isEmpty())
        {
            const qsizetype lineBreaks = bytes.count('\n') > 0 ? bytes.count('\n') : bytes.count('\r');
            if (m_confirmMultilinePaste && lineBreaks > 0)
            {
                m_pendingMultilinePaste = bytes;
                const qsizetype maximumLineCount = std::numeric_limits<int>::max();
                emit multilinePasteConfirmationRequested(static_cast<int>(std::min(lineBreaks + 1, maximumLineCount)));
            }
            else
            {
                emit pasteRequested(bytes);
            }
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

QString TerminalItem::readClipboardText() const
{
    return QGuiApplication::clipboard()->text(QClipboard::Clipboard);
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *event)
{
    m_preeditText = event->preeditString();
    m_preeditCursorPosition = m_preeditText.size();
    m_preeditCursorVisible = true;
    m_cursorBlinkPhase = true;
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

    invalidateRenderer(true);
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
    dismissSelectionAction();
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
            const bool nonEmpty = point->column != m_selectionAnchor.column || point->row != m_selectionAnchor.row;
            if (nonEmpty)
            {
                m_selectionActionPosition = event->position();
                m_selectionActionVisible = true;
                emit selectionActionChanged();
            }
        }
        if (m_copyOnSelect)
        {
            emit copyRequested();
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
    invalidateRenderer(true);
}

void TerminalItem::invalidateRenderer(const bool full)
{
    m_fullInvalidationPending = m_fullInvalidationPending || full;
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

terminal::TerminalCursorStyle TerminalItem::effectiveCursorStyle() const noexcept
{
    if (m_cursorPreference == QStringLiteral("block"))
    {
        return terminal::TerminalCursorStyle::block;
    }
    if (m_cursorPreference == QStringLiteral("bar"))
    {
        return terminal::TerminalCursorStyle::bar;
    }
    if (m_cursorPreference == QStringLiteral("underline"))
    {
        return terminal::TerminalCursorStyle::underline;
    }
    return m_snapshot ? m_snapshot->cursor.style : terminal::TerminalCursorStyle::block;
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
