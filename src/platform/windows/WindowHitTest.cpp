#include "platform/windows/WindowHitTest.h"

#include <algorithm>
#include <cstdint>

namespace ztermy::windowing
{

Rect constrainMaximizedClientRect(const Rect proposedClientRect, const Rect workArea) noexcept
{
    const int left = std::max(proposedClientRect.x, workArea.x);
    const int top = std::max(proposedClientRect.y, workArea.y);
    const int right = std::min(proposedClientRect.x + proposedClientRect.width, workArea.x + workArea.width);
    const int bottom = std::min(proposedClientRect.y + proposedClientRect.height, workArea.y + workArea.height);

    if (right <= left || bottom <= top)
    {
        return proposedClientRect;
    }

    return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

Size scaleLogicalSizeForDpi(const Size logicalSize, const unsigned int dpi) noexcept
{
    constexpr std::int64_t defaultDpi = 96;
    const auto scale = [dpi](const int value) {
        return static_cast<int>(((static_cast<std::int64_t>(value) * dpi) + (defaultDpi / 2)) / defaultDpi);
    };
    return {.width = scale(logicalSize.width), .height = scale(logicalSize.height)};
}

HitArea classifyHitTest(const Point point, const Size windowSize, const HitTestMetrics &metrics,
                        const bool maximized) noexcept
{
    if (!maximized)
    {
        const bool onLeft = point.x >= 0 && point.x < metrics.resizeBorder;
        const bool onRight = point.x < windowSize.width && point.x >= windowSize.width - metrics.resizeBorder;
        const bool onTop = point.y >= 0 && point.y < metrics.resizeBorder;
        const bool onBottom = point.y < windowSize.height && point.y >= windowSize.height - metrics.resizeBorder;

        if (onTop && onLeft)
        {
            return HitArea::TopLeft;
        }
        if (onTop && onRight)
        {
            return HitArea::TopRight;
        }
        if (onBottom && onLeft)
        {
            return HitArea::BottomLeft;
        }
        if (onBottom && onRight)
        {
            return HitArea::BottomRight;
        }
        if (onLeft)
        {
            return HitArea::Left;
        }
        if (onTop)
        {
            return HitArea::Top;
        }
        if (onRight)
        {
            return HitArea::Right;
        }
        if (onBottom)
        {
            return HitArea::Bottom;
        }
    }

    if (metrics.maximizeButton.contains(point))
    {
        return HitArea::MaximizeButton;
    }
    if (metrics.caption.contains(point))
    {
        return HitArea::Caption;
    }
    return HitArea::Client;
}

} // namespace ztermy::windowing
