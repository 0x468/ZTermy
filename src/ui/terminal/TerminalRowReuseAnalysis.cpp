#include "ui/terminal/TerminalRowReuseAnalysis.h"

#include <algorithm>
#include <cstdlib>
#include <span>

namespace ztermy::ui
{
namespace
{

[[nodiscard]] bool sameRow(const terminal::TerminalSnapshot &previous, const std::size_t previousRow,
                           const terminal::TerminalSnapshot &current, const std::size_t currentRow)
{
    const std::size_t columns = current.columns;
    const auto previousCells = std::span(previous.cells).subspan(previousRow * columns, columns);
    const auto currentCells = std::span(current.cells).subspan(currentRow * columns, columns);
    return std::ranges::equal(previousCells, currentCells);
}

[[nodiscard]] bool betterCandidate(const std::size_t matches, const int shift,
                                   const TerminalRowReuseAnalysis &best) noexcept
{
    if (matches != best.reusableRows)
    {
        return matches > best.reusableRows;
    }
    const int candidateDistance = std::abs(shift);
    const int bestDistance = std::abs(best.rowShift);
    return candidateDistance < bestDistance || (candidateDistance == bestDistance && shift < best.rowShift);
}

} // namespace

TerminalRowReuseAnalysis analyzeTerminalRowReuse(const terminal::TerminalSnapshot &previous,
                                                 const terminal::TerminalSnapshot &current)
{
    TerminalRowReuseAnalysis best{.totalRows = current.rows};
    const std::size_t expectedCells = static_cast<std::size_t>(current.columns) * current.rows;
    if (previous.columns != current.columns || previous.rows != current.rows
        || previous.defaultForeground != current.defaultForeground
        || previous.defaultBackground != current.defaultBackground || previous.cells.size() != expectedCells
        || current.cells.size() != expectedCells || current.columns == 0 || current.rows == 0)
    {
        return best;
    }

    const int rows = current.rows;
    for (int shift = -(rows - 1); shift < rows; ++shift)
    {
        std::size_t matches = 0;
        for (int currentRow = 0; currentRow < rows; ++currentRow)
        {
            const int previousRow = currentRow + shift;
            if (previousRow >= 0 && previousRow < rows
                && sameRow(previous, static_cast<std::size_t>(previousRow), current,
                           static_cast<std::size_t>(currentRow)))
            {
                ++matches;
            }
        }
        if (betterCandidate(matches, shift, best))
        {
            best.rowShift = shift;
            best.reusableRows = matches;
        }
    }
    return best;
}

} // namespace ztermy::ui
