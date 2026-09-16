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

QTEST_MAIN(WindowStateTests)

#include "window_state_tests.moc"
