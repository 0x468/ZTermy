#pragma once

#include <QFont>
#include <QString>

#include <vector>

namespace ztermy::ui
{

struct PreeditCluster
{
    QString text;
    qsizetype start = 0;
    qsizetype length = 0;
    int column = 0;
    int width = 1;
};

struct PreeditCursorCell
{
    int column = 0;
    int width = 1;
};

[[nodiscard]] std::vector<PreeditCluster> layoutPreeditText(const QString &text, const QFont &font, qreal cellWidth);
[[nodiscard]] int preeditColumnCount(const std::vector<PreeditCluster> &clusters) noexcept;
[[nodiscard]] PreeditCursorCell preeditCursorCell(const std::vector<PreeditCluster> &clusters,
                                                  qsizetype cursorPosition) noexcept;
[[nodiscard]] int shiftedTerminalColumn(int sourceColumn, int insertionColumn, int insertedColumns) noexcept;

} // namespace ztermy::ui
