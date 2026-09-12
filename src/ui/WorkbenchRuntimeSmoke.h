#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/DetachedWindowRuntimeSmoke.h"
#include "ui/ResizeRuntimeSmoke.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/SideDrawerRuntimeSmoke.h"
#include "ui/terminal/TerminalPaneRuntimeSmoke.h"

#include <QColor>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <algorithm>
#include <memory>
#include <optional>

namespace ztermy::ui
{
// Isolated presentation check: creates no sessions and never executes script text.
inline bool verifyScriptFormLayout(NativeWindow &window, AppController &controller)
{
    window.show();
    window.hide();
    window.show();
    bool passed = true;
    for (const auto *file : {"ScriptEditor", "ScriptRunPane"})
    {
        QQmlComponent component(window.engine(),
                                QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/%1.qml").arg(QLatin1StringView{file})));
        QVariantMap properties{{QStringLiteral("controller"), QVariant::fromValue(&controller)}};
        if (QLatin1StringView{file} == QLatin1StringView{"ScriptRunPane"})
            properties.insert(QStringLiteral("activeTab"), QVariantMap{});
        std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
        auto *form = qobject_cast<QQuickItem *>(object.get());
        if (!form)
        {
            qWarning() << component.errors();
            return false;
        }
        form->setParentItem(window.contentItem());
        form->setZ(100);
        form->setHeight(660);
        if (QLatin1StringView{file} == QLatin1StringView{"ScriptEditor"})
            QMetaObject::invokeMethod(form, "beginNew", Q_ARG(QVariant, QStringLiteral("echo layout fixture")));
        for (const int width : {320, 520, 800})
        {
            form->setWidth(width);
            processWindowEventsFor(std::chrono::milliseconds{350});
            auto *scroll = form->findChild<QQuickItem *>(QStringLiteral("scriptFormScroll"));
            auto *content = form->findChild<QQuickItem *>(QStringLiteral("scriptFormContent"));
            bool valid = scroll && content && content->width() > width - 40
                         && qAbs(content->width() - scroll->property("availableWidth").toReal()) < 1;
            if (QLatin1StringView{file} == QLatin1StringView{"ScriptEditor"})
            {
                // Repeater delegates belong to the visual tree, not the form's QObject tree.
                QQuickItem *step = nullptr;
                if (content)
                    for (auto *child : content->childItems())
                        if (child->objectName() == QStringLiteral("scriptStepCard"))
                            step = child;
                auto *fields = step ? step->findChild<QQuickItem *>(QStringLiteral("scriptStepFields")) : nullptr;
                valid = valid && step && fields && step->height() >= fields->implicitHeight() + 15;
            }
            qInfo() << "Script form layout" << file << "width=" << width << "passed=" << valid
                    << "contentWidth=" << (content ? content->width() : -1)
                    << "availableWidth=" << (scroll ? scroll->property("availableWidth").toReal() : -1);
            passed = valid && passed;
        }
    }
    return passed;
}
// Real QML hit testing with two independent synthetic terminal snapshots.
// This never creates a process/SSH connection or sends terminal input.
inline bool verifyPaneScrollbarLayout(NativeWindow &window, AppController &controller)
{
    window.resize(1100, 800);
    window.show();
    window.hide();
    window.show();
    QQmlComponent component(window.engine(),
                            QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/src/ui/qml/TerminalSplitNode.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(&controller)}, {QStringLiteral("paneCount"), 2}}));
    auto *surface = qobject_cast<QQuickItem *>(object.get());
    if (!surface)
    {
        qWarning() << component.errors();
        return false;
    }
    surface->setParentItem(window.contentItem());
    surface->setPosition({20, 70});
    surface->setSize({960, 640});
    surface->setZ(100);
    const auto leaf = [](const QString &id, bool active) {
        return QVariantMap{{QStringLiteral("kind"), QStringLiteral("leaf")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("active"), active},
                           {QStringLiteral("tab"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("local")},
                                                               {QStringLiteral("title"), id},
                                                               {QStringLiteral("running"), true},
                                                               {QStringLiteral("localExited"), false}}}};
    };
    const auto find = [surface](const QString &name) -> QQuickItem * {
        QList<QQuickItem *> pending{surface};
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if (item->objectName() == name)
                return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    bool passed = true;
    for (const auto *orientation : {"horizontal", "vertical", "detached"})
    {
        const bool detached = QLatin1StringView{orientation} == QLatin1StringView{"detached"};
        surface->setProperty("detachedPane", detached);
        const QVariantMap layout = detached
                                       ? leaf(QStringLiteral("a"), true)
                                       : QVariantMap{{QStringLiteral("kind"), QStringLiteral("split")},
                                                     {QStringLiteral("orientation"), QString::fromLatin1(orientation)},
                                                     {QStringLiteral("ratio"), 0.5},
                                                     {QStringLiteral("first"), leaf(QStringLiteral("a"), true)},
                                                     {QStringLiteral("second"), leaf(QStringLiteral("b"), false)}};
        surface->setProperty("node", layout);
        for (const bool headers : {false, true})
        {
            surface->setProperty("headersVisible", headers);
            processWindowEventsFor(std::chrono::milliseconds{400});
            auto *a = qobject_cast<TerminalItem *>(find(QStringLiteral("terminalViewport-a")));
            auto *b = qobject_cast<TerminalItem *>(find(QStringLiteral("terminalViewport-b")));
            auto snapshot = std::make_shared<terminal::TerminalSnapshot>();
            snapshot->columns = 12;
            snapshot->rows = 8;
            snapshot->cells.resize(96);
            snapshot->scrollbar = {.total = 100, .offset = 40, .visible = 20};
            if (!a || (!detached && !b))
                return false;
            a->setSnapshot(snapshot);
            if (b)
                b->setSnapshot(snapshot);
            processWindowEventsFor(std::chrono::milliseconds{100});
            int aScrolls = 0;
            int bScrolls = 0;
            const auto aConnection = QObject::connect(a, &TerminalItem::scrollRequested, a, [&] {
                ++aScrolls;
            });
            const auto bConnection = b ? QObject::connect(b, &TerminalItem::scrollRequested, b,
                                                          [&] {
                                                              ++bScrolls;
                                                          })
                                       : QMetaObject::Connection{};
            auto *bar = find(QStringLiteral("terminalPaneScrollbar-a"));
            auto *thumb = find(QStringLiteral("terminalPaneScrollbarThumb-a"));
            auto *actions = find(QStringLiteral("terminalPaneActions-a"));
            bool valid =
                bar && thumb && actions && bar->isVisible()
                && bar->mapToScene(QPointF{0, 0}).y() >= actions->mapToScene(QPointF{0, actions->height()}).y();
            if (valid)
            {
                const qreal thumbHeight = thumb->height();
                const QPointF start = bar->mapToScene({bar->width() / 2, bar->height() / 2});
                const auto send = [&window](const QPointF &point, Qt::MouseButtons buttons, Qt::MouseButton button,
                                            QEvent::Type type) {
                    qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), buttons, button, type,
                                        Qt::NoModifier, static_cast<int>(GetTickCount()));
                    processWindowEventsFor(std::chrono::milliseconds{30});
                };
                send(start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
                valid = qAbs(thumb->height() - thumbHeight) < 0.5;
                send(start + QPointF{0, 40}, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
                send(start + QPointF{0, 80}, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
                send(start + QPointF{0, 80}, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
            }
            valid = valid && aScrolls >= 2 && bScrolls == 0;
            QObject::disconnect(aConnection);
            QObject::disconnect(bConnection);
            if (!detached)
            {
                auto *button = find(QStringLiteral("terminalPaneAction-new-a"));
                if (button)
                    sendMouseClick(window, *button, {button->width() / 2, button->height() / 2});
                auto *menu = surface->findChild<QObject *>(QStringLiteral("terminalNewPaneMenu-a"));
                valid = valid && button && menu && menu->property("visible").toBool();
                if (menu)
                    QMetaObject::invokeMethod(menu, "close");
            }
            qInfo() << "Pane scrollbar layout/hit test" << orientation << "headers=" << headers << "passed=" << valid;
            passed = valid && passed;
        }
    }
    return passed;
}
inline bool verifyToolbarHoverAnimation(NativeWindow &window, AppController &controller)
{
    window.show();
    window.hide();
    window.show();
    QQmlComponent component(window.engine());
    component.setData(R"qml(import QtQuick
import Ztermy
TerminalWorkbench {
    width: 520; height: 640
    Component.onCompleted: {
        Theme.preference = "light";
        Theme.highContrast = false;
        Theme.animationsEnabled = true;
        Theme.backdropOpacity = 1;
    }
})qml",
                      QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/hover-test.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(&controller)},
         {QStringLiteral("activeTab"), QVariantMap{{QStringLiteral("workbenchPage"), QStringLiteral("scripts")}}},
         {QStringLiteral("panelSide"), QStringLiteral("left")}}));
    auto *workbench = qobject_cast<QQuickItem *>(object.get());
    if (!workbench)
    {
        qWarning() << component.errors();
        return false;
    }
    workbench->setParentItem(window.contentItem());
    workbench->setPosition({20, 70});
    workbench->setZ(100);
    auto *button = workbench->findChild<QQuickItem *>(QStringLiteral("terminalHistoryPageButton"));
    if (!button)
        return false;
    auto *background = button->property("background").value<QQuickItem *>();
    if (!background || background->childItems().isEmpty())
        return false;
    auto *fill = background->childItems().constFirst();
    const auto move = [&window](const QPointF &point) {
        qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), Qt::NoButton, Qt::NoButton,
                            QEvent::MouseMove, Qt::NoModifier, static_cast<int>(GetTickCount()));
    };
    move({500, 500});
    processWindowEventsFor(std::chrono::milliseconds{250});
    move(button->mapToScene(QPointF{button->width() / 2, button->height() / 2}));
    qreal darkestComposite = 255;
    bool stayedHovered = true;
    for (int sample = 0; sample < 12; ++sample)
    {
        processWindowEventsFor(std::chrono::milliseconds{16});
        const auto color = fill->property("color").value<QColor>();
        const qreal composite = 255.0 * (color.redF() * color.alphaF() + (1 - color.alphaF()));
        darkestComposite = std::min(darkestComposite, composite);
        stayedHovered = stayedHovered && button->property("hovered").toBool();
    }
    const auto finalColor = fill->property("color").value<QColor>();
    const qreal finalComposite = 255.0 * (finalColor.redF() * finalColor.alphaF() + (1 - finalColor.alphaF()));
    bool passed = stayedHovered && darkestComposite + 2 >= finalComposite;
    for (const auto *name : {"terminalRemoteFilesPageButton", "terminalHistoryPageButton", "terminalScriptsPageButton",
                             "terminalNotesPageButton", "terminalAiAssistantButton"})
    {
        auto *page = workbench->findChild<QQuickItem *>(QLatin1StringView{name});
        if (!page)
            return false;
        move(page->mapToScene(QPointF{page->width() / 2, page->height() / 2}));
        processWindowEventsFor(std::chrono::milliseconds{700});
        auto *tip = page->findChild<QObject *>(QStringLiteral("iconButtonToolTip"));
        const bool valid = !page->property("label").toString().isEmpty()
                           && !page->property("iconName").toString().isEmpty() && tip
                           && tip->property("visible").toBool() && tip->property("opacity").toReal() > 0.99
                           && tip->property("text") == page->property("toolTipText");
        qInfo() << "Shared icon button tooltip" << name << "passed=" << valid;
        passed = valid && passed;
    }
    button->forceActiveFocus(Qt::TabFocusReason);
    passed = button->property("visualFocus").toBool() && passed;
    for (const auto *supplied : {"label: \"fixture\"", "iconName: \"folder\""})
    {
        QQmlComponent missingProperty(window.engine());
        missingProperty.setData(QByteArray("import Ztermy\nAppIconButton { ") + supplied + " }", QUrl{});
        std::unique_ptr<QObject> invalid(missingProperty.create());
        const bool rejected = !invalid && missingProperty.isError();
        qInfo() << "Shared icon button missing required field rejected=" << rejected;
        passed = rejected && passed;
    }
    return passed;
}
inline bool verifyHistoryScrollPreservation(NativeWindow &window, AppController &controller)
{
    window.show();
    window.hide();
    window.show();
    QQmlComponent component(window.engine(), QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/TerminalWorkbench.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(&controller)},
         {QStringLiteral("activeTab"), QVariantMap{{QStringLiteral("workbenchPage"), QStringLiteral("history")}}},
         {QStringLiteral("panelSide"), QStringLiteral("left")}}));
    auto *workbench = qobject_cast<QQuickItem *>(object.get());
    if (!workbench)
    {
        qWarning() << component.errors();
        return false;
    }
    workbench->setParentItem(window.contentItem());
    workbench->setPosition({20, 70});
    workbench->setSize({520, 640});
    workbench->setZ(100);
    processWindowEventsFor(std::chrono::milliseconds{350});
    auto *list = workbench->findChild<QQuickItem *>(QStringLiteral("terminalHistoryList"));
    if (!list)
        return false;
    if (QCoreApplication::arguments().contains(QStringLiteral("--history-autoload-smoke")))
    {
        // CMD has no supported history file: exercise the real UI triggers without reading user history
        // or sending any terminal input. A synthetic successful result represents the cached state.
        const QString session = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
        if (session.isEmpty())
            return false;
        QVariantMap tab{{QStringLiteral("sessionId"), session},
                        {QStringLiteral("running"), false},
                        {QStringLiteral("workbenchPage"), QStringLiteral("history")}};
        workbench->setProperty("activeTab", tab);
        processWindowEventsFor(std::chrono::milliseconds{150});
        bool valid = controller.terminalHistoryState() == QStringLiteral("idle");
        tab.insert(QStringLiteral("running"), true);
        workbench->setProperty("activeTab", tab);
        processWindowEventsFor(std::chrono::milliseconds{150});
        valid = controller.terminalHistoryState() == QStringLiteral("error") && valid;
        for (const bool changePage : {false, true})
        {
            valid = QMetaObject::invokeMethod(&controller, "terminalHistoryTaskCompleted", Q_ARG(QString, session),
                                              Q_ARG(quint64, 0), Q_ARG(ShellHistoryEntries, ShellHistoryEntries{}),
                                              Q_ARG(QString, QString{}))
                    && valid;
            processWindowEventsFor(std::chrono::milliseconds{100});
            valid = controller.terminalHistoryState() == QStringLiteral("ready") && valid;
            if (changePage)
                tab.insert(QStringLiteral("workbenchPage"), QStringLiteral("scripts"));
            else
                workbench->setVisible(false);
            workbench->setProperty("activeTab", tab);
            processWindowEventsFor(std::chrono::milliseconds{100});
            tab.insert(QStringLiteral("workbenchPage"), QStringLiteral("history"));
            workbench->setProperty("activeTab", tab);
            workbench->setVisible(true);
            processWindowEventsFor(std::chrono::milliseconds{150});
            valid = controller.terminalHistoryState() == QStringLiteral("error") && valid;
        }
        qInfo() << "History auto load on ready/reopen/page entry passed=" << valid;
        controller.closeTerminalTab(session);
        return valid;
    }
    QVariantList entries;
    for (int index = 0; index < 100; ++index)
        entries.append(QVariantMap{{QStringLiteral("command"), QStringLiteral("history fixture %1").arg(index)},
                                   {QStringLiteral("sourceLabel"), QStringLiteral("fixture")}});
    const auto replace = [&](const QString &session, const QString &search = {}) {
        const bool invoked = QMetaObject::invokeMethod(workbench, "replaceHistoryEntries", Q_ARG(QVariant, entries),
                                                       Q_ARG(QVariant, session), Q_ARG(QVariant, search));
        processWindowEventsFor(std::chrono::milliseconds{150});
        return invoked;
    };
    const auto position = [&] {
        return list->property("contentY").toReal() - list->property("originY").toReal();
    };
    bool passed = replace(QStringLiteral("a"));
    list->setProperty("currentIndex", 22);
    list->setProperty("contentY", 1000);
    passed = replace(QStringLiteral("a")) && qAbs(position() - 1000) < 1 && passed;
    entries.prepend(QVariantMap{{QStringLiteral("command"), QStringLiteral("new fixture")},
                                {QStringLiteral("sourceLabel"), QStringLiteral("fixture")}});
    passed = replace(QStringLiteral("a")) && qAbs(position() - 1049) < 1 && list->property("currentIndex").toInt() == 23
             && passed;
    qInfo() << "History scroll unchanged/prepend" << "position=" << position() << "passed=" << passed;
    passed = replace(QStringLiteral("b")) && qAbs(position()) < 1 && passed;
    list->setProperty("contentY", 1000);
    passed = replace(QStringLiteral("b"), QStringLiteral("changed search")) && qAbs(position()) < 1 && passed;
    qInfo() << "History scroll session/search reset" << "passed=" << passed;
    entries.clear();
    passed = replace(QStringLiteral("b")) && passed;
    object.reset();
    processWindowEventsFor(std::chrono::milliseconds{150});
    return passed;
}
inline std::optional<bool> runWorkbenchRuntimeCheck(NativeWindow &window, AppController &controller,
                                                    const QStringList &arguments)
{
    if (arguments.contains(QStringLiteral("--resize-interactions-smoke")))
        return verifyResizeInteractions(window, controller);
    if (arguments.contains(QStringLiteral("--detached-material-smoke")))
        return verifyDetachedWindowSurface(window, controller);
    if (arguments.contains(QStringLiteral("--history-scroll-smoke"))
        || arguments.contains(QStringLiteral("--history-autoload-smoke")))
        return verifyHistoryScrollPreservation(window, controller);
    if (arguments.contains(QStringLiteral("--toolbar-hover-smoke")))
        return verifyToolbarHoverAnimation(window, controller);
    if (arguments.contains(QStringLiteral("--pane-scrollbar-smoke")))
        return verifyPaneScrollbarLayout(window, controller);
    if (arguments.contains(QStringLiteral("--script-form-layout-smoke")))
        return verifyScriptFormLayout(window, controller);
    return runSideDrawerRuntimeCheck(window, controller, arguments);
}
} // namespace ztermy::ui
