#pragma once

#include "application/AppController.h"
#include "ui/ApplicationInfo.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/terminal/TerminalItem.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QScopeGuard>

namespace ztermy::ui
{
inline bool verifyFontTypography(NativeWindow &window, AppController &controller, const QString &directory)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    const auto oldTabs = controller.terminalTabs();
    for (const auto &old : oldTabs)
        controller.closeTerminalTab(old.toMap().value(QStringLiteral("id")).toString());
    window.setGeometry(80, 60, 1120, 860);
    showForRuntimeSmoke(window);
    window.requestActivate();
    const auto id = controller.startLocalTerminalWithShell(QStringLiteral("commandPrompt"));
    if (id.isEmpty())
        return false;
    root->setProperty("currentPage", QStringLiteral("terminal"));
    processWindowEventsFor(500ms);
    auto *viewport = qobject_cast<TerminalItem *>(terminalViewportItem(root));
    if (!viewport || !QMetaObject::invokeMethod(root, "openSettingsTab"))
        return false;
    processWindowEventsFor(300ms);
    auto *pane = quickItem(root, "settingsPane");
    if (!pane)
        return false;
    pane->setProperty("currentCategory", QStringLiteral("terminal"));
    auto *size = quickItem(root, "settingsFontSize");
    auto *weight = quickItem(root, "settingsFontWeight");
    auto *line = quickItem(root, "settingsLineHeight");
    auto *width = quickItem(root, "settingsCellWidth");
    auto *sample = quickItem(root, "settingsFontSample");
    auto *preview = qobject_cast<TerminalItem *>(quickItem(sample, "terminalFontPreview"));
    if (!size || !weight || !line || !width || !preview)
        return false;
    auto *sampleViewport = quickItem(sample, "fontSampleViewport");
    auto *sampleScrollBar = quickItem(sample, "fontSampleHorizontalScrollBar");
    if (!sampleViewport || !sampleScrollBar
        || sampleScrollBar->mapToItem(sample, QPointF{}).y()
               < sampleViewport->mapToItem(sample, QPointF{0, sampleViewport->height()}).y())
        return false;
    const auto document = [&] {
        QFile file(QDir(directory).filePath(QStringLiteral("settings.json")));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto activate = [&](const char *name) {
        if (!focusItem(window, quickItem(root, name), QString::fromLatin1(name)))
            return false;
        sendKey(window, Qt::Key_Space);
        processWindowEventsFor(250ms);
        return true;
    };
    const auto original = document();
    const auto originalSize = controller.terminalFontSize();
    auto *editor = size->property("contentItem").value<QQuickItem *>();
    if (!editor)
        return false;
    editor->forceActiveFocus();
    sendKey(window, Qt::Key_A, Qt::ControlModifier);
    sendText(window, QStringLiteral("11.25"));
    sendKey(window, Qt::Key_Tab);
    weight->setProperty("currentIndex", 5);
    line->setProperty("value", 125);
    width->setProperty("value", 110);
    processWindowEventsFor(200ms);
    qInfo() << "Typography draft" << size->property("value") << preview->fontPointSize() << preview->fontWeight()
            << preview->lineHeightScale() << preview->cellWidthScale() << controller.terminalFontSize()
            << viewport->fontPointSize() << "unchanged=" << (document() == original);
    if (size->property("value").toInt() != 1125 || preview->fontPointSize() != 11.25 || preview->fontWeight() != 600
        || preview->lineHeightScale() != 1.25 || preview->cellWidthScale() != 1.1
        || controller.terminalFontSize() != originalSize || viewport->fontPointSize() != originalSize
        || document() != original || !activate("settingsDiscard")
        || size->property("value").toInt() != qRound(originalSize * 100))
    {
        qWarning() << "Typography preview/discard contract failed; value=" << size->property("value")
                   << "saved=" << originalSize;
        return false;
    }
    size->setProperty("value", 1250);
    weight->setProperty("currentIndex", 5);
    line->setProperty("value", 125);
    width->setProperty("value", 110);
    if (!activate("settingsApply") || controller.terminalFontSize() != 12.5 || controller.terminalFontWeight() != 600
        || controller.terminalLineHeight() != 1.25 || controller.terminalCellWidth() != 1.1
        || viewport->fontPointSize() != 12.5 || viewport->fontWeight() != 600 || viewport->lineHeightScale() != 1.25
        || viewport->cellWidthScale() != 1.1)
    {
        qWarning() << "Typography apply/live-viewport contract failed";
        return false;
    }
    const auto persisted = QJsonDocument::fromJson(document()).object();
    if (persisted.value(QStringLiteral("version")).toInt() != 46
        || persisted.value(QStringLiteral("terminalFontSize")).toDouble() != 12.5)
        return false;
    if (!controller.setActiveTerminalAppearance(controller.terminalFontFamily(), 11.25, true, 0.0,
                                                QStringLiteral("bar"), QStringLiteral("#FFFFFF"),
                                                QStringLiteral("#101010"), 500, 1.4, 1.2))
        return false;
    processWindowEventsFor(200ms);
    if (viewport->fontPointSize() != 11.25 || viewport->fontWeight() != 500 || viewport->lineHeightScale() != 1.4
        || viewport->cellWidthScale() != 1.2 || !controller.resetActiveTerminalAppearance())
        return false;
    processWindowEventsFor(200ms);
    if (viewport->fontPointSize() != 12.5 || viewport->fontWeight() != 600)
        return false;
    for (const auto *theme : {"ztermy-dark", "ztermy-light"})
    {
        if (!controller.saveTerminalTheme(QString::fromLatin1(theme)))
            return false;
        for (const auto sizeValue : {QSize{1120, 860}, QSize{540, 780}})
        {
            window.resize(sizeValue);
            QMetaObject::invokeMethod(pane, "jumpToRow", Q_ARG(QVariant, QStringLiteral("terminal")),
                                      Q_ARG(QVariant, QStringLiteral("terminalFontSize")));
            processWindowEventsFor(400ms);
            const auto sampleName =
                QStringLiteral("font-%1-%2.png").arg(QLatin1StringView{theme}).arg(sizeValue.width());
            if (!window.grabWindow().save(QDir(directory).filePath(sampleName)))
                return false;
        }
    }
    QMetaObject::invokeMethod(pane, "loadDraft");
    if (size->property("value").toInt() != 1250 || weight->property("currentIndex").toInt() != 5)
        return false;
    window.resize({1120, 860});
    root->setProperty("currentPage", QStringLiteral("terminal"));
    processWindowEventsFor(300ms);
    auto *popover = root->findChild<QObject *>(QStringLiteral("terminalSessionSettingsPopover"));
    if (!popover)
        return false;
    if (!QMetaObject::invokeMethod(popover, "openFor", Q_ARG(QVariant, QVariant::fromValue(viewport))))
        return false;
    processWindowEventsFor(300ms);
    if (!QMetaObject::invokeMethod(popover, "close"))
        return false;
    processWindowEventsFor(300ms);
    if (controller.terminalTabs().constFirst().toMap().value(QStringLiteral("sessionFontSize")).toDouble() != 0)
    {
        qWarning() << "Unedited session settings created an appearance override";
        return false;
    }
    for (const auto *effects : {"full", "reduced", "off"})
    {
        if (!controller.saveEffectsTier(QString::fromLatin1(effects))
            || !QMetaObject::invokeMethod(popover, "openFor", Q_ARG(QVariant, QVariant::fromValue(viewport))))
            return false;
        const bool motion = QLatin1StringView{effects} != QLatin1StringView{"off"};
        const bool entering = popover->property("visible").toBool() && popover->property("opacity").toReal() < 1.0;
        processWindowEventsFor(300ms);
        auto *popupItem = popover->property("contentItem").value<QQuickItem *>();
        auto *sessionSize = quickItem(popupItem, "sessionFontSize");
        if (!sessionSize || sessionSize->property("value").toInt() != 1250
            || quickItem(popupItem, "terminalFontPreview") != nullptr || popover->property("opacity").toReal() != 1.0
            || popover->property("height").toReal() > window.height() - 16 || (motion && !entering))
        {
            qWarning() << "Session typography enter" << effects << "entering=" << entering
                       << "opacity=" << popover->property("opacity") << "height=" << popover->property("height");
            return false;
        }
        const auto beforeLivePreview = document();
        sessionSize->setProperty("value", 1425);
        processWindowEventsFor(180ms);
        if (viewport->fontPointSize() != 14.25 || document() != beforeLivePreview
            || controller.terminalFontSize() != 12.5)
        {
            qWarning() << "Session live typography changed durable global state or failed to update the pane";
            return false;
        }
        if (QLatin1StringView{effects} == QLatin1StringView{"full"}
            && !window.grabWindow().save(QDir(directory).filePath(QStringLiteral("font-session-popover.png"))))
            return false;
        sessionSize->setProperty("value", 1475);
        if (!QMetaObject::invokeMethod(popover, "close"))
            return false;
        if (controller.terminalTabs().constFirst().toMap().value(QStringLiteral("sessionFontSize")).toDouble() != 14.75)
        {
            qWarning() << "Closing session settings lost the pending live adjustment";
            return false;
        }
        auto *shield = popupItem->parentItem();
        const auto shieldVisible = [&] {
            return shield && shield->window() == &window && shield->parentItem() && shield->isVisible();
        };
        if (motion && !shieldVisible())
        {
            qWarning() << "Session typography exit lost the popup item" << effects;
            return false;
        }
        for (int frame = 0; frame < 40 && (popover->property("visible").toBool() || shieldVisible()); ++frame)
            processWindowEventsFor(50ms);
        processWindowEventsFor(50ms);
        if (popover->property("visible").toBool() || shieldVisible() || viewport->fontPointSize() != 14.75)
        {
            qWarning() << "Session typography exit did not finish" << effects;
            return false;
        }
        if (!controller.resetActiveTerminalAppearance())
            return false;
        processWindowEventsFor(100ms);
    }
    if (!QMetaObject::invokeMethod(root, "openSettingsTab"))
        return false;
    pane->setProperty("currentCategory", QStringLiteral("about"));
    processWindowEventsFor(250ms);
    auto *identity = quickItem(root, "settingsRuntimeIdentity");
    const ApplicationInfo info;
    if (!identity || !identity->property("text").toString().contains(info.platformDescription())
        || !identity->property("text").toString().contains(info.buildCommit())
        || identity->property("text").toString().contains(QStringLiteral("C++23")))
        return false;
    auto *clipboard = QGuiApplication::clipboard();
    auto previousClipboard = std::make_unique<QMimeData>();
    if (const auto *previous = clipboard->mimeData())
        for (const auto &format : previous->formats())
            previousClipboard->setData(format, previous->data(format));
    const QScopeGuard restoreClipboard([&] {
        clipboard->setMimeData(previousClipboard.release());
    });
    clipboard->setText(QStringLiteral("build-identity-copy-probe"));
    const QPointF copyPoint{identity->width() / 2.0, identity->height() / 2.0};
    sendMouseClick(window, *identity, copyPoint);
    sendMouseClick(window, *identity, copyPoint);
    if (clipboard->text() != info.buildCommit())
    {
        qWarning() << "About build revision double-click did not copy the short commit";
        return false;
    }
    qInfo() << "Typography: fractional input, real renderer preview, discard, apply, persistence, live pane, override, "
               "reset, themes and breakpoints passed";
    return true;
}
} // namespace ztermy::ui
