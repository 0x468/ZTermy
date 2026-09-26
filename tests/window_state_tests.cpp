#include "core/windowing/WindowPresenter.h"
#include "core/windowing/WindowStateTransitions.h"

#include <QTest>
#include <QWindow>

class WindowStateTests final : public QObject
{
    Q_OBJECT

private slots:
    void minimizedStatesKeepEveryOtherFlag();
    void presentedStatesOnlyClearMinimized();
    void maximizeToggleClearsMinimizedAndFlipsMaximized();
    void minimizeThenPresentRestoresMaximizedWindow();
    void presentShowsHiddenWindowWithItsStates();
    void revealShowsHiddenWindowWithoutChangingItsState();
    void toggleMaximizeRoundTripsVisibleWindow();
    void restoreGeometryStaysOnAvailableScreen();
    void restorePlacementDoesNotRevealOrKeepMinimization();
};

void WindowStateTests::minimizedStatesKeepEveryOtherFlag()
{
    using namespace ztermy::windowing;
    QCOMPARE(minimizedStates(Qt::WindowNoState), Qt::WindowStates{Qt::WindowMinimized});
    QCOMPARE(minimizedStates(Qt::WindowMaximized), Qt::WindowMaximized | Qt::WindowMinimized);
    QCOMPARE(minimizedStates(Qt::WindowFullScreen), Qt::WindowFullScreen | Qt::WindowMinimized);
    QCOMPARE(minimizedStates(Qt::WindowMaximized | Qt::WindowMinimized), Qt::WindowMaximized | Qt::WindowMinimized);
}

void WindowStateTests::presentedStatesOnlyClearMinimized()
{
    using namespace ztermy::windowing;
    QCOMPARE(presentedStates(Qt::WindowMinimized), Qt::WindowStates{Qt::WindowNoState});
    QCOMPARE(presentedStates(Qt::WindowMaximized | Qt::WindowMinimized), Qt::WindowStates{Qt::WindowMaximized});
    QCOMPARE(presentedStates(Qt::WindowFullScreen | Qt::WindowMinimized), Qt::WindowStates{Qt::WindowFullScreen});
    QCOMPARE(presentedStates(Qt::WindowMaximized), Qt::WindowStates{Qt::WindowMaximized});
}

void WindowStateTests::maximizeToggleClearsMinimizedAndFlipsMaximized()
{
    using namespace ztermy::windowing;
    QCOMPARE(maximizeToggledStates(Qt::WindowNoState), Qt::WindowStates{Qt::WindowMaximized});
    QCOMPARE(maximizeToggledStates(Qt::WindowMaximized), Qt::WindowStates{Qt::WindowNoState});
    QCOMPARE(maximizeToggledStates(Qt::WindowMaximized | Qt::WindowMinimized), Qt::WindowStates{Qt::WindowNoState});
    QCOMPARE(maximizeToggledStates(Qt::WindowMinimized), Qt::WindowStates{Qt::WindowMaximized});
    QCOMPARE(maximizeToggledStates(Qt::WindowFullScreen), Qt::WindowFullScreen | Qt::WindowMaximized);
}

void WindowStateTests::minimizeThenPresentRestoresMaximizedWindow()
{
    QWindow window;
    window.setWindowStates(Qt::WindowMaximized);
    window.setVisible(true);

    ztermy::windowing::minimize(window);
    QCOMPARE(window.windowStates(), Qt::WindowMaximized | Qt::WindowMinimized);
    QCOMPARE(window.windowState(), Qt::WindowMinimized);
    QVERIFY(window.isVisible());

    ztermy::windowing::present(window);
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
    QCOMPARE(window.windowState(), Qt::WindowMaximized);
    QVERIFY(window.isVisible());
}

void WindowStateTests::presentShowsHiddenWindowWithItsStates()
{
    // Close-to-tray hides the window; presenting it later must not reset the
    // maximized state the way QWindow::show() would.
    QWindow window;
    window.setWindowStates(Qt::WindowMaximized);
    window.setVisible(true);
    window.hide();
    QVERIFY(!window.isVisible());

    ztermy::windowing::present(window);
    QVERIFY(window.isVisible());
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
}

void WindowStateTests::revealShowsHiddenWindowWithoutChangingItsState()
{
    QWindow window;
    window.setWindowStates(Qt::WindowMaximized);
    window.setVisible(true);
    window.hide();

    ztermy::windowing::reveal(window);
    QVERIFY(window.isVisible());
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
}

void WindowStateTests::toggleMaximizeRoundTripsVisibleWindow()
{
    QWindow window;
    window.setVisible(true);

    ztermy::windowing::toggleMaximize(window);
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});

    ztermy::windowing::toggleMaximize(window);
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowNoState});
    QVERIFY(window.isVisible());
}

void WindowStateTests::restoreGeometryStaysOnAvailableScreen()
{
    using ztermy::windowing::boundedRestoreGeometry;
    const QRect leftScreen{-1920, -200, 1920, 1040};
    const QRect unchanged{-1800, -100, 900, 600};
    QCOMPARE(boundedRestoreGeometry(unchanged, leftScreen, {480, 320}), unchanged);
    // A removed left monitor must not leave an inaccessible title bar.
    QCOMPARE(boundedRestoreGeometry(unchanged, {0, 40, 1366, 728}, {480, 320}), QRect(0, 40, 900, 600));
    // A smaller work area (including a top taskbar) bounds both size and origin.
    QCOMPARE(boundedRestoreGeometry({3000, 2000, 2560, 1440}, {0, 40, 1366, 728}, {480, 320}), QRect(0, 40, 1366, 728));
    QCOMPARE(boundedRestoreGeometry({1200, 700, 1, 1}, {0, 0, 1366, 768}, {480, 320}), QRect(886, 448, 480, 320));
    // A screen smaller than the application's preferred minimum remains usable.
    QCOMPARE(boundedRestoreGeometry({-99, -99, 900, 600}, {10, 20, 320, 200}, {480, 320}), QRect(10, 20, 320, 200));
}

void WindowStateTests::restorePlacementDoesNotRevealOrKeepMinimization()
{
    QWindow window;
    window.setWindowStates(Qt::WindowMinimized | Qt::WindowMaximized);
    const QRect normal{70, 80, 640, 480};
    ztermy::windowing::restorePlacement(window, normal, true);
    QVERIFY(!window.isVisible());
    QCOMPARE(window.geometry(), normal);
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
    ztermy::windowing::restorePlacement(window, normal, false);
    QVERIFY(!window.isVisible());
    QCOMPARE(window.geometry(), normal);
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowNoState});
}

QTEST_MAIN(WindowStateTests)

#include "window_state_tests.moc"
