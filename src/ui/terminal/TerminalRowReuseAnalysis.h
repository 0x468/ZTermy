#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <cstddef>

namespace ztermy::ui
{

// Snapshot-only upper bound for a future CPU backing-store renderer. It does
// not account for presentation changes such as fonts or keyword rules, so it
// is diagnostic evidence rather than a rendering decision.
struct TerminalRowReuseAnalysis final
{
    int rowShift = 0;
    std::size_t totalRows = 0;
    std::size_t reusableRows = 0;

    [[nodiscard]] std::size_t repaintRows() const noexcept { return totalRows - reusableRows; }
    [[nodiscard]] bool shifted() const noexcept { return rowShift != 0 && reusableRows != 0; }
};

[[nodiscard]] TerminalRowReuseAnalysis analyzeTerminalRowReuse(const terminal::TerminalSnapshot &previous,
                                                               const terminal::TerminalSnapshot &current);

} // namespace ztermy::ui
