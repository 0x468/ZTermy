#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <QColor>
#include <QString>

#include <cstddef>
#include <vector>

namespace ztermy::ui
{

struct TerminalKeywordRule final
{
    QString id;
    QString pattern;
    QColor foreground;
    QColor background;
    bool enabled = true;
    bool caseSensitive = false;
};

struct TerminalKeywordCellStyle final
{
    QColor foreground;
    QColor background;
};

inline constexpr std::size_t maximumKeywordRules = 16;
inline constexpr qsizetype maximumKeywordPatternLength = 128;
inline constexpr std::size_t maximumKeywordMatchesPerViewport = 512;

[[nodiscard]] std::vector<TerminalKeywordCellStyle>
highlightTerminalKeywords(const terminal::TerminalSnapshot &snapshot, const std::vector<TerminalKeywordRule> &rules);

} // namespace ztermy::ui
