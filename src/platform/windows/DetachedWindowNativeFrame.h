#pragma once

#include "platform/windows/WindowHitTest.h"

#include <QQuickWindow>
#include <qt_windows.h>

namespace ztermy::windowing
{
[[nodiscard]] LRESULT toNativeHitArea(HitArea area) noexcept;
[[nodiscard]] int resizeBorderForWindow(HWND windowHandle) noexcept;
[[nodiscard]] bool handleDetachedWindowFrameMessage(QQuickWindow &window, const MSG &message, qintptr *result);
[[nodiscard]] bool isUnobscuredDropTarget(QWindow &window, QPointF localPosition, QWindow *movingWindow);
} // namespace ztermy::windowing
