#include "platform/windows/WindowHitTest.h"

#include <QTest>

class WindowHitTestTests final : public QObject
{
    Q_OBJECT

private slots:
    void classifiesResizeEdges();
    void classifiesCaptionControlsAndClient();
    void disablesResizeAreasWhenMaximized();
    void constrainsMaximizedClientToWorkArea();
};

void WindowHitTestTests::classifiesResizeEdges()
{
    using enum ztermy::windowing::HitArea;

    const ztermy::windowing::HitTestMetrics metrics{
        .resizeBorder = 8,
        .caption = {.x = 0, .y = 0, .width = 862, .height = 42},
        .maximizeButton = {.x = 908, .y = 0, .width = 46, .height = 42},
    };
    constexpr ztermy::windowing::Size size{.width = 1000, .height = 700};

    QCOMPARE(classifyHitTest({0, 0}, size, metrics, false), TopLeft);
    QCOMPARE(classifyHitTest({999, 0}, size, metrics, false), TopRight);
    QCOMPARE(classifyHitTest({0, 699}, size, metrics, false), BottomLeft);
    QCOMPARE(classifyHitTest({999, 699}, size, metrics, false), BottomRight);
    QCOMPARE(classifyHitTest({1, 200}, size, metrics, false), Left);
    QCOMPARE(classifyHitTest({500, 1}, size, metrics, false), Top);
    QCOMPARE(classifyHitTest({998, 200}, size, metrics, false), Right);
    QCOMPARE(classifyHitTest({500, 698}, size, metrics, false), Bottom);
}

void WindowHitTestTests::classifiesCaptionControlsAndClient()
{
    using enum ztermy::windowing::HitArea;

    const ztermy::windowing::HitTestMetrics metrics{
        .resizeBorder = 8,
        .caption = {.x = 0, .y = 0, .width = 862, .height = 42},
        .maximizeButton = {.x = 908, .y = 0, .width = 46, .height = 42},
    };
    constexpr ztermy::windowing::Size size{.width = 1000, .height = 700};

    QCOMPARE(classifyHitTest({400, 24}, size, metrics, false), Caption);
    QCOMPARE(classifyHitTest({930, 24}, size, metrics, false), MaximizeButton);
    QCOMPARE(classifyHitTest({885, 24}, size, metrics, false), Client);
    QCOMPARE(classifyHitTest({500, 300}, size, metrics, false), Client);
}

void WindowHitTestTests::disablesResizeAreasWhenMaximized()
{
    using enum ztermy::windowing::HitArea;

    const ztermy::windowing::HitTestMetrics metrics{
        .resizeBorder = 8,
        .caption = {.x = 0, .y = 0, .width = 862, .height = 42},
        .maximizeButton = {.x = 908, .y = 0, .width = 46, .height = 42},
    };
    constexpr ztermy::windowing::Size size{.width = 1000, .height = 700};

    QCOMPARE(classifyHitTest({1, 200}, size, metrics, true), Client);
    QCOMPARE(classifyHitTest({400, 1}, size, metrics, true), Caption);
    QCOMPARE(classifyHitTest({930, 1}, size, metrics, true), MaximizeButton);
}

void WindowHitTestTests::constrainsMaximizedClientToWorkArea()
{
    using ztermy::windowing::Rect;

    constexpr Rect oversized{.x = -8, .y = -8, .width = 1936, .height = 1056};
    constexpr Rect workArea{.x = 0, .y = 0, .width = 1920, .height = 1040};
    const Rect constrained = constrainMaximizedClientRect(oversized, workArea);

    QCOMPARE(constrained.x, 0);
    QCOMPARE(constrained.y, 0);
    QCOMPARE(constrained.width, 1920);
    QCOMPARE(constrained.height, 1040);

    constexpr Rect secondaryMonitorWorkArea{.x = -1920, .y = 40, .width = 1920, .height = 1040};
    constexpr Rect secondaryOversized{.x = -1928, .y = 32, .width = 1936, .height = 1056};
    const Rect secondaryConstrained = constrainMaximizedClientRect(secondaryOversized, secondaryMonitorWorkArea);

    QCOMPARE(secondaryConstrained.x, -1920);
    QCOMPARE(secondaryConstrained.y, 40);
    QCOMPARE(secondaryConstrained.width, 1920);
    QCOMPARE(secondaryConstrained.height, 1040);

    QCOMPARE(constrainMaximizedClientRect(workArea, workArea).x, workArea.x);
    QCOMPARE(constrainMaximizedClientRect(workArea, workArea).y, workArea.y);
    QCOMPARE(constrainMaximizedClientRect(workArea, workArea).width, workArea.width);
    QCOMPARE(constrainMaximizedClientRect(workArea, workArea).height, workArea.height);
}

QTEST_GUILESS_MAIN(WindowHitTestTests)

#include "window_hit_test_tests.moc"
