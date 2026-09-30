#pragma once

#include "application/logging/ConnectionHistoryController.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QJSValue>
#include <QQmlComponent>
#include <memory>
#include <optional>

namespace ztermy::ui
{
// Exercise actual editor controls with isolated, non-networked fixture data.
inline bool verifyHostEditorDismissal(NativeWindow &window, AppController &controller)
{
    window.resize(1120, 800);
    window.show();
    window.requestActivate();
    QQmlComponent component(window.engine());
    component.loadFromModule(QStringLiteral("Ztermy"), QStringLiteral("HostConnectionPane"));
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}}));
    auto *pane = qobject_cast<QQuickItem *>(object.get());
    if (!pane)
        return false;
    pane->setParentItem(window.contentItem());
    pane->setSize({1120, 760});
    pane->setZ(200);
    const auto pause = [] {
        processWindowEventsFor(std::chrono::milliseconds{300});
    };
    const auto click = [&](const char *name) {
        auto *item = visualQuickItem(window.contentItem(), name);
        if (!item)
            return false;
        sendMouseClick(window, *item, {item->width() / 2, item->height() / 2});
        pause();
        return true;
    };
    const auto expanded = [&] {
        return pane->property("editorExpanded").toBool();
    };
    auto *name = quickItem(pane, "hostName");
    auto *dialog = pane->findChild<QObject *>(QStringLiteral("hostDiscardChangesDialog"));
    if (!name || !dialog)
        return false;
    pause();
    bool passed = click("hostNew");
    passed = click("hostEditorClose") && !expanded() && !dialog->property("visible").toBool() && passed;
    qInfo() << "Pristine host editor close=" << passed;
    passed = click("hostNew") && passed;
    name->setProperty("text", QStringLiteral("Unsaved fixture"));
    sendMouseClick(window, *pane, {10, 400});
    pause();
    passed = expanded() && name->property("text").toString() == QStringLiteral("Unsaved fixture") && passed;
    passed = click("hostEditorClose") && dialog->property("visible").toBool() && expanded() && passed;
    const auto args = QCoreApplication::arguments();
    const auto option = args.indexOf(QStringLiteral("--data-dir"));
    if (option >= 0 && option + 1 < args.size())
        passed =
            window.grabWindow().save(QDir(args.at(option + 1)).filePath("host-unsaved-confirmation.png")) && passed;
    passed = click("hostKeepEditing") && expanded()
             && name->property("text").toString() == QStringLiteral("Unsaved fixture") && passed;
    qInfo() << "Host draft outside click and keep=" << passed;
    // Invalid save must neither close nor clear the draft.
    QMetaObject::invokeMethod(pane, "saveProfile");
    passed = expanded() && name->property("text").toString() == QStringLiteral("Unsaved fixture") && passed;
    name->forceActiveFocus();
    // Escape in a child picker closes only that picker, not the editor.
    passed = click("hostAuthentication") && passed;
    auto *authentication = quickItem(pane, "hostAuthentication");
    auto *picker = authentication ? authentication->property("popup").value<QObject *>() : nullptr;
    passed = picker && picker->property("visible").toBool() && passed;
    sendKey(window, Qt::Key_Escape);
    pause();
    passed = picker && !picker->property("visible").toBool() && !dialog->property("visible").toBool() && expanded()
             && passed;
    name->forceActiveFocus();
    sendKey(window, Qt::Key_Escape);
    pause();
    qInfo() << "Host Escape confirmation visible=" << dialog->property("visible").toBool()
            << "window active=" << window.isActive();
    passed = dialog->property("visible").toBool() && expanded() && passed;
    passed = click("hostDiscardChanges") && !expanded() && passed;
    qInfo() << "Host invalid save and Escape discard=" << passed;
    QMetaObject::invokeMethod(pane, "beginNewProfileForHost", Q_ARG(QVariant, QStringLiteral("first.invalid")),
                              Q_ARG(QVariant, 22));
    pause();
    name->setProperty("text", QStringLiteral("Keep this draft"));
    QMetaObject::invokeMethod(pane, "beginNewProfileForHost", Q_ARG(QVariant, QStringLiteral("second.invalid")),
                              Q_ARG(QVariant, 22));
    pause();
    passed = dialog->property("visible").toBool()
             && name->property("text").toString() == QStringLiteral("Keep this draft") && passed;
    passed = click("hostKeepEditing") && passed;
    QMetaObject::invokeMethod(pane, "beginNewProfileForHost", Q_ARG(QVariant, QStringLiteral("second.invalid")),
                              Q_ARG(QVariant, 22));
    pause();
    passed = click("hostDiscardChanges") && expanded()
             && name->property("text").toString() == QStringLiteral("second.invalid") && passed;
    qInfo() << "Host draft replacement=" << passed;
    // Successful save closes; re-opening a saved profile must start pristine.
    QMetaObject::invokeMethod(pane, "saveProfile");
    pause();
    passed = !expanded() && passed;
    const auto profiles = controller.property("hostProfiles").toList();
    if (profiles.isEmpty())
        return false;
    const QVariant profile = profiles.back();
    QMetaObject::invokeMethod(pane, "editProfile", Q_ARG(QVariant, profile));
    pause();
    passed = click("hostEditorClose") && !expanded() && !dialog->property("visible").toBool() && passed;
    QMetaObject::invokeMethod(pane, "editProfile", Q_ARG(QVariant, profile));
    pause();
    auto *port = quickItem(pane, "hostPort");
    if (!port)
        return false;
    port->setProperty("text", QStringLiteral("2200"));
    passed = click("hostEditorClose") && dialog->property("visible").toBool() && passed;
    passed = click("hostKeepEditing") && port->property("text").toString() == QStringLiteral("2200") && passed;
    qInfo() << "Saved host pristine and modified edit=" << passed;
    if (option >= 0 && option + 1 < args.size())
        passed = window.grabWindow().save(QDir(args.at(option + 1)).filePath("host-editor.png")) && passed;
    qInfo() << "Host editor outside click / pristine close / keep / discard / Escape / replacement / invalid save="
            << passed;
    return passed;
}

// Uses synthetic records in the caller's isolated data directory; no terminal is started.
inline std::optional<bool> runSideDrawerRuntimeCheck(NativeWindow &window, AppController &controller,
                                                     const QStringList &arguments)
{
    if (arguments.contains(QStringLiteral("--host-editor-dismiss-smoke")))
        return verifyHostEditorDismissal(window, controller);
    if (!arguments.contains(QStringLiteral("--side-drawer-smoke")))
        return std::nullopt;
    window.show();
    window.hide();
    window.show();
    auto *history = qobject_cast<logging::ConnectionHistoryController *>(controller.connectionHistory());
    if (!history)
        return false;
    bool passed = true;
    for (const int width : {1120, 600})
    {
        window.resize(width, 800);
        for (const auto *type : {"AppComboBox", "EditableSuggestionField"})
        {
            QQmlComponent comboComponent(window.engine());
            comboComponent.loadFromModule(QStringLiteral("Ztermy"), QString::fromLatin1(type));
            std::unique_ptr<QObject> combo(comboComponent.createWithInitialProperties(
                {{QStringLiteral("model"), QStringList{QStringLiteral("First"), QStringLiteral("Second")}}}));
            auto *item = qobject_cast<QQuickItem *>(combo.get());
            if (!item)
                return false;
            item->setParentItem(window.contentItem());
            item->setPosition({30, 90});
            item->setSize({220, 34});
            item->setZ(300);
            processWindowEventsFor(std::chrono::milliseconds{150});
            sendMouseClick(window, *item, {200, 17});
            processWindowEventsFor(std::chrono::milliseconds{250});
            auto *popup = item->property("popup").value<QObject *>();
            auto *content = popup ? popup->property("contentItem").value<QQuickItem *>() : nullptr;
            bool valid = popup && content && popup->property("opened").toBool()
                         && popup->property("height").toReal() > 25 && popup->property("height").toReal() <= 240
                         && qAbs(popup->property("width").toReal() - 220) < 0.5;
            if (valid)
            {
                sendMouseClick(window, *content, {20, content->height() * 0.75});
                processWindowEventsFor(std::chrono::milliseconds{250});
                const bool editable = item->property("editable").toBool();
                valid = !popup->property("visible").toBool()
                        && (editable ? item->property("text").toString() == QStringLiteral("Second")
                                           && item->property("currentIndex").toInt() == -1
                                     : item->property("currentIndex").toInt() == 1);
            }
            qInfo() << "Shared popup sizing/selection" << type << "width=" << width << "passed=" << valid;
            passed = valid && passed;
        }
        for (const auto *id : {"drawer-a", "drawer-b", "drawer-c"})
        {
            logging::ConnectionHistoryEntry entry;
            entry.id = id;
            entry.sessionId = id;
            entry.hostLabel = id;
            entry.hostname = "fixture.invalid";
            entry.username = "fixture";
            entry.localUsername = "fixture";
            entry.localHostname = "fixture-local";
            entry.protocol = "ssh";
            entry.status = "connecting";
            entry.phase = "connecting";
            entry.startedUtcMs = 1000;
            if (!logging::validConnectionHistoryEntry(entry))
                return false;
            history->recordStarted(std::move(entry));
        }
        QQmlComponent component(window.engine());
        component.loadFromModule(QStringLiteral("Ztermy"), QStringLiteral("WorkspaceLogsPane"));
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}}));
        auto *pane = qobject_cast<QQuickItem *>(object.get());
        if (!pane)
        {
            qWarning() << component.errors();
            return false;
        }
        pane->setParentItem(window.contentItem());
        pane->setSize({static_cast<qreal>(width), 760});
        pane->setZ(200);
        processWindowEventsFor(std::chrono::milliseconds{350});
        auto *drawer = pane->findChild<QObject *>(QStringLiteral("connectionHistoryDetails"));
        auto *list = quickItem(pane, "connectionHistoryList");
        if (!drawer || !list)
            return false;
        const auto pause = [] {
            processWindowEventsFor(std::chrono::milliseconds{250});
        };
        const auto key = [&] {
            return drawer->property("entryKey").toString();
        };
        const auto clickRow = [&](const char *name) {
            auto *row = visualQuickItem(pane, name);
            if (!row)
                return false;
            sendMouseClick(window, *row, {20, 20});
            return true;
        };
        const auto clickButton = [&](const char *name) {
            auto *button = visualQuickItem(window.contentItem(), name);
            if (!button)
                return false;
            sendMouseClick(window, *button, {button->width() / 2, button->height() / 2});
            return true;
        };
        const qreal listWidth = list->width();
        bool valid = clickRow("connectionHistoryRow-drawer-a");
        pause();
        valid = drawer->property("opened").toBool() && key() == QStringLiteral("drawer-a")
                && qAbs(list->width() - listWidth) < 0.5 && qAbs(drawer->property("slideOffset").toReal()) < 0.5
                && valid;
        qInfo() << "Drawer open/overlay width=" << width << "passed=" << valid;
        valid = clickRow("connectionHistoryRow-drawer-b") && valid;
        valid = clickRow("connectionHistoryRow-drawer-c") && valid;
        pause();
        valid = key() == QStringLiteral("drawer-c") && drawer->property("opened").toBool() && valid;
        qInfo() << "Drawer rapid row replacement width=" << width << "key=" << key() << "passed=" << valid;
        history->recordPhase(QStringLiteral("drawer-c"), QStringLiteral("ready"), QStringLiteral("connected"));
        pause();
        const auto payload = drawer->property("payload").value<QJSValue>().toVariant().toMap();
        valid = payload.value(QStringLiteral("status")).toString() == QStringLiteral("connected") && valid;
        valid = clickButton("sideDrawerPin") && valid;
        sendMouseClick(window, *pane, {10, 10});
        pause();
        valid = drawer->property("pinned").toBool() && drawer->property("opened").toBool() && valid;
        const auto present = [&](const QString &id) {
            const auto entries = history->entries();
            const auto found = std::ranges::find_if(entries, [&](const QVariant &entry) {
                return entry.toMap().value(QStringLiteral("id")).toString() == id;
            });
            return found != entries.end()
                   && QMetaObject::invokeMethod(drawer, "present", Q_ARG(QVariant, id), Q_ARG(QVariant, *found));
        };
        valid = present(QStringLiteral("drawer-a")) && valid;
        processWindowEventsFor(std::chrono::milliseconds{40});
        const qreal partialOffset = drawer->property("slideOffset").toReal();
        valid = key() == QStringLiteral("drawer-c") && partialOffset > 0 && valid;
        valid = present(QStringLiteral("drawer-b")) && valid;
        valid = qAbs(partialOffset - drawer->property("slideOffset").toReal()) < 0.5 && valid;
        pause();
        valid = key() == QStringLiteral("drawer-b") && !drawer->property("changingEntry").toBool() && valid;
        qInfo() << "Drawer interrupted animation width=" << width << "passed=" << valid;
        drawer->setProperty("pinned", false);
        auto *drawerBody = drawer->property("contentItem").value<QQuickItem *>();
        QQmlComponent menuComponent(window.engine());
        menuComponent.setData(R"qml(
            import QtQuick
            import Ztermy
            AppMenu {
                id: menu
                property bool activated: false
                AppMenuItem {
                    objectName: "drawerChildAction"
                    text: "Fixture"
                    onTriggered: menu.activated = true
                }
            }
        )qml",
                              QUrl{});
        std::unique_ptr<QObject> menu(
            menuComponent.createWithInitialProperties({{QStringLiteral("parent"), QVariant::fromValue(drawerBody)},
                                                       {QStringLiteral("x"), -150},
                                                       {QStringLiteral("y"), 50}}));
        if (!menu)
            return false;
        QMetaObject::invokeMethod(menu.get(), "open");
        pause();
        valid = clickButton("drawerChildAction") && menu->property("activated").toBool() && valid;
        pause();
        valid = drawer->property("opened").toBool() && valid;
        qInfo() << "Drawer child menu boundary width=" << width << "passed=" << valid;
        menu.reset();
        const auto directoryOption = arguments.indexOf(QStringLiteral("--data-dir"));
        if (directoryOption >= 0 && directoryOption + 1 < arguments.size())
            window.grabWindow().save(
                QDir(arguments.at(directoryOption + 1)).filePath(QStringLiteral("drawer-%1.png").arg(width)));
        valid = clickButton("sideDrawerClose") && valid;
        pause();
        valid = !drawer->property("visible").toBool() && valid;
        qInfo() << "Drawer live detail/pin/explicit close width=" << width << "passed=" << valid;
        drawer->setProperty("pinned", false);
        valid = clickRow("connectionHistoryRow-drawer-a") && valid;
        pause();
        sendMouseClick(window, *pane, {10, 10});
        pause();
        valid = !drawer->property("visible").toBool() && valid;
        valid = clickRow("connectionHistoryRow-drawer-b") && valid;
        pause();
        drawer->setProperty("dismissBlocked", true);
        valid = present(QStringLiteral("drawer-c")) && valid;
        sendMouseClick(window, *pane, {10, 10});
        valid = clickButton("sideDrawerClose") && valid;
        pause();
        valid = drawer->property("opened").toBool() && key() == QStringLiteral("drawer-b") && valid;
        drawer->setProperty("dismissBlocked", false);
        history->remove(QStringLiteral("drawer-b"));
        pause();
        valid = !drawer->property("visible").toBool() && valid;
        qInfo() << "Drawer outside/guard/removal width=" << width << "passed=" << valid;
        for (const auto *id : {"drawer-a", "drawer-c"})
            history->remove(QString::fromLatin1(id));
        passed = valid && passed;
        object.reset();
        pause();
    }
    return passed;
}
} // namespace ztermy::ui
