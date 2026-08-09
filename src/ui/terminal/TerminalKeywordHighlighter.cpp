#include "ui/terminal/TerminalKeywordHighlighter.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace ztermy::ui
{

std::vector<TerminalKeywordCellStyle> highlightTerminalKeywords(const terminal::TerminalSnapshot &snapshot,
                                                                const std::vector<TerminalKeywordRule> &rules)
{
    const std::size_t cellCount = static_cast<std::size_t>(snapshot.columns) * snapshot.rows;
    std::vector<TerminalKeywordCellStyle> styles(cellCount);
    if (snapshot.columns == 0 || snapshot.rows == 0 || rules.empty())
    {
        return styles;
    }

    std::size_t matchCount = 0;
    std::vector<bool> claimed(cellCount, false);
    const std::size_t ruleCount = std::min(rules.size(), maximumKeywordRules);
    for (quint16 row = 0; row < snapshot.rows && matchCount < maximumKeywordMatchesPerViewport; ++row)
    {
        QString text;
        std::vector<quint16> characterStarts;
        std::vector<quint16> characterEnds;
        text.reserve(snapshot.columns);
        characterStarts.reserve(snapshot.columns);
        characterEnds.reserve(snapshot.columns);
        for (quint16 column = 0; column < snapshot.columns; ++column)
        {
            const terminal::TerminalCell &cell = snapshot.cell(column, row);
            if (cell.grapheme.empty())
            {
                if (cell.displayWidth != 0)
                {
                    text.append(QLatin1Char(' '));
                    characterStarts.push_back(column);
                    characterEnds.push_back(static_cast<quint16>(column + 1));
                }
                continue;
            }
            const QString grapheme =
                QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
            text.append(grapheme);
            const auto width = static_cast<quint16>(std::clamp<int>(cell.displayWidth, 1, snapshot.columns - column));
            for (qsizetype index = 0; index < grapheme.size(); ++index)
            {
                characterStarts.push_back(column);
                characterEnds.push_back(static_cast<quint16>(column + width));
            }
        }

        for (std::size_t ruleIndex = 0; ruleIndex < ruleCount && matchCount < maximumKeywordMatchesPerViewport;
             ++ruleIndex)
        {
            const TerminalKeywordRule &rule = rules[ruleIndex];
            if (!rule.enabled || rule.pattern.isEmpty() || rule.pattern.size() > maximumKeywordPatternLength
                || (!rule.foreground.isValid() && !rule.background.isValid()))
            {
                continue;
            }
            const Qt::CaseSensitivity sensitivity = rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
            qsizetype from = 0;
            while (from <= text.size() - rule.pattern.size() && matchCount < maximumKeywordMatchesPerViewport)
            {
                const qsizetype match = text.indexOf(rule.pattern, from, sensitivity);
                if (match < 0)
                {
                    break;
                }
                const qsizetype finalCharacter = match + rule.pattern.size() - 1;
                const auto matchIndex = static_cast<std::size_t>(match);
                const auto finalCharacterIndex = static_cast<std::size_t>(finalCharacter);
                if (matchIndex < characterStarts.size() && finalCharacterIndex < characterEnds.size())
                {
                    const quint16 firstColumn = characterStarts[matchIndex];
                    const quint16 finalColumn = characterEnds[finalCharacterIndex];
                    for (quint16 column = firstColumn; column < finalColumn && column < snapshot.columns; ++column)
                    {
                        TerminalKeywordCellStyle &style =
                            styles[(static_cast<std::size_t>(row) * snapshot.columns) + column];
                        const std::size_t styleIndex = (static_cast<std::size_t>(row) * snapshot.columns) + column;
                        if (claimed[styleIndex])
                        {
                            continue;
                        }
                        if (rule.foreground.isValid())
                        {
                            style.foreground = rule.foreground;
                        }
                        if (rule.background.isValid())
                        {
                            style.background = rule.background;
                        }
                        claimed[styleIndex] = true;
                    }
                    ++matchCount;
                }
                from = match + std::max<qsizetype>(1, rule.pattern.size());
            }
        }
    }
    return styles;
}

} // namespace ztermy::ui
