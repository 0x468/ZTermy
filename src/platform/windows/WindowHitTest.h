#pragma once

namespace ztermy::windowing
{

struct Point
{
    int x;
    int y;
};

struct Size
{
    int width;
    int height;
};

struct Rect
{
    int x;
    int y;
    int width;
    int height;

    [[nodiscard]] constexpr bool contains(const Point point) const noexcept
    {
        return point.x >= x && point.x < x + width && point.y >= y && point.y < y + height;
    }
};

struct HitTestMetrics
{
    int resizeBorder;
    Rect caption;
    Rect maximizeButton;
};

[[nodiscard]] Rect constrainMaximizedClientRect(Rect proposedClientRect, Rect workArea) noexcept;
[[nodiscard]] Size scaleLogicalSizeForDpi(Size logicalSize, unsigned int dpi) noexcept;

enum class HitArea
{
    Client,
    Caption,
    MaximizeButton,
    Left,
    Top,
    Right,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

[[nodiscard]] HitArea classifyHitTest(Point point, Size windowSize, const HitTestMetrics &metrics,
                                      bool maximized) noexcept;

} // namespace ztermy::windowing
