#pragma once

#include "domain/terminal/TerminalEngine.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace ztermy::ui
{

enum class TerminalQuickSelectKind : std::uint8_t
{
    hyperlink,
    path,
    address,
    gitHash,
};

struct TerminalQuickSelectTarget final
{
    quint16 row = 0;
    quint16 startColumn = 0;
    quint16 endColumn = 0;
    QString label;
    QString value;
    QString uri;
    TerminalQuickSelectKind kind = TerminalQuickSelectKind::hyperlink;
};

inline constexpr std::size_t maximumQuickSelectTargets = 256;

[[nodiscard]] std::vector<TerminalQuickSelectTarget> quickSelectTargets(const terminal::TerminalSnapshot &snapshot);

} // namespace ztermy::ui
