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
} // namespace ztermy::ui
