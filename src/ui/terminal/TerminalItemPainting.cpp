#include "ui/terminal/TerminalItem.h"
#include "ui/terminal/TerminalLayoutMetrics.h"
#include "ui/terminal/TerminalQuickSelect.h"
#include "ui/terminal/TerminalRowReuseAnalysis.h"
#include "ui/terminal/TerminalTextLayout.h"

#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
Q_LOGGING_CATEGORY(terminalRenderLog, "ztermy.terminal.render")

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

[[nodiscard]] double relativeLuminance(const QColor &value)
{
    static const std::array<double, 256> linearChannel = [] {
        std::array<double, 256> result{};
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const double channel = static_cast<double>(index) / 255.0;
            result[index] = channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
        }
        return result;
    }();
    return (0.2126 * linearChannel[static_cast<std::size_t>(value.red())])
           + (0.7152 * linearChannel[static_cast<std::size_t>(value.green())])
           + (0.0722 * linearChannel[static_cast<std::size_t>(value.blue())]);
}

[[nodiscard]] QColor readableOnLightBackground(const QColor foreground, const double backgroundLuminance,
                                               const double minimumContrast = 5.5)
{
    const double maximumForegroundLuminance = ((backgroundLuminance + 0.05) / minimumContrast) - 0.05;
    if (relativeLuminance(foreground) <= maximumForegroundLuminance)
        return foreground;

    double readableScale = 0.0;
    double unreadableScale = 1.0;
    QColor adjusted = foreground;
    for (int step = 0; step < 9; ++step)
    {
        const double scale = (readableScale + unreadableScale) / 2.0;
        const QColor candidate(qRound(foreground.red() * scale), qRound(foreground.green() * scale),
                               qRound(foreground.blue() * scale));
        if (relativeLuminance(candidate) <= maximumForegroundLuminance)
        {
            readableScale = scale;
            adjusted = candidate;
        }
        else
        {
            unreadableScale = scale;
        }
    }
    return adjusted;
}

[[nodiscard]] QColor faintForeground(const QColor foreground, const QColor background)
{
    constexpr double foregroundWeight = 0.55;
    const auto blend = [foregroundWeight](const int ink, const int surface) {
        return qRound((foregroundWeight * ink) + ((1.0 - foregroundWeight) * surface));
    };
    QColor result(blend(foreground.red(), background.red()), blend(foreground.green(), background.green()),
                  blend(foreground.blue(), background.blue()));
    const double backgroundLuminance = relativeLuminance(background);
    if (backgroundLuminance > 0.5)
        result = readableOnLightBackground(result, backgroundLuminance, 4.5);
    return result;
}

void paintUnderline(QPainter &painter, const ztermy::terminal::TerminalUnderlineStyle style, const QColor ink,
                    const QRectF cellRect, const qreal baseline, const qreal devicePixelRatio)
{
    if (style == ztermy::terminal::TerminalUnderlineStyle::none)
        return;
    const qreal pixel = 1.0 / devicePixelRatio;
    const qreal bottom = cellRect.bottom() - pixel;
    const qreal y = std::min(bottom, baseline + pixel);
    painter.save();
    QPen pen(ink, pixel);
    if (style == ztermy::terminal::TerminalUnderlineStyle::dotted)
        pen.setStyle(Qt::DotLine);
    else if (style == ztermy::terminal::TerminalUnderlineStyle::dashed)
        pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    if (style == ztermy::terminal::TerminalUnderlineStyle::curly)
    {
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal center = std::min(bottom - pixel, y + pixel);
        const qreal period = 6.0 * pixel;
        QPainterPath wave(QPointF{cellRect.left(), center});
        const int waveSegments = std::max(1, qCeil(cellRect.width() / period));
        for (int segment = 0; segment < waveSegments; ++segment)
        {
            const qreal x = cellRect.left() + (segment * period);
            const qreal step = std::min(period, cellRect.right() - x);
            wave.quadTo(x + (step * 0.25), center - pixel, x + (step * 0.5), center);
            wave.quadTo(x + (step * 0.75), center + pixel, x + step, center);
        }
        painter.drawPath(wave);
    }
    else if (style == ztermy::terminal::TerminalUnderlineStyle::doubleLine)
    {
        const qreal first = std::min(y, bottom - (2.0 * pixel));
        painter.drawLine(QPointF{cellRect.left(), first}, QPointF{cellRect.right(), first});
        painter.drawLine(QPointF{cellRect.left(), first + (2.0 * pixel)},
                         QPointF{cellRect.right(), first + (2.0 * pixel)});
    }
    else
    {
        painter.drawLine(QPointF{cellRect.left(), y}, QPointF{cellRect.right(), y});
    }
    painter.restore();
}

[[nodiscard]] bool sameColor(const ztermy::terminal::TerminalColor left,
                             const ztermy::terminal::TerminalColor right) noexcept
{
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

[[nodiscard]] bool ligatureRunCell(const ztermy::terminal::TerminalCell &cell)
{
    return !cell.invisible && cell.displayWidth == 1 && cell.grapheme.size() == 1 && cell.grapheme.front() >= U'!'
           && cell.grapheme.front() <= U'~';
}

[[nodiscard]] bool sameTextStyle(const ztermy::terminal::TerminalCell &first,
                                 const ztermy::terminal::TerminalCell &second)
{
    return first.bold == second.bold && first.italic == second.italic && first.faint == second.faint
           && first.underlineStyle == second.underlineStyle && first.underlineColor == second.underlineColor
           && first.strikethrough == second.strikethrough && first.overline == second.overline
           && first.hyperlinkId == second.hyperlinkId && first.foreground == second.foreground;
}

} // namespace

namespace ztermy::ui
{
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
    const QColor &selectionBackground = m_selectionBackground;
    const QColor &selectionForeground = m_selectionForeground;
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
            node->cursorNode->setRect(
                m_terminalCursorVisible && (!m_cursorBlink || m_cursorBlinkPhase) ? node->cursorRect : QRectF{});
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
        // Reuse the backing store when the size is unchanged; the previous
        // texture owned the only other reference and has been uploaded.
        if (node->image.size() != pixelSize || node->image.format() != QImage::Format_ARGB32_Premultiplied)
        {
            node->image = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
        }
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
        const qreal ascent = m_fontAscent;
        const std::vector<PreeditCluster> preeditClusters = layoutPreeditText(m_preeditText, m_font, cellWidthValue);
        const int insertedColumns = preeditColumnCount(preeditClusters);
        const int snapshotColumns = static_cast<int>(m_snapshot->columns);
        const bool terminalCursorPresent =
            m_terminalCursorVisible && preeditClusters.empty() && m_snapshot->cursor.visible
            && m_snapshot->cursor.column < m_snapshot->columns && m_snapshot->cursor.row < m_snapshot->rows;
        const bool cursorRepaintsCell =
            terminalCursorPresent && effectiveCursorStyle() == terminal::TerminalCursorStyle::block;
        QString cursorLigatureText;
        QFont cursorLigatureFont;
        quint16 cursorLigatureStartColumn = 0;
        if (m_searchStylesDirty)
        {
            refreshSearchStyles();
        }
        // Empty vectors mean "no rule matched anything"; the highlighter
        // skips the per-cell allocation when there are no rules.
        const std::size_t cellCount = static_cast<std::size_t>(m_snapshot->columns) * m_snapshot->rows;
        const TerminalKeywordCellStyle *const keywordStyles =
            m_keywordStyles.size() == cellCount ? m_keywordStyles.data() : nullptr;
        const TerminalKeywordCellStyle *const searchStyles =
            m_searchStyles.size() == cellCount ? m_searchStyles.data() : nullptr;
        static const TerminalKeywordCellStyle noStyle{};
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
            // Adjacent cells with the same background collapse into one fill.
            QRectF runRect;
            QColor runColor;
            const auto flushRun = [&painter, &runRect, &runColor] {
                if (runColor.isValid())
                {
                    painter.fillRect(runRect, runColor);
                    runColor = QColor{};
                }
            };
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
                const std::size_t styleIndex = (static_cast<std::size_t>(row) * m_snapshot->columns) + column;
                const TerminalKeywordCellStyle &searchStyle = searchStyles ? searchStyles[styleIndex] : noStyle;
                const TerminalKeywordCellStyle &keywordStyle = keywordStyles ? keywordStyles[styleIndex] : noStyle;
                const bool currentSearchCell = m_snapshot->searchSelectionPresent && cell.selected;
                const QColor background = currentSearchCell                   ? m_searchCurrentBackground
                                          : cell.selected                     ? selectionBackground
                                          : searchStyle.background.isValid()  ? searchStyle.background
                                          : keywordStyle.background.isValid() ? keywordStyle.background
                                          : cell.explicitBackground           ? color(cell.background)
                                                                              : QColor{};
                if (!background.isValid())
                {
                    flushRun();
                    continue;
                }
                const QRectF cellRect{
                    horizontalPadding + (displayColumn * cellWidthValue),
                    verticalPadding + (row * cellHeightValue),
                    std::max<qreal>(1.0, cell.displayWidth) * cellWidthValue,
                    cellHeightValue,
                };
                if (runColor.isValid() && runColor == background && qFuzzyCompare(runRect.right(), cellRect.left()))
                {
                    runRect.setRight(cellRect.right());
                    continue;
                }
                flushRun();
                runRect = cellRect;
                runColor = background;
            }
            flushRun();
        }
        if (collectPaintPhases)
        {
            const qint64 now = frameTimer.nsecsElapsed();
            backgroundPaintNanoseconds = now - phaseMarkNanoseconds;
            phaseMarkNanoseconds = now;
        }

        std::size_t activeStyleBits = std::numeric_limits<std::size_t>::max();
        QColor activePen;
        // The default pane is transparent, so use its palette RGB as the contrast reference even without a fill.
        const double defaultBackgroundLuminance = relativeLuminance(defaultBackgroundColor);
        QHash<quint64, QColor> readableForegrounds;
        QString grapheme;
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

                const bool hoverUnderline = cell.hyperlinkId != 0 && cell.hyperlinkId == m_hoveredLinkId;
                const bool fontUnderline =
                    (cell.underlineStyle == terminal::TerminalUnderlineStyle::single && !cell.underlineColor)
                    || (hoverUnderline && cell.underlineStyle == terminal::TerminalUnderlineStyle::none);
                const std::size_t styleBits = (cell.bold ? styleBold : 0U) | (cell.italic ? styleItalic : 0U)
                                              | (fontUnderline ? styleUnderline : 0U)
                                              | (cell.strikethrough ? styleStrikeOut : 0U)
                                              | (cell.overline ? styleOverline : 0U);
                if (styleBits != activeStyleBits)
                {
                    painter.setFont(styledFont(styleBits));
                    activeStyleBits = styleBits;
                }
                const std::size_t styleIndex = (static_cast<std::size_t>(row) * m_snapshot->columns) + column;
                const TerminalKeywordCellStyle &searchStyle = searchStyles ? searchStyles[styleIndex] : noStyle;
                const TerminalKeywordCellStyle &keywordStyle = keywordStyles ? keywordStyles[styleIndex] : noStyle;
                const QColor cellForeground =
                    m_foregroundOverride.isValid() && sameColor(cell.foreground, defaultForeground)
                        ? m_foregroundOverride
                        : color(cell.foreground);
                QColor normalPen = keywordStyle.foreground.isValid() ? keywordStyle.foreground : cellForeground;
                // Explicit cell and highlight backgrounds can be just as light as the default pane surface.
                const QColor inkBackground = searchStyle.background.isValid()    ? searchStyle.background
                                             : keywordStyle.background.isValid() ? keywordStyle.background
                                             : cell.explicitBackground           ? color(cell.background)
                                                                                 : defaultBackgroundColor;
                const double backgroundLuminance = inkBackground == defaultBackgroundColor
                                                       ? defaultBackgroundLuminance
                                                       : relativeLuminance(inkBackground);
                if (backgroundLuminance > 0.5 && !cell.selected)
                {
                    const quint64 cacheKey =
                        (quint64{normalPen.rgb() & 0x00ffffffU} << 24U) | quint64{inkBackground.rgb() & 0x00ffffffU};
                    auto cached = readableForegrounds.constFind(cacheKey);
                    if (cached == readableForegrounds.cend())
                    {
                        cached = readableForegrounds.insert(cacheKey,
                                                            readableOnLightBackground(normalPen, backgroundLuminance));
                    }
                    normalPen = *cached;
                }
                if (cell.faint)
                {
                    normalPen = faintForeground(normalPen, inkBackground);
                }
                const QColor selectedPen =
                    m_snapshot->searchSelectionPresent ? m_searchCurrentForeground : selectionForeground;
                const QColor pen = cell.selected ? selectedPen : normalPen;
                if (pen != activePen)
                {
                    painter.setPen(pen);
                    activePen = pen;
                }

                grapheme.resize(0);
                for (const char32_t codePoint : cell.grapheme)
                {
                    grapheme.append(QChar::fromUcs4(codePoint));
                }
                quint16 runEnd = column;
                int previousDisplayColumn = displayColumn;
                bool mixedSelection = false;
                if (m_ligaturesEnabled && ligatureRunCell(cell))
                {
                    while (runEnd + 1 < m_snapshot->columns)
                    {
                        const terminal::TerminalCell &next = m_snapshot->cell(runEnd + 1, row);
                        const std::size_t nextStyleIndex = styleIndex + (runEnd + 1 - column);
                        const TerminalKeywordCellStyle &nextSearchStyle =
                            searchStyles ? searchStyles[nextStyleIndex] : noStyle;
                        const TerminalKeywordCellStyle &nextKeywordStyle =
                            keywordStyles ? keywordStyles[nextStyleIndex] : noStyle;
                        const int nextDisplayColumn =
                            !preeditClusters.empty() && row == m_snapshot->cursor.row
                                ? shiftedTerminalColumn(runEnd + 1, m_snapshot->cursor.column, insertedColumns)
                                : runEnd + 1;
                        if (nextDisplayColumn >= snapshotColumns || nextDisplayColumn != previousDisplayColumn + 1
                            || !ligatureRunCell(next) || !sameTextStyle(cell, next)
                            || cell.explicitBackground != next.explicitBackground
                            || searchStyle.background != nextSearchStyle.background
                            || keywordStyle.background != nextKeywordStyle.background
                            || keywordStyle.foreground != nextKeywordStyle.foreground)
                        {
                            break;
                        }
                        grapheme.append(QChar(static_cast<char16_t>(next.grapheme.front())));
                        mixedSelection = mixedSelection || next.selected != cell.selected;
                        ++runEnd;
                        previousDisplayColumn = nextDisplayColumn;
                    }
                }
                if (cursorRepaintsCell && row == m_snapshot->cursor.row && column <= m_snapshot->cursor.column
                    && m_snapshot->cursor.column <= runEnd && runEnd > column)
                {
                    cursorLigatureText = grapheme;
                    cursorLigatureFont = painter.font();
                    cursorLigatureStartColumn = column;
                }
                const QPointF baseline{horizontalPadding + (displayColumn * cellWidthValue),
                                       verticalPadding + (row * cellHeightValue) + ascent};
                if (!mixedSelection)
                {
                    painter.drawText(baseline, grapheme);
                }
                else
                {
                    // Shape the full run for both colors, then crop pixels per cell so selection cannot reshape it.
                    const qreal runWidth = (runEnd - column + 1) * cellWidthValue;
                    QImage runImage(QSize{std::max(1, qCeil(runWidth * devicePixelRatio)),
                                          std::max(1, qCeil(cellHeightValue * devicePixelRatio))},
                                    QImage::Format_ARGB32_Premultiplied);
                    runImage.setDevicePixelRatio(devicePixelRatio);
                    for (const bool selectedPass : {false, true})
                    {
                        runImage.fill(Qt::transparent);
                        QPainter runPainter(&runImage);
                        runPainter.setRenderHint(QPainter::TextAntialiasing);
                        runPainter.setFont(painter.font());
                        runPainter.setPen(selectedPass ? selectedPen : normalPen);
                        runPainter.drawText(QPointF{0.0, ascent}, grapheme);
                        runPainter.end();

                        const int runLast = runEnd;
                        int segmentStart = column;
                        while (segmentStart <= runLast)
                        {
                            const bool selected = m_snapshot->cell(static_cast<quint16>(segmentStart), row).selected;
                            int segmentEnd = segmentStart;
                            while (segmentEnd < runLast
                                   && m_snapshot->cell(static_cast<quint16>(segmentEnd + 1), row).selected == selected)
                            {
                                ++segmentEnd;
                            }
                            if (selected == selectedPass)
                            {
                                const qreal offset = (segmentStart - column) * cellWidthValue;
                                const qreal segmentWidth = (segmentEnd - segmentStart + 1) * cellWidthValue;
                                painter.drawImage(
                                    QRectF{baseline.x() + offset, verticalPadding + (row * cellHeightValue),
                                           segmentWidth, cellHeightValue},
                                    runImage,
                                    QRectF{offset * devicePixelRatio, 0.0, segmentWidth * devicePixelRatio,
                                           cellHeightValue * devicePixelRatio});
                            }
                            segmentStart = segmentEnd + 1;
                        }
                    }
                }
                if (cell.underlineStyle != terminal::TerminalUnderlineStyle::none && !fontUnderline)
                {
                    const int runLast = runEnd;
                    int segmentStart = column;
                    while (segmentStart <= runLast)
                    {
                        const bool selected = m_snapshot->cell(static_cast<quint16>(segmentStart), row).selected;
                        int segmentEnd = segmentStart;
                        while (segmentEnd < runLast
                               && m_snapshot->cell(static_cast<quint16>(segmentEnd + 1), row).selected == selected)
                            ++segmentEnd;
                        const QColor underlineInk = cell.underlineColor ? color(*cell.underlineColor)
                                                    : selected          ? selectedPen
                                                                        : normalPen;
                        const QRectF underlineRect{baseline.x() + ((segmentStart - column) * cellWidthValue),
                                                   verticalPadding + (row * cellHeightValue),
                                                   (segmentEnd - segmentStart + 1) * cellWidthValue, cellHeightValue};
                        paintUnderline(painter, cell.underlineStyle, underlineInk, underlineRect, baseline.y(),
                                       devicePixelRatio);
                        segmentStart = segmentEnd + 1;
                    }
                }
                column = runEnd;
            }
        }
        if (!cursorOnlyPaint && m_quickSelectActive)
        {
            QFont labelFont = m_font;
            labelFont.setBold(true);
            labelFont.setPixelSize(std::max(10, m_font.pixelSize() - 1));
            painter.setFont(labelFont);
            const QFontMetricsF labelMetrics(labelFont);
            for (const TerminalQuickSelectTarget &target : m_quickSelectTargets)
            {
                if (!target.label.startsWith(m_quickSelectInput, Qt::CaseInsensitive))
                {
                    continue;
                }
                const QRectF targetRect{
                    horizontalPadding + (target.startColumn * cellWidthValue),
                    verticalPadding + (target.row * cellHeightValue),
                    std::max<qreal>(cellWidthValue, (target.endColumn - target.startColumn) * cellWidthValue),
                    cellHeightValue,
                };
                painter.fillRect(targetRect, QColor(34, 197, 94, 52));
                const qreal labelWidth = labelMetrics.horizontalAdvance(target.label) + 8.0;
                const QRectF labelRect{targetRect.left(), targetRect.top(), labelWidth, cellHeightValue};
                painter.fillRect(labelRect, QColor(15, 23, 42, 235));
                painter.setPen(QColor(248, 250, 252));
                painter.drawText(QPointF(labelRect.left() + 4.0, labelRect.top() + labelMetrics.ascent()),
                                 target.label);
            }
        }
        if (!cursorOnlyPaint && m_copyModeActive)
        {
            QFont modeFont = m_font;
            modeFont.setBold(true);
            modeFont.setPixelSize(std::max(10, m_font.pixelSize() - 2));
            painter.setFont(modeFont);
            const QString label = tr("COPY MODE  ·  Esc cancel  ·  Y copy");
            const QFontMetricsF modeMetrics(modeFont);
            const QSizeF labelSize{modeMetrics.horizontalAdvance(label) + 18.0, modeMetrics.height() + 8.0};
            const QRectF labelRect{std::max<qreal>(horizontalPadding, width() - horizontalPadding - labelSize.width()),
                                   verticalPadding, labelSize.width(), labelSize.height()};
            painter.setPen(QColor(74, 222, 128));
            painter.setBrush(QColor(15, 23, 42, 230));
            painter.drawRoundedRect(labelRect, 6.0, 6.0);
            painter.drawText(QPointF(labelRect.left() + 9.0, labelRect.top() + 4.0 + modeMetrics.ascent()), label);
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
                painter.drawText(QPointF(clusterRect.left(), compositionTop + ascent), cluster.text);
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
            const QRectF logicalCursorRect{horizontalPadding + (m_snapshot->cursor.column * cellWidthValue),
                                           verticalPadding + (m_snapshot->cursor.row * cellHeightValue),
                                           m_snapshot->cursor.width * cellWidthValue, cellHeightValue};
            const int pixelLeft = qFloor(logicalCursorRect.left() * devicePixelRatio);
            const int pixelTop = qFloor(logicalCursorRect.top() * devicePixelRatio);
            const int pixelRight = qCeil(logicalCursorRect.right() * devicePixelRatio);
            const int pixelBottom = qCeil(logicalCursorRect.bottom() * devicePixelRatio);
            const QSize cursorPixelSize{std::max(1, pixelRight - pixelLeft), std::max(1, pixelBottom - pixelTop)};
            node->cursorRect =
                QRectF{pixelLeft / devicePixelRatio, pixelTop / devicePixelRatio,
                       cursorPixelSize.width() / devicePixelRatio, cursorPixelSize.height() / devicePixelRatio};
            QImage cursorImage(cursorPixelSize, QImage::Format_ARGB32_Premultiplied);
            cursorImage.setDevicePixelRatio(devicePixelRatio);
            cursorImage.fill(Qt::transparent);
            QPainter cursorPainter(&cursorImage);
            cursorPainter.setRenderHint(QPainter::TextAntialiasing);
            const QRectF cursorCell{logicalCursorRect.x() - node->cursorRect.x(),
                                    logicalCursorRect.y() - node->cursorRect.y(), logicalCursorRect.width(),
                                    logicalCursorRect.height()};
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
                        cursorFont.setUnderline(cell.underlineStyle == terminal::TerminalUnderlineStyle::single
                                                && !cell.underlineColor);
                        cursorFont.setStrikeOut(cell.strikethrough);
                        cursorFont.setOverline(cell.overline);
                        cursorPainter.setFont(cursorFont);
                        cursorPainter.setPen(m_backgroundOverride.isValid()
                                                     && sameColor(cell.background, defaultBackground)
                                                 ? m_backgroundOverride
                                                 : color(cell.background));
                        if (!cursorLigatureText.isEmpty())
                        {
                            const qreal offset =
                                (m_snapshot->cursor.column - cursorLigatureStartColumn) * cellWidthValue;
                            cursorPainter.setFont(cursorLigatureFont);
                            cursorPainter.drawText(QPointF{cursorCell.left() - offset, cursorCell.top() + ascent},
                                                   cursorLigatureText);
                        }
                        else
                        {
                            cursorPainter.drawText(
                                QPointF(cursorCell.left(), cursorCell.top() + ascent),
                                QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size())));
                        }
                        if (cell.underlineStyle != terminal::TerminalUnderlineStyle::none
                            && (cell.underlineStyle != terminal::TerminalUnderlineStyle::single || cell.underlineColor))
                        {
                            const QColor underlineInk =
                                cell.underlineColor ? color(*cell.underlineColor) : cursorPainter.pen().color();
                            paintUnderline(cursorPainter, cell.underlineStyle, underlineInk, cursorCell,
                                           cursorCell.top() + ascent, devicePixelRatio);
                        }
                    }
                    break;
                }
            }
            if (node->cursorNode == nullptr)
            {
                node->cursorNode = new QSGSimpleTextureNode;
                node->cursorNode->setOwnsTexture(true);
                node->cursorNode->setFiltering(QSGTexture::Nearest);
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

} // namespace ztermy::ui
