#pragma once

#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/terminal/TerminalItem.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScopeGuard>

#include <cmath>

namespace ztermy::ui
{
inline bool verifyPasteOptions(NativeWindow &window, AppController &controller, const QString &directory)
{
    using namespace std::chrono_literals;
    if (!QCoreApplication::arguments().contains(QStringLiteral("--data-dir"))
        || qEnvironmentVariableIntValue("ZTERMY_TEST_ISOLATED_SHELLS") != 1)
        return false;
    auto *clipboard = QGuiApplication::clipboard();
    auto original = cloneMimeData(clipboard->mimeData());
    const auto restoreClipboard = qScopeGuard([&] {
        clipboard->setMimeData(original.release());
    });
    for (const auto &tab : controller.terminalTabs())
        controller.closeTerminalTab(tab.toMap().value(QStringLiteral("id")).toString());
    if (!controller.resetApplicationSettings()
        || controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt")).isEmpty())
        return false;
    auto *root = window.rootObject();
    root->setProperty("currentPage", QStringLiteral("terminal"));
    showForRuntimeSmoke(window);
    window.requestActivate();
    processWindowEventsFor(250ms);
    auto *viewport = qobject_cast<TerminalItem *>(terminalViewportItem(root));
    auto *dialog = root->findChild<QObject *>(QStringLiteral("multilinePasteDialog"));
    if (!viewport || !dialog)
        return false;
    const auto readSettings = [&] {
        QFile file(QDir(directory).filePath(QStringLiteral("settings.json")));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto key = [](QQuickWindow &target, Qt::Key value, Qt::KeyboardModifiers modifiers = {}) {
        qt_handleKeyEvent(&target, QEvent::KeyPress, value, modifiers);
        QCoreApplication::processEvents();
        qt_handleKeyEvent(&target, QEvent::KeyRelease, value, modifiers);
        processWindowEventsFor(60ms);
    };
    const auto focusRing = [](QQuickItem *button) {
        auto *ring = quickItem(button, "actionButtonFocusRing");
        auto *background = button ? button->property("background").value<QQuickItem *>() : nullptr;
        auto *pen = ring ? ring->property("border").value<QObject *>() : nullptr;
        if (!ring || !background || !pen || !ring->isVisible() || !button->hasActiveFocus()
            || !button->property("visualFocus").toBool())
            return false;
        const auto luminance = [](const QColor &color) {
            const auto linear = [](double value) {
                return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
        };
        const double first = luminance(pen->property("color").value<QColor>());
        const double second = luminance(background->property("color").value<QColor>());
        return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05) >= 3.0;
    };
    const auto open = [&](TerminalItem *item, QObject *popup) {
        if (!processWindowEventsUntil(
                [&] {
                    return !item->readOnly();
                },
                3000ms))
            return false;
        clipboard->setText(QStringLiteral("rem paste check\nrem paste check"));
        item->pasteClipboard();
        qInfo() << "Paste fixture pending immediately, expected clipboard, prompt target"
                << item->multilinePastePending()
                << (clipboard->text() == QStringLiteral("rem paste check\nrem paste check"))
                << (popup->property("pendingViewport").value<QObject *>() == item);
        const bool opened = processWindowEventsUntil(
            [&] {
                return popup->property("opened").toBool();
            },
            2000ms);
        if (!opened || !item->multilinePastePending())
            qWarning() << "Paste fixture open failed: opened, visible, pending, readOnly, confirmation"
                       << popup->property("opened") << popup->property("visible") << item->multilinePastePending()
                       << item->property("readOnly") << item->property("confirmMultilinePaste");
        return opened && item->multilinePastePending();
    };
    const auto close = [&](QObject *popup) {
        return processWindowEventsUntil(
            [&] {
                return !popup->property("visible").toBool();
            },
            2000ms);
    };
    int pastes = 0;
    bool exactBytes = true;
    const auto pasted =
        QObject::connect(viewport, &TerminalItem::pasteRequested, viewport, [&](const QByteArray &bytes) {
            ++pastes;
            exactBytes = exactBytes && bytes == QByteArrayLiteral("rem paste check\nrem paste check");
        });
    const auto disconnectPaste = qScopeGuard([&] {
        QObject::disconnect(pasted);
    });
    // Default and keyboard-selected primary focus remain visible in both themes,
    // including extreme custom accent colors. No system appearance is changed.
    for (const auto &theme : {QStringLiteral("dark"), QStringLiteral("light")})
    {
        if (!controller.saveApplicationSettings(
                theme, 1.0, QStringLiteral("solid"), QStringLiteral("custom"),
                theme == QStringLiteral("dark") ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000"), {},
                controller.terminalFontFamily(), 12.0, false, true, 0.0, QStringLiteral("terminal"), true, false, false,
                true, QStringLiteral("system"), false, true))
            return false;
        const auto before = readSettings();
        if (!open(viewport, dialog))
            return false;
        auto *cancel = quickItem(root, "multilinePasteReject");
        auto *accept = quickItem(root, "multilinePasteAccept");
        if (!focusRing(cancel))
        {
            qWarning() << "Paste default cancellation focus is not visible";
            return false;
        }
        key(window, Qt::Key_Right);
        if (!focusRing(accept))
            return false;
        auto *scope = quickItem(root, "multilinePasteScope");
        if (!scope)
            return false;
        scope->forceActiveFocus(Qt::TabFocusReason);
        key(window, Qt::Key_Down);
        if (dialog->property("promptChoice").toInt() != 1)
            return false;
        key(window, Qt::Key_Escape);
        // During the shared exit transition the modal keeps shielding input;
        // dismissal must finish before the viewport can regain focus.
        const bool closing = dialog->property("visible").toBool();
        if (closing && (dialog->property("opened").toBool() || viewport->hasActiveFocus()))
            return false;
        if (!close(dialog) || viewport->multilinePastePending() || pastes != 0 || readSettings() != before
            || !controller.multilinePasteConfirmationRequired() || !viewport->hasActiveFocus())
            return false;
        qInfo() << "Paste cancellation, exit shielding and contrasting keyboard focus passed" << theme << closing;
    }
    const auto before = readSettings();
    if (!open(viewport, dialog))
        return false;
    key(window, Qt::Key_Right);
    key(window, Qt::Key_Return);
    if (!close(dialog) || pastes != 1 || !exactBytes || readSettings() != before
        || !controller.multilinePasteConfirmationRequired())
        return false;
    if (!open(viewport, dialog))
        return false;
    dialog->setProperty("promptChoice", 2);
    key(window, Qt::Key_Escape);
    if (!close(dialog) || readSettings() != before || !controller.multilinePasteConfirmationRequired())
        return false;
    if (!open(viewport, dialog))
        return false;
    dialog->setProperty("promptChoice", 2);
    key(window, Qt::Key_Right);
    key(window, Qt::Key_Return);
    if (!close(dialog) || pastes != 2 || !exactBytes || controller.confirmMultilinePaste() || readSettings() == before)
        return false;
    if (!controller.resetApplicationSettings())
        return false;
    // Exercise the same prompt in an owned detached window, then verify the
    // run-only policy propagates to both windows without writing settings.
    const auto detachedId = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    const auto paneId = controller.terminalWorkspace(detachedId).value(QStringLiteral("activePaneId")).toString();
    if (paneId.isEmpty() || !QMetaObject::invokeMethod(root, "detachTerminalPane", Q_ARG(QVariant, paneId)))
        return false;
    processWindowEventsFor(300ms);
    QQuickWindow *detached = nullptr;
    for (auto *candidate : QGuiApplication::allWindows())
        if (candidate->objectName() == QStringLiteral("detachedTerminalWindow"))
            detached = qobject_cast<QQuickWindow *>(candidate);
    if (!detached)
        return false;
    auto *detachedViewport = qobject_cast<TerminalItem *>(terminalViewportItem(detached->contentItem()));
    auto *detachedDialog = detached->findChild<QObject *>(QStringLiteral("multilinePasteDialog"));
    const auto runBefore = readSettings();
    if (!detachedViewport || !detachedDialog || !open(detachedViewport, detachedDialog)
        || !focusRing(quickItem(detached->contentItem(), "multilinePasteReject")))
        return false;
    int detachedPastes = 0;
    const auto detachedPasted = QObject::connect(
        detachedViewport, &TerminalItem::pasteRequested, detachedViewport, [&](const QByteArray &bytes) {
            ++detachedPastes;
            exactBytes = exactBytes && bytes == QByteArrayLiteral("rem paste check\nrem paste check");
        });
    const auto disconnectDetachedPaste = qScopeGuard([&] {
        QObject::disconnect(detachedPasted);
    });
    detachedDialog->setProperty("promptChoice", 1);
    key(*detached, Qt::Key_Right);
    key(*detached, Qt::Key_Return);
    if (!close(detachedDialog) || detachedPastes != 1 || !exactBytes || detachedViewport->multilinePastePending()
        || !controller.confirmMultilinePaste() || controller.multilinePasteConfirmationRequired()
        || viewport->property("confirmMultilinePaste").toBool()
        || detachedViewport->property("confirmMultilinePaste").toBool() || readSettings() != runBefore)
        return false;
    viewport->pasteClipboard();
    if (viewport->multilinePastePending() || pastes != 3 || !exactBytes)
        return false;
    qInfo() << "Paste scopes passed: once, cancellation, persistent and run-only; main and detached windows";
    return true;
}
} // namespace ztermy::ui
