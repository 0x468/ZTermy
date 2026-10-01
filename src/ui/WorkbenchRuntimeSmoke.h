#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/DetachedWindowRuntimeSmoke.h"
#include "ui/ResizeRuntimeSmoke.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/SideDrawerRuntimeSmoke.h"
#include "ui/SidePanelLayoutRuntimeSmoke.h"
#include "ui/TitleBarRuntimeSmoke.h"
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
                             "terminalAiAssistantButton"})
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
    // Real press/release: feedback must not move the hit target or leave the
    // glyph shrunken after release. Reduced/off effects suppress scaling.
    auto *theme = window.engine()->singletonInstance<QObject *>(qmlTypeId("Ztermy", 1, 0, "Theme"));
    auto *glyph = button->property("contentItem").value<QQuickItem *>();
    if (!theme || !glyph)
        return false;
    const auto previousTier = theme->property("effectsTier");
    const QRectF originalBounds(button->position(), button->size());
    const QPointF pressPoint = button->mapToScene(QPointF{button->width() / 2, button->height() / 2});
    for (const auto *tier : {"full", "reduced", "off"})
    {
        theme->setProperty("effectsTier", QString::fromLatin1(tier));
        move(pressPoint);
        qt_handleMouseEvent(&window, pressPoint, window.mapToGlobal(pressPoint.toPoint()), Qt::LeftButton,
                            Qt::LeftButton, QEvent::MouseButtonPress, Qt::NoModifier, static_cast<int>(GetTickCount()));
        processWindowEventsFor(std::chrono::milliseconds{150});
        const bool full = QString::fromLatin1(tier) == QStringLiteral("full");
        const bool pressValid = button->property("down").toBool()
                                && (full ? glyph->scale() < 1 : qAbs(glyph->scale() - 1) < 0.001)
                                && QRectF(button->position(), button->size()) == originalBounds;
        qt_handleMouseEvent(&window, pressPoint, window.mapToGlobal(pressPoint.toPoint()), Qt::NoButton, Qt::LeftButton,
                            QEvent::MouseButtonRelease, Qt::NoModifier, static_cast<int>(GetTickCount()));
        processWindowEventsFor(std::chrono::milliseconds{150});
        const bool valid = pressValid && !button->property("down").toBool() && qAbs(glyph->scale() - 1) < 0.001;
        qInfo() << "Shared icon button press/release" << tier << "passed=" << valid;
        passed = valid && passed;
    }
    theme->setProperty("effectsTier", previousTier);
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
    if (arguments.contains(QStringLiteral("--title-bar-immersive-smoke")))
        return verifyImmersiveTitleBar(window, controller);
    if (arguments.contains(QStringLiteral("--material-theme-capture")))
    {
        const auto dataIndex = arguments.indexOf(QStringLiteral("--data-dir"));
        if (dataIndex < 0 || dataIndex + 1 >= arguments.size())
            return false;
        QDir captures(QDir(arguments[dataIndex + 1]).filePath(QStringLiteral("captures")));
        if (!captures.mkpath(QStringLiteral(".")))
            return false;
        QQuickWindow backing;
        backing.setFlags(Qt::Window | Qt::FramelessWindowHint);
        backing.setGeometry(window.screen()->availableGeometry());
        backing.setColor(QColor(QStringLiteral("#238C62")));
        backing.show();
        window.setGeometry(100, 100, 900, 620);
        window.hide();
        window.show();
        auto *root = window.rootObject();
        if (controller.startLocalTerminal().isEmpty())
            return false;
        processWindowEventsFor(std::chrono::milliseconds{800});
        root->setProperty("currentPage", QStringLiteral("terminal"));
        root->setProperty("previewEffectsTier", QStringLiteral("full"));
        root->setProperty("previewBackdropOpacity", 0.0);
        root->setProperty("appearancePreviewActive", true);
        bool passed = true;
        for (const auto *material : {"transparent", "aero", "acrylic", "mica", "micaAlt", "solid"})
            for (const auto *palette : {"ztermy-dark", "ztermy-light"})
            {
                passed = controller.saveTerminalTheme(QString::fromLatin1(palette)) && passed;
                processWindowEventsFor(std::chrono::milliseconds{200});
                root->setProperty("currentPage", QStringLiteral("terminal"));
                root->setProperty("previewEffectsTier", QStringLiteral("full"));
                root->setProperty("previewBackdropOpacity", 0.0);
                root->setProperty("previewBackdropPreference", QString::fromLatin1(material));
                root->setProperty("appearancePreviewActive", true);
                QMetaObject::invokeMethod(root, "applyWindowAppearance");
                window.raise();
                window.requestActivate();
                processWindowEventsFor(std::chrono::milliseconds{600});
                QColor firstDesktop;
                for (const auto *color : {"#238C62", "#BA486D"})
                {
                    backing.setColor(QColor(QString::fromLatin1(color)));
                    backing.lower();
                    window.showNormal();
                    window.setAlwaysOnTop(true);
                    window.raise();
                    processWindowEventsFor(std::chrono::milliseconds{300});
                    const QString name =
                        QStringLiteral("%1-%2-%3")
                            .arg(QLatin1StringView{material}, QLatin1StringView{palette}, QLatin1StringView{color + 1});
                    const auto scene = window.grabWindow();
                    const auto origin = window.mapToGlobal(QPoint{}) - window.screen()->geometry().topLeft();
                    const auto desktop = window.screen()
                                             ->grabWindow(0, origin.x(), origin.y(), window.width(), window.height())
                                             .toImage();
                    if (scene.isNull() || desktop.isNull())
                        return false;
                    const QColor center = desktop.pixelColor(desktop.width() / 2, desktop.height() / 2);
                    // Reject a hidden/occluded test window, even if its backing
                    // colors happen to satisfy the transparency comparison.
                    const QPoint iconPoint(qRound(20 * scene.devicePixelRatio()),
                                           qRound(20 * scene.devicePixelRatio()));
                    const auto icon = scene.pixelColor(iconPoint);
                    const auto screenIcon = desktop.pixelColor(iconPoint);
                    passed = icon.alpha() == 255 && qAbs(icon.red() - screenIcon.red()) < 8
                             && qAbs(icon.green() - screenIcon.green()) < 8 && qAbs(icon.blue() - screenIcon.blue()) < 8
                             && passed;
                    if (!firstDesktop.isValid())
                        firstDesktop = center;
                    else if (QLatin1StringView{material} == "transparent")
                    {
                        const int difference = qAbs(center.red() - firstDesktop.red())
                                               + qAbs(center.green() - firstDesktop.green())
                                               + qAbs(center.blue() - firstDesktop.blue());
                        passed = difference > 40 && passed;
                        qInfo() << "Transparent desktop response" << palette << difference;
                    }
                    else if (QLatin1StringView{material} == "solid")
                        passed = center == firstDesktop && passed;
                    passed = scene.save(captures.filePath(name + QStringLiteral("-scene.png"))) && passed;
                    passed = desktop.save(captures.filePath(name + QStringLiteral("-desktop.png"))) && passed;
                    qInfo() << "Material capture" << name << "rootColor=" << root->property("color")
                            << "clearColor=" << window.color() << "alphaBits=" << window.format().alphaBufferSize()
                            << "sceneCenter=" << scene.pixelColor(scene.width() / 2, scene.height() / 2)
                            << "desktopCenter=" << desktop.pixelColor(desktop.width() / 2, desktop.height() / 2);
                }
                if (QLatin1StringView{material} == "transparent" || QLatin1StringView{material} == "aero"
                    || QLatin1StringView{material} == "acrylic")
                {
                    root->setProperty("previewBackdropOpacity", 1.0);
                    window.requestUpdate();
                    processWindowEventsFor(std::chrono::milliseconds{400});
                    const auto opaqueScene = window.grabWindow();
                    processWindowEventsFor(std::chrono::milliseconds{100});
                    const auto origin = window.mapToGlobal(QPoint{}) - window.screen()->geometry().topLeft();
                    const auto opaque = window.screen()
                                            ->grabWindow(0, origin.x(), origin.y(), window.width(), window.height())
                                            .toImage();
                    if (opaque.isNull())
                        return false;
                    const auto center = opaque.pixelColor(opaque.width() / 2, opaque.height() / 2);
                    const auto expected = root->property("color").value<QColor>();
                    passed = !opaqueScene.isNull()
                             && opaqueScene.pixelColor(opaqueScene.width() / 2, opaqueScene.height() / 2).alpha() == 255
                             && passed;
                    passed = qAbs(center.red() - expected.red()) < 3 && qAbs(center.green() - expected.green()) < 3
                             && qAbs(center.blue() - expected.blue()) < 3 && passed;
                    passed = opaque.save(
                                 captures.filePath(QStringLiteral("%1-%2-opaque.png")
                                                       .arg(QLatin1StringView{material}, QLatin1StringView{palette})))
                             && passed;
                }
            }
        root->setProperty("currentPage", QStringLiteral("hosts"));
        root->setProperty("workspaceNavigationWidth", 220);
        auto *theme = window.engine()->singletonInstance<QObject *>(qmlTypeId("Ztermy", 1, 0, "Theme"));
        if (theme)
            theme->setProperty("animationsEnabled", false);
        root->setProperty("pageReveal", 1.0);
        processWindowEventsFor(std::chrono::milliseconds{1200});
        passed = window.grabWindow().save(captures.filePath(QStringLiteral("host-import-export.png"))) && passed;
        qInfo() << "Material desktop capture checks passed=" << passed;
        return passed;
    }
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
        return verifyScriptFormLayout(window, controller) && verifySidePanelLayout(window, controller);
    if (arguments.contains(QStringLiteral("--profile-card-icons-smoke")))
    {
        window.resize(1000, 760);
        window.show();
        processWindowEventsFor(std::chrono::milliseconds{600});
        bool passed = true;
        for (const auto &[name, expected] :
             {QPair{QStringLiteral("savedHostProfileIcon-target"), QStringLiteral("brand-ubuntu")},
              QPair{QStringLiteral("savedHostProfileIcon-jump"), QStringLiteral("database")},
              QPair{QStringLiteral("recentHostProfileIcon-target"), QStringLiteral("brand-ubuntu")}})
        {
            auto *icon = findWindowSmokeItem(window.contentItem(), name);
            const bool valid = icon && icon->isVisible() && icon->property("name").toString() == expected;
            qInfo() << "Host card profile icon" << name << "passed=" << valid;
            passed = passed && valid;
        }
        return captureWindowSmokeItem(window.contentItem(), QStringLiteral("profile-card-icons.png")) && passed;
    }
    if (arguments.contains(QStringLiteral("--profile-icons-smoke")))
    {
        auto *theme = window.engine()->singletonInstance<QObject *>(qmlTypeId("Ztermy", 1, 0, "Theme"));
        if (!theme)
            return false;
        window.resize(1000, 760);
        window.show();
        bool passed = verifyPaneShellMenu(window);
        int repeats = arguments.contains(QStringLiteral("--repeat-profile-icons")) ? 10 : 1;
        for (const auto &argument : arguments)
            if (argument.startsWith(QStringLiteral("--profile-icon-cycles=")))
                repeats = qBound(1, argument.sliced(22).toInt(), 30);
        for (int repeat = 0; repeat < repeats; ++repeat)
        {
            for (const auto *mode : {"dark", "light"})
            {
                theme->setProperty("terminalPalette", QVariantMap{});
                theme->setProperty("preference", QString::fromLatin1(mode));
                processWindowEventsFor(std::chrono::milliseconds{250});
                passed = passed && theme->property("dark").toBool() == (QLatin1StringView{mode} == "dark");
                passed =
                    verifyHostProfileIconPicker(window, QStringLiteral("profile-icons-%1.png").arg(mode)) && passed;
            }
            qInfo() << "Profile icon lifecycle round=" << repeat + 1
                    << "liveTreeObjects=" << window.contentItem()->findChildren<QObject *>().size();
        }
        return passed;
    }
    return runSideDrawerRuntimeCheck(window, controller, arguments);
}
} // namespace ztermy::ui
