#pragma once

#include <QFont>
#include "ui/terminal/TerminalFontStyles.h"
#include <array>
#include <bitset>
#include <cstdint>

namespace ztermy::ui
{
// Value-owned typography and raster invalidation state; no independent lifetime.
struct TerminalRasterState
{
    std::uint64_t revision = 0;
    qreal backgroundOpacity = 1.0;
    QFont font;
    qreal lineHeightScale = 1.0;
    qreal cellWidthScale = 1.0;
    qreal cellWidth = 1.0;
    qreal cellHeight = 1.0;
    qreal fontAscent = 0.0;
    std::array<QFont, styledFontCount> styledFonts;
    std::bitset<styledFontCount> styledFontsReady;
    std::uint64_t rowRasterRevision = 0;
    bool rowRasterCacheEnabled = true;
    bool fullInvalidationPending = true;
};
} // namespace ztermy::ui
