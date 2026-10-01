#pragma once

#include "application/AppController.h"
#include "application/ApplicationInstance.h"
#include "core/windowing/WindowPresenter.h"
#include "platform/windows/NativeWindow.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/WindowStateRuntimeSmoke.h"

#include <QProcess>
#include <array>

namespace ztermy::ui
{
[[nodiscard]] inline bool verifyGlobalShortcutSettingsRuntime(NativeWindow &main, AppController &controller,
                                                              QWindow &peer, const QDir &captures)
{
    using namespace std::chrono_literals;
    auto *root = main.rootObject();
    if (!QMetaObject::invokeMethod(root, "openSettingsTab"))
        return false;
    processWindowEventsFor(250ms);
    auto *category = quickItem(root, "settingsShortcutsCategory");
    if (!focusItem(main, category, QStringLiteral("settingsShortcutsCategory")))
        return false;
    sendKey(main, Qt::Key_Space);
    const auto record = [&](const Qt::Key key, const Qt::KeyboardModifiers modifiers) {
        auto *recorder = quickItem(root, "shortcutRecorder_system.summon");
        if (!focusItem(main, recorder, QStringLiteral("shortcutRecorder_system.summon")))
            return false;
        sendKey(main, Qt::Key_Space);
        sendKey(main, key, modifiers);
        return true;
    };
    const auto shortcut = [&] {
        return controller.windowInteractionSettings().value(QStringLiteral("globalShortcut")).toString();
    };
    auto *settings = quickItem(root, "settingsPane");
    auto *card = quickItem(root, "systemShortcutSettingsCard");
    if (!settings || !card || !card->isVisible()
        || settings->property("currentCategory").toString() != QStringLiteral("shortcuts")
        || quickItem(root, "settingsGlobalShortcutField"))
        return false;
    if (!record(Qt::Key_F22, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)
        || shortcut() != QStringLiteral("Ctrl+Alt+Shift+F22") || settings->property("shortcutRecording").toBool())
        return false;
    if (!record(Qt::Key_A, {}) || !settings->property("shortcutRecording").toBool()
        || shortcut() != QStringLiteral("Ctrl+Alt+Shift+F22") || main.globalShortcutError().isEmpty())
        return false;
    sendKey(main, Qt::Key_Escape);
    const auto peerHandle = reinterpret_cast<HWND>(peer.winId()); // NOLINT(performance-no-int-to-ptr)
    constexpr int reservedId = 0x6001;
    if (!RegisterHotKey(peerHandle, reservedId, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, VK_F21))
        return false;
    const auto unregister = qScopeGuard([&] {
        UnregisterHotKey(peerHandle, reservedId);
    });
    if (!record(Qt::Key_F21, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)
        || !settings->property("shortcutRecording").toBool() || main.globalShortcutError().isEmpty()
        || shortcut() != QStringLiteral("Ctrl+Alt+Shift+F22"))
        return false;
    sendKey(main, Qt::Key_Escape);
    if (!main.grabWindow().save(captures.filePath(QStringLiteral("shortcut-native-conflict.png")))
        || !record(Qt::Key_Backspace, {}) || !shortcut().isEmpty() || settings->property("shortcutRecording").toBool())
        return false;
    qInfo() << "System-wide shortcut panel: keyboard recording, ordinary-key rejection, real OS conflict, clear: true";
    return main.grabWindow().save(captures.filePath(QStringLiteral("shortcut-settings.png")));
}

[[nodiscard]] inline bool sendSummonRuntimeKey()
{
    constexpr std::array<WORD, 4> keys{VK_CONTROL, VK_MENU, VK_SHIFT, VK_F23};
    std::array<INPUT, 8> input{};
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        // Do not release modifiers being held by a person during acceptance.
        if ((GetAsyncKeyState(keys[index]) & 0x8000) != 0)
            return false;
        input[index].type = INPUT_KEYBOARD;
        input[index].ki.wVk = keys[index];
        input[index + keys.size()].type = INPUT_KEYBOARD;
        input[index + keys.size()].ki.wVk = keys[keys.size() - index - 1];
        input[index + keys.size()].ki.dwFlags = KEYEVENTF_KEYUP;
    }
    return SendInput(static_cast<UINT>(input.size()), input.data(), sizeof(INPUT)) == input.size();
}

[[nodiscard]] inline bool runSystemIntegrationRuntimeSmoke(NativeWindow &main, AppController &controller,
                                                           ApplicationInstance &instance, const QString &dataDir)
{
    using namespace std::chrono_literals;
    const auto handle = reinterpret_cast<HWND>(main.winId()); // NOLINT(performance-no-int-to-ptr)
    if (!main.configureGlobalShortcut(QStringLiteral("Ctrl+Alt+Shift+F23"), false))
        return false;
    const auto release = qScopeGuard([&main] {
        (void)main.configureGlobalShortcut({}, false);
    });
    QWindow peer;
    peer.setTitle(QStringLiteral("ztermy summon acceptance peer"));
    peer.setGeometry(50, 50, 500, 360);
    windowing::present(peer);
    if (!settleWindowUntil(
            [&peer] {
                return GetForegroundWindow()
                       == reinterpret_cast<HWND>(peer.winId()); // NOLINT(performance-no-int-to-ptr)
            },
            3s))
        return false;
    processWindowEventsFor(100ms);
    const QRect peerGeometry = peer.geometry();
    const auto peerStates = peer.windowStates();
    const auto unchangedPeer = [&] {
        return peer.isVisible() && peer.geometry() == peerGeometry && peer.windowStates() == peerStates;
    };
    const auto wake = [&] {
        return sendSummonRuntimeKey()
               && settleWindowUntil(
                   [&] {
                       return main.isVisible() && !main.windowStates().testFlag(Qt::WindowMinimized)
                              && GetForegroundWindow() == handle;
                   },
                   3s)
               && unchangedPeer();
    };
    const auto hide = [&] {
        return sendSummonRuntimeKey()
               && settleWindowUntil(
                   [&] {
                       return !main.isVisible();
                   },
                   3s)
               && unchangedPeer();
    };
    qInfo() << "System integration smoke: wake/hide";
    if (!wake() || !hide())
        return false;
    main.setCloseToTrayEnabled(false);
    main.prepareBackgroundLaunch();
    qInfo() << "System integration smoke: background-launch recovery";
    if (!main.trayIconVisible() || !wake() || main.trayIconVisible() || main.closeToTrayEnabled())
        return false;
    qInfo() << "System integration smoke: maximize recovery";
    windowing::toggleMaximize(main);
    if (!settleWindowUntil(
            [&] {
                return main.maximized();
            },
            3s)
        || !hide() || !wake() || !main.maximized())
        return false;
    qInfo() << "System integration smoke: minimize recovery";
    windowing::minimize(main);
    if (!settleWindowUntil(
            [&] {
                return main.windowStates().testFlag(Qt::WindowMinimized);
            },
            3s)
        || !wake() || !main.maximized())
        return false;
    QDir captures(QDir(dataDir).filePath(QStringLiteral("system-integration-captures")));
    if (!captures.mkpath(QStringLiteral("."))
        || !main.grabWindow().save(captures.filePath(QStringLiteral("summoned-maximized.png"))))
        return false;
    const auto directory = captures.filePath(QStringLiteral("中文 space & % !"));
    if (!QDir().mkpath(directory))
        return false;
    QObject::connect(&instance, &ApplicationInstance::directoryOpenRequested, &controller,
                     [&controller](const QString &path) {
                         (void)controller.openLocalDirectory(path);
                     });
    instance.setReady();
    const auto tabsBefore = controller.terminalTabs().size();
    qInfo() << "System integration smoke: directory IPC";
    QProcess launcher;
    launcher.start(QCoreApplication::applicationFilePath(),
                   {QStringLiteral("--data-dir"), dataDir, QStringLiteral("--open-directory"), directory});
    const auto stop = qScopeGuard([&launcher] {
        if (launcher.state() != QProcess::NotRunning)
        {
            launcher.kill();
            launcher.waitForFinished(1000);
        }
    });
    if (!settleWindowUntil(
            [&] {
                return launcher.state() == QProcess::NotRunning;
            },
            15s)
        || launcher.exitStatus() != QProcess::NormalExit || launcher.exitCode() != 0
        || controller.terminalTabs().size() != tabsBefore + 1)
        return false;
    qInfo() << "System integration smoke: directory presentation" << main.rootObject()->property("currentPage");
    if (!settleWindowUntil(
            [&] {
                return main.rootObject()->property("currentPage").toString() == QStringLiteral("terminal");
            },
            3s))
        return false;
    processWindowEventsFor(1500ms);
    const bool capture = main.grabWindow().save(captures.filePath(QStringLiteral("directory-forwarded.png")));
    qInfo()
        << "System integration: native hotkey hide/wake/maximize/minimize, peer unchanged, second-process directory:"
        << capture;
    return capture && verifyGlobalShortcutSettingsRuntime(main, controller, peer, captures);
}
} // namespace ztermy::ui
