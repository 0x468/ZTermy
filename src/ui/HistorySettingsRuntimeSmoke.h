#pragma once

#include "application/AppController.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace ztermy::ui
{
inline bool verifyHistorySettings(NativeWindow &window, AppController &controller, const QString &outputDirectory)
{
    using namespace std::chrono_literals;
    auto *root = window.rootObject();
    if (!QMetaObject::invokeMethod(root, "openSettingsTab"))
        return false;
    processWindowEventsFor(300ms);
    auto *pane = quickItem(root, "settingsPane");
    if (!pane)
        return false;
    pane->setProperty("currentCategory", QStringLiteral("terminal"));
    QMetaObject::invokeMethod(pane, "jumpToRow", Q_ARG(QVariant, QStringLiteral("terminal")),
                              Q_ARG(QVariant, QStringLiteral("terminalHistoryLines")));
    processWindowEventsFor(300ms);
    auto *history = quickItem(root, "settingsTerminalHistoryLines");
    if (!history || !history->isVisible() || history->property("value").toInt() != controller.terminalHistoryLines())
        return false;
    const auto dataIndex = QCoreApplication::arguments().indexOf(QStringLiteral("--data-dir"));
    if (dataIndex < 0)
        return false;
    const auto document = [&] {
        QFile file(QDir(QCoreApplication::arguments().value(dataIndex + 1)).filePath(QStringLiteral("settings.json")));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    };
    const auto activate = [&](const char *name) {
        auto *item = quickItem(root, name);
        if (!focusItem(window, item, QString::fromLatin1(name)))
            return false;
        sendKey(window, Qt::Key_Space);
        processWindowEventsFor(300ms);
        return true;
    };
    const auto original = document();
    const int previous = controller.terminalHistoryLines();
    auto *editor = history->property("contentItem").value<QQuickItem *>();
    if (!editor)
        return false;
    editor->forceActiveFocus();
    sendKey(window, Qt::Key_A, Qt::ControlModifier);
    sendText(window, QStringLiteral("20000"));
    sendKey(window, Qt::Key_Tab);
    if (history->property("value").toInt() != 20000)
        return false;
    if (controller.terminalHistoryLines() != previous || document() != original || !activate("settingsDiscard")
        || history->property("value").toInt() != previous)
        return false;
    history->setProperty("value", 20000);
    if (!activate("settingsApply") || controller.terminalHistoryLines() != 20000)
        return false;
    const auto saved = QJsonDocument::fromJson(document()).object();
    if (saved.value(QStringLiteral("version")).toInt() != 46
        || saved.value(QStringLiteral("terminalHistoryLines")).toInt() != 20000)
        return false;
    bool passed = true;
    for (const auto *theme : {"ztermy-dark", "ztermy-light"})
    {
        passed = controller.saveTerminalTheme(QString::fromLatin1(theme)) && passed;
        processWindowEventsFor(200ms);
        passed =
            window.grabWindow().save(
                QDir(outputDirectory).filePath(QStringLiteral("history-settings-%1.png").arg(QLatin1StringView{theme})))
            && passed;
    }
    history->setProperty("value", 30000);
    QMetaObject::invokeMethod(pane, "loadDraft");
    passed = history->property("value").toInt() == 20000 && passed;
    qInfo() << "History settings: draft, discard, apply, persistence, reload, themes:" << passed;
    return passed;
}
} // namespace ztermy::ui
