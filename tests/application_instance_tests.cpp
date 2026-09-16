#include "application/ApplicationInstance.h"

#include <QMetaObject>
#include <QTest>
#include <QWindow>

class ApplicationInstanceTests final : public QObject
{
    Q_OBJECT

private slots:
    void activationRestoresMinimizedWindowWithoutLosingMaximizedState();
    void activationShowsHiddenWindowWithoutLosingMaximizedState();
};

void ApplicationInstanceTests::activationRestoresMinimizedWindowWithoutLosingMaximizedState()
{
    QWindow window;
    window.setWindowStates(Qt::WindowMaximized | Qt::WindowMinimized);
    window.setVisible(true);

    ztermy::ApplicationInstance instance;
    instance.setWindow(&window);
    QVERIFY(QMetaObject::invokeMethod(&instance, "activationRequested"));

    QCOMPARE(window.windowState(), Qt::WindowMaximized);
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
    QVERIFY(window.isVisible());
}

void ApplicationInstanceTests::activationShowsHiddenWindowWithoutLosingMaximizedState()
{
    // A second launch while the window sits in the tray must bring the window
    // back exactly as it was, not as a normal window.
    QWindow window;
    window.setWindowStates(Qt::WindowMaximized);
    window.setVisible(true);
    window.hide();

    ztermy::ApplicationInstance instance;
    instance.setWindow(&window);
    QVERIFY(QMetaObject::invokeMethod(&instance, "activationRequested"));

    QVERIFY(window.isVisible());
    QCOMPARE(window.windowStates(), Qt::WindowStates{Qt::WindowMaximized});
}

QTEST_MAIN(ApplicationInstanceTests)

#include "application_instance_tests.moc"
