#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/DetachedWindowRuntimeSmoke.h"
#include "ui/ResizeRuntimeSmoke.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/SideDrawerRuntimeSmoke.h"
#include "ui/SidePanelLayoutRuntimeSmoke.h"
#include "ui/TitleBarRuntimeSmoke.h"
#include "ui/TitleTriggerMaterialRuntimeSmoke.h"
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
// Reproduce a different computer's catalog without touching its registry, PATH,
// or user settings. Inspect the real menu, not a second copy of its filter.
inline bool verifyPaneShellMenu(NativeWindow &window)
{
    QQmlComponent component(window.engine());
    component.setData(R"(import QtQuick
import Ztermy
Item {
    id: fixture
    width: 800; height: 600
    property var shellCatalog: []
    property QtObject mock: QtObject {
        property var availableLocalShells: fixture.shellCatalog
        property var hostProfiles: []
    }
    TerminalPaneToolbar {
        x: 40; y: 60
        controller: fixture.mock
        paneId: "fixture"
        revealed: true
    }
})",
                      QUrl(QStringLiteral("qrc:/shell-menu-fixture.qml")));
    std::unique_ptr<QObject> object(component.create());
    auto *fixture = qobject_cast<QQuickItem *>(object.get());
    if (!fixture)
    {
        qWarning() << component.errors();
        return false;
    }
    fixture->setParentItem(window.contentItem());
    fixture->setZ(100);
    auto *menu = fixture->findChild<QObject *>(QStringLiteral("terminalNewPaneMenu-fixture"));
    if (!menu)
        return false;
    bool passed = true;
    for (const bool nuInstalled : {false, true, false})
    {
        fixture->setProperty("shellCatalog",
                             QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("automatic")},
                                                      {QStringLiteral("available"), true}},
                                          QVariantMap{{QStringLiteral("id"), QStringLiteral("powerShellCore")},
                                                      {QStringLiteral("name"), QStringLiteral("PowerShell 7")},
                                                      {QStringLiteral("iconName"), QStringLiteral("brand-powershell")},
                                                      {QStringLiteral("available"), true}},
                                          QVariantMap{{QStringLiteral("id"), QStringLiteral("nushell")},
                                                      {QStringLiteral("name"), QStringLiteral("Nushell")},
                                                      {QStringLiteral("iconName"), QStringLiteral("shell-nushell")},
                                                      {QStringLiteral("available"), nuInstalled}}});
        if (!QMetaObject::invokeMethod(menu, "open"))
            return false;
        processWindowEventsFor(std::chrono::milliseconds{350});
        auto *content = qvariant_cast<QQuickItem *>(menu->property("contentItem"));
        auto *pwsh = findWindowSmokeItem(content, QStringLiteral("newPaneShell-powerShellCore"));
        auto *nu = findWindowSmokeItem(content, QStringLiteral("newPaneShell-nushell"));
        const bool valid = pwsh && pwsh->isVisible()
                           && pwsh->property("iconName").toString() == QStringLiteral("brand-powershell")
                           && (nuInstalled ? nu && nu->isVisible() : nu == nullptr)
                           && menu->property("count").toInt() == (nuInstalled ? 4 : 3);
        qInfo() << "Pane menu catalog: Nu installed=" << nuInstalled << "passed=" << valid;
        passed = valid && passed;
        if (!nuInstalled)
            passed = captureWindowSmokeItem(window.contentItem(), QStringLiteral("pane-shell-menu.png")) && passed;
        QMetaObject::invokeMethod(menu, "close");
        processWindowEventsFor(std::chrono::milliseconds{150});
    }
    return passed;
}

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
    for (const auto *orientation : {"horizontal", "vertical", "single"})
    {
        const bool single = QLatin1StringView{orientation} == QLatin1StringView{"single"};
        const QVariantMap layout = single
                                       ? leaf(QStringLiteral("a"), true)
                                       : QVariantMap{{QStringLiteral("kind"), QStringLiteral("split")},
                                                     {QStringLiteral("orientation"), QString::fromLatin1(orientation)},
                                                     {QStringLiteral("ratio"), 0.5},
                                                     {QStringLiteral("first"), leaf(QStringLiteral("a"), true)},
                                                     {QStringLiteral("second"), leaf(QStringLiteral("b"), false)}};
        surface->setProperty("node", layout);
        for (const bool toolbarRevealed : {false, true})
        {
            processWindowEventsFor(std::chrono::milliseconds{400});
            auto *actions = find(QStringLiteral("terminalPaneActions-a"));
            if (!actions)
                return false;
            actions->setProperty("revealed", toolbarRevealed);
            auto *a = qobject_cast<TerminalItem *>(find(QStringLiteral("terminalViewport-a")));
            auto *b = qobject_cast<TerminalItem *>(find(QStringLiteral("terminalViewport-b")));
            auto snapshot = std::make_shared<terminal::TerminalSnapshot>();
            snapshot->columns = 12;
            snapshot->rows = 8;
            snapshot->cells.resize(96);
            snapshot->scrollbar = {.total = 100, .offset = 40, .visible = 20};
            if (!a || (!single && !b))
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
            bool valid =
                bar && thumb && actions && bar->isVisible() && qAbs(a->y()) < 1
                && (!toolbarRevealed
                    || bar->mapToScene(QPointF{0, 0}).y() >= actions->mapToScene(QPointF{0, actions->height()}).y());
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
            qInfo() << "Pane scrollbar layout/hit test" << orientation << "toolbar=" << toolbarRevealed
                    << "passed=" << valid;
            passed = valid && passed;
        }
    }
    return passed;
}
} // namespace ztermy::ui
