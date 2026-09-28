#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

#include <memory>

namespace ztermy::ui
{
// Tests the reported geometry failure, not merely successful QML construction.
// Uses synthetic presentation state: no Shell or SSH session is created.
inline bool verifySidePanelLayout(NativeWindow &window, AppController &controller)
{
    QQmlComponent component(window.engine(), QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/TerminalWorkbench.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(&controller)},
         {QStringLiteral("panelSide"), QStringLiteral("left")},
         {QStringLiteral("activeTab"), QVariantMap{{QStringLiteral("workbenchPage"), QStringLiteral("scripts")}}}}));
    auto *pane = qobject_cast<QQuickItem *>(object.get());
    if (!pane)
    {
        qWarning() << component.errors();
        return false;
    }
    pane->setParentItem(window.contentItem());
    pane->setZ(1000);
    bool passed = !pane->findChild<QObject *>(QStringLiteral("terminalNotesPageButton"));
    const auto captureRoot = qEnvironmentVariable("ZTERMY_SIDEBAR_CAPTURE_DIR");
    for (const QSize size : {QSize{320, 420}, QSize{440, 820}, QSize{600, 600}})
    {
        window.resize(qMax(window.minimumWidth(), size.width() + 40), size.height() + 40);
        pane->setSize(size);
        processWindowEventsFor(std::chrono::milliseconds{250});
        auto *empty = pane->findChild<QQuickItem *>(QStringLiteral("scriptLibraryEmptyState"));
        auto *content = empty ? empty->findChild<QQuickItem *>(QStringLiteral("statePanelContent")) : nullptr;
        auto *search = pane->findChild<QQuickItem *>(QStringLiteral("scriptLibrarySearch"));
        auto *create = pane->findChild<QQuickItem *>(QStringLiteral("scriptLibraryCreateButton"));
        bool valid = empty && content && search && create && empty->isVisible() && content->height() < 180
                     && content->height() > 40 && search->mapToItem(pane, QPointF{}).y() < 100 && content->y() >= 17
                     && content->y() + content->height() <= empty->height() - 17;
        if (valid)
        {
            // A taller empty area must move the group, not stretch its text rows.
            const qreal compactHeight = content->height();
            pane->setHeight(size.height() + 140);
            processWindowEventsFor(std::chrono::milliseconds{100});
            valid = qAbs(content->height() - compactHeight) < 1;
            pane->setHeight(size.height());
            search->setProperty("text", QStringLiteral("no-match-for-sidebar-regression"));
            processWindowEventsFor(std::chrono::milliseconds{100});
            valid = valid && empty->isVisible() && content->height() < 180;
            search->setProperty("text", QString{});
            processWindowEventsFor(std::chrono::milliseconds{100});
            if (!captureRoot.isEmpty())
            {
                QDir().mkpath(captureRoot);
                valid = window.grabWindow().save(
                            QDir(captureRoot)
                                .filePath(QStringLiteral("scripts-%1x%2.png").arg(size.width()).arg(size.height())))
                        && valid;
            }
            QMetaObject::invokeMethod(create, "clicked");
            processWindowEventsFor(std::chrono::milliseconds{100});
            valid = valid && pane->property("scriptSurface").toString() == QStringLiteral("editor");
            pane->setProperty("scriptSurface", QStringLiteral("library"));
        }
        qInfo() << "Side panel geometry" << size << "passed=" << valid;
        passed = valid && passed;
    }
    // Spy on navigation requests while retaining the real controller's read-only
    // presentation properties. No Shell, SSH connection or AI request is started.
    auto *engine = window.engine();
    QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
    auto navigation = engine->evaluate(QStringLiteral(
        "(function() { const state = {requestCount: 0};"
        "state.toggleTerminalWorkbench = function(page) { state.requestCount++; state.requestedPage = page; };"
        "return state; })()"));
    navigation.setPrototype(engine->newQObject(&controller));
    pane->setProperty("controller", QVariant::fromValue(navigation));
    const QList<QPair<QString, QString>> pages{
        {QStringLiteral("sftp"), QStringLiteral("terminalRemoteFilesPageButton")},
        {QStringLiteral("history"), QStringLiteral("terminalHistoryPageButton")},
        {QStringLiteral("scripts"), QStringLiteral("terminalScriptsPageButton")},
        {QStringLiteral("ai"), QStringLiteral("terminalAiAssistantButton")}};
    for (const auto &[page, buttonName] : pages)
    {
        pane->setProperty("activeTab", QVariantMap{{QStringLiteral("workbenchPage"), page}});
        processWindowEventsFor(std::chrono::milliseconds{100});
        auto *button = pane->findChild<QQuickItem *>(buttonName);
        if (!button)
            return false;
        const int before = navigation.property(QStringLiteral("requestCount")).toInt();
        for (int click = 0; click < 2; ++click)
            sendMouseClick(window, *button, {button->width() / 2, button->height() / 2});
        const bool unchanged = navigation.property(QStringLiteral("requestCount")).toInt() == before
                               && button->property("checked").toBool() && button->property("selected").toBool();
        // The same button must still request navigation when another page is active.
        pane->setProperty("activeTab", QVariantMap{{QStringLiteral("workbenchPage"), page == QStringLiteral("history")
                                                                                         ? QStringLiteral("scripts")
                                                                                         : QStringLiteral("history")}});
        processWindowEventsFor(std::chrono::milliseconds{100});
        sendMouseClick(window, *button, {button->width() / 2, button->height() / 2});
        const bool switched = navigation.property(QStringLiteral("requestCount")).toInt() == before + 1
                              && navigation.property(QStringLiteral("requestedPage")).toString() == page;
        qInfo() << "Side panel navigation" << page << "repeatNoOp=" << unchanged << "switch=" << switched;
        passed = passed && unchanged && switched;
    }
    pane->setProperty("controller", QVariant::fromValue(&controller));
    return passed;
}
} // namespace ztermy::ui
