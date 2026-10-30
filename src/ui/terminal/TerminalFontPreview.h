#pragma once

#include "ui/terminal/TerminalItem.h"

namespace ztermy::ui
{
// A bounded, immutable sample painted by the real terminal renderer. No PTY,
// SSH session, worker, shell startup or per-cell QML objects are involved.
class TerminalFontPreview : public TerminalItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TerminalFontPreview)

public:
    explicit TerminalFontPreview(QQuickItem *parent = nullptr);
};

} // namespace ztermy::ui
