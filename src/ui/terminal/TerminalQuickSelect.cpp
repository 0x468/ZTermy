#include "ui/terminal/TerminalQuickSelect.h"

#include <QRegularExpression>

#include <algorithm>

namespace ztermy::ui
{
namespace
{

struct RowText final
{
    QString text;
    std::vector<quint16> starts;
    std::vector<quint16> ends;
};

[[nodiscard]] RowText rowText(const terminal::TerminalSnapshot &snapshot, const quint16 row)
{
    RowText result;
    result.text.reserve(snapshot.columns);
    result.starts.reserve(snapshot.columns);
    result.ends.reserve(snapshot.columns);
    for (quint16 column = 0; column < snapshot.columns; ++column)
    {
        const terminal::TerminalCell &cell = snapshot.cell(column, row);
        if (cell.displayWidth == 0)
        {
            continue;
        }
        const QString grapheme =
            cell.grapheme.empty()
                ? QStringLiteral(" ")
                : QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size()));
        result.text.append(grapheme);
        const quint16 end =
            static_cast<quint16>(std::min<int>(snapshot.columns, column + std::max<int>(1, cell.displayWidth)));
        for (qsizetype index = 0; index < grapheme.size(); ++index)
        {
            result.starts.push_back(column);
            result.ends.push_back(end);
        }
    }
    return result;
}

[[nodiscard]] QString targetText(const terminal::TerminalSnapshot &snapshot, const quint16 row, const quint16 start,
                                 const quint16 end)
{
    QString result;
    for (quint16 column = start; column < end && column < snapshot.columns; ++column)
    {
        const terminal::TerminalCell &cell = snapshot.cell(column, row);
        if (cell.displayWidth != 0 && !cell.grapheme.empty())
        {
            result.append(QString::fromUcs4(cell.grapheme.data(), static_cast<qsizetype>(cell.grapheme.size())));
        }
    }
    return result;
}

void appendPatternMatches(const quint16 row, const RowText &text, const QRegularExpression &pattern,
                          const TerminalQuickSelectKind kind, std::vector<bool> &claimed,
                          std::vector<TerminalQuickSelectTarget> &targets)
{
    QRegularExpressionMatchIterator matches = pattern.globalMatch(text.text);
    while (matches.hasNext() && targets.size() < maximumQuickSelectTargets)
    {
        const QRegularExpressionMatch match = matches.next();
        if (!match.hasMatch() || match.capturedLength() <= 0)
        {
            continue;
        }
        const qsizetype firstCharacter = match.capturedStart();
        const qsizetype finalCharacter = firstCharacter + match.capturedLength() - 1;
        if (firstCharacter < 0 || finalCharacter < 0 || static_cast<std::size_t>(finalCharacter) >= text.ends.size())
        {
            continue;
        }
        const quint16 start = text.starts[static_cast<std::size_t>(firstCharacter)];
        const quint16 end = text.ends[static_cast<std::size_t>(finalCharacter)];
        bool overlaps = false;
        for (quint16 column = start; column < end; ++column)
        {
            if (claimed[column])
            {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
        {
            continue;
        }
        for (quint16 column = start; column < end; ++column)
        {
            claimed[column] = true;
        }
        targets.push_back(TerminalQuickSelectTarget{.row = row,
                                                    .startColumn = start,
                                                    .endColumn = end,
                                                    .value = match.captured(),
                                                    .kind = kind});
    }
}

[[nodiscard]] QString labelFor(const std::size_t index, const bool twoCharacters)
{
    static const QString alphabet = QStringLiteral("asdfghjklqwertyuiopzxcvbnm");
    const auto base = static_cast<std::size_t>(alphabet.size());
    if (!twoCharacters)
    {
        return alphabet.mid(static_cast<qsizetype>(index), 1);
    }
    return alphabet.mid(static_cast<qsizetype>(index / base), 1)
           + alphabet.mid(static_cast<qsizetype>(index % base), 1);
}

} // namespace

std::vector<TerminalQuickSelectTarget> quickSelectTargets(const terminal::TerminalSnapshot &snapshot)
{
    std::vector<TerminalQuickSelectTarget> targets;
    if (snapshot.columns == 0 || snapshot.rows == 0)
    {
        return targets;
    }
    targets.reserve(std::min<std::size_t>(maximumQuickSelectTargets, snapshot.hyperlinks.size() + 16));

    static const QRegularExpression pathPattern(
        QStringLiteral(R"((?:[A-Za-z]:[\\/]|(?:~|\.{1,2})?/)[^\s<>"'`]+(?::\d+(?::\d+)?)?)"));
    static const QRegularExpression addressPattern(
        QStringLiteral(R"((?<![\w.])(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)(?![\w.]))"));
    static const QRegularExpression gitHashPattern(QStringLiteral(R"(\b[0-9A-Fa-f]{7,40}\b)"));

    for (quint16 row = 0; row < snapshot.rows && targets.size() < maximumQuickSelectTargets; ++row)
    {
        std::vector<bool> claimed(snapshot.columns, false);
        for (quint16 column = 0; column < snapshot.columns && targets.size() < maximumQuickSelectTargets;)
        {
            const std::uint32_t id = snapshot.cell(column, row).hyperlinkId;
            if (id == 0)
            {
                ++column;
                continue;
            }
            const quint16 start = column;
            while (column < snapshot.columns && snapshot.cell(column, row).hyperlinkId == id)
            {
                claimed[column] = true;
                ++column;
            }
            if (const terminal::TerminalHyperlink *link = snapshot.hyperlink(id); link != nullptr)
            {
                targets.push_back(TerminalQuickSelectTarget{
                    .row = row,
                    .startColumn = start,
                    .endColumn = column,
                    .value = targetText(snapshot, row, start, column),
                    .uri = QString::fromUtf8(link->uri),
                    .kind = TerminalQuickSelectKind::hyperlink,
                });
            }
        }

        const RowText text = rowText(snapshot, row);
        appendPatternMatches(row, text, pathPattern, TerminalQuickSelectKind::path, claimed, targets);
        appendPatternMatches(row, text, addressPattern, TerminalQuickSelectKind::address, claimed, targets);
        appendPatternMatches(row, text, gitHashPattern, TerminalQuickSelectKind::gitHash, claimed, targets);
    }

    const bool twoCharacters = targets.size() > 26;
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        targets[index].label = labelFor(index, twoCharacters);
    }
    return targets;
}

} // namespace ztermy::ui
