#include "platform/windows/WindowsGlobalShortcut.h"

#include <QSignalSpy>
#include <QTest>
#include <QWindow>
#include <qt_windows.h>

class WindowsGlobalShortcutTests final : public QObject
{
    Q_OBJECT
private slots:
    void ordinaryTypingAndChordSequencesAreRejected();
    void conflictingReplacementKeepsPreviousBinding();
    void dispatcherHotkeyWithoutResultStorageIsHandled();
};

void WindowsGlobalShortcutTests::ordinaryTypingAndChordSequencesAreRejected()
{
    using ztermy::windowing::WindowsGlobalShortcut;
    for (const auto *text : {"A", "Shift+A", "Ctrl+K, Ctrl+C", "Ctrl+NotAKey"})
        QVERIFY2(!WindowsGlobalShortcut::parse(QString::fromLatin1(text)), text);
    const auto valid = WindowsGlobalShortcut::parse(QStringLiteral("Ctrl+Alt+Space"));
    QVERIFY(valid);
    QCOMPARE(valid->virtualKey, static_cast<unsigned int>(VK_SPACE));
    QCOMPARE(valid->modifiers, static_cast<unsigned int>(MOD_CONTROL | MOD_ALT));
    QVERIFY(WindowsGlobalShortcut::parse({}));
}

void WindowsGlobalShortcutTests::conflictingReplacementKeepsPreviousBinding()
{
    QWindow first;
    QWindow second;
    ztermy::windowing::WindowsGlobalShortcut owner(&first);
    ztermy::windowing::WindowsGlobalShortcut other(&second);
    // Real Windows registrations on distinct HWNDs, not a mock returning success.
    QVERIFY(owner.configure(QStringLiteral("Ctrl+Alt+Shift+F23")).isEmpty());
    QVERIFY(other.configure(QStringLiteral("Ctrl+Alt+Shift+F24")).isEmpty());
    QVERIFY(!owner.configure(QStringLiteral("Ctrl+Alt+Shift+F24")).isEmpty());
    QCOMPARE(owner.shortcut(), QStringLiteral("Ctrl+Alt+Shift+F23"));
    QVERIFY(!other.configure(QStringLiteral("Ctrl+Alt+Shift+F23")).isEmpty());
    QVERIFY(owner.configure({}).isEmpty());
    QVERIFY(other.configure(QStringLiteral("Ctrl+Alt+Shift+F23")).isEmpty());
}

void WindowsGlobalShortcutTests::dispatcherHotkeyWithoutResultStorageIsHandled()
{
    QWindow window;
    ztermy::windowing::WindowsGlobalShortcut shortcut(&window);
    QVERIFY(shortcut.configure(QStringLiteral("Ctrl+Alt+Shift+F23")).isEmpty());
    QSignalSpy activated(&shortcut, &ztermy::windowing::WindowsGlobalShortcut::activated);
    // Queue-level messages in QEventDispatcherWin32 have no LRESULT storage.
    MSG message{};
    message.hwnd = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    message.message = WM_HOTKEY;
    message.wParam = 0x5A70;
    QVERIFY(shortcut.nativeEventFilter("windows_dispatcher_MSG", &message, nullptr));
    QCOMPARE(activated.count(), 1);
    message.wParam = 0x5A71;
    QVERIFY(!shortcut.nativeEventFilter("windows_dispatcher_MSG", &message, nullptr));
    QCOMPARE(activated.count(), 1);
}

QTEST_MAIN(WindowsGlobalShortcutTests)
#include "windows_global_shortcut_tests.moc"
