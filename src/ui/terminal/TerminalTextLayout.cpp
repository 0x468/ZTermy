#include "ui/terminal/TerminalTextLayout.h"

#include <QFontMetricsF>
#include <QTextBoundaryFinder>

#include <algorithm>

namespace ztermy::ui
{

std::vector<PreeditCluster> layoutPreeditText(const QString &text, const QFont &font, const qreal cellWidth)
{
    std::vector<PreeditCluster> result;
    if (text.isEmpty() || cellWidth <= 0.0)
    {
        return result;
    }

    const QFontMetricsF metrics(font);
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, text);
    qsizetype start = 0;
    int column = 0;
    while (start < text.size())
    {
        boundaries.setPosition(start);
        qsizetype end = boundaries.toNextBoundary();
        if (end <= start)
        {
            end = start + 1;
        }

        const QString clusterText = text.mid(start, end - start);
        const int clusterWidth = std::clamp(qRound(metrics.horizontalAdvance(clusterText) / cellWidth), 1, 2);
        result.push_back({
            .text = clusterText,
            .start = start,
            .length = end - start,
            .column = column,
            .width = clusterWidth,
        });
        column += clusterWidth;
        start = end;
    }
    return result;
}

int preeditColumnCount(const std::vector<PreeditCluster> &clusters) noexcept
{
    if (clusters.empty())
    {
        return 0;
    }
    const PreeditCluster &last = clusters.back();
    return last.column + last.width;
}

PreeditCursorCell preeditCursorCell(const std::vector<PreeditCluster> &clusters,
                                    const qsizetype cursorPosition) noexcept
{
    for (const PreeditCluster &cluster : clusters)
    {
        if (cursorPosition >= cluster.start && cursorPosition < cluster.start + cluster.length)
        {
            return {.column = cluster.column, .width = cluster.width};
        }
    }
    return {.column = preeditColumnCount(clusters), .width = 1};
}

int shiftedTerminalColumn(const int sourceColumn, const int insertionColumn, const int insertedColumns) noexcept
{
    return sourceColumn >= insertionColumn ? sourceColumn + std::max(0, insertedColumns) : sourceColumn;
}

} // namespace ztermy::ui
