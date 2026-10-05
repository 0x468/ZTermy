#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/WindowStateRuntimeSmoke.h"

#include <QQmlComponent>
#include <QQmlEngine>

#include <memory>

namespace ztermy::ui
{
// Real stored-script rendering and real QML event paths, with a dispatch spy:
// these fixtures never start a Shell or send the proxy command to a session.
inline bool verifyScriptComposer(NativeWindow &window, AppController &controller)
{
    const QVariantMap variable{{QStringLiteral("name"), QStringLiteral("PROXY")},
                               {QStringLiteral("label"), QStringLiteral("Proxy")},
                               {QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("defaultValue"), QStringLiteral("http://127.0.0.1:7890")},
                               {QStringLiteral("required"), true}};
    const auto step = [](const QString &command, const QString &continuation = QStringLiteral("immediate")) {
        return QVariantMap{{QStringLiteral("command"), command},
                           {QStringLiteral("continuation"), continuation},
                           {QStringLiteral("outputMarker"),
                            continuation == QStringLiteral("immediate") ? QString{} : QStringLiteral("READY")},
                           {QStringLiteral("timeoutMs"), 30000}};
    };
    const auto save = [&](const QString &name, const QVariantList &variables, const QVariantList &steps) {
        return controller.saveScript({{QStringLiteral("name"), name},
                                      {QStringLiteral("description"), QString{}},
                                      {QStringLiteral("shell"), QStringLiteral("powershell")},
                                      {QStringLiteral("variables"), variables},
                                      {QStringLiteral("steps"), steps}});
    };
    if (!save(QStringLiteral("proxy fixture"), {variable}, {step(QStringLiteral("$env:HTTP_PROXY = '${PROXY}'"))})
        || !save(QStringLiteral("wait fixture"), {},
                 {step(QStringLiteral("Write-Output READY"), QStringLiteral("literal-output")),
                  step(QStringLiteral("Write-Output DONE"))})
        || !save(QStringLiteral("plain fixture"), {}, {step(QStringLiteral("Write-Output plain"))}))
        return false;
    const auto scriptNamed = [&](const QString &name) {
        for (const auto &entry : controller.quickCommands())
            if (entry.toMap().value(QStringLiteral("name")).toString() == name)
                return entry.toMap();
        return QVariantMap{};
    };
    const auto proxy = scriptNamed(QStringLiteral("proxy fixture"));
    const auto wait = scriptNamed(QStringLiteral("wait fixture"));
    const auto plain = scriptNamed(QStringLiteral("plain fixture"));
    auto *engine = window.engine();
    QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
    auto mock = engine->evaluate(QStringLiteral(
        "({terminalTabs:[{sessionId:'fixture-session',running:true,scriptExecutionState:'idle'}],"
        "runs:[],rawCommands:[],renderScript:function(id,values){return this.real.renderScript(id,values);},"
        "runScript:function(id,values,target){this.runs.push({id:id,values:values,target:target});return true;},"
        "runTerminalCommand:function(command){this.rawCommands.push(command);return true;}})"));
    mock.setProperty(QStringLiteral("real"), engine->newQObject(&controller));
    mock.setPrototype(engine->newQObject(&controller));
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/TerminalComposer.qml")));
    const QVariantMap tab{{QStringLiteral("id"), QStringLiteral("fixture-tab")},
                          {QStringLiteral("sessionId"), QStringLiteral("fixture-session")},
                          {QStringLiteral("running"), true}};
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(mock)}, {QStringLiteral("activeTab"), tab}}));
    auto *composer = qobject_cast<QQuickItem *>(object.get());
    if (!composer)
    {
        qWarning() << component.errors();
        return false;
    }
    window.resize(1000, 800);
    window.show();
    window.hide();
    window.show();
    composer->setParentItem(window.contentItem());
    composer->setPosition({20, 620});
    composer->setSize({900, 150});
    composer->setZ(100);
    processWindowEventsFor(std::chrono::milliseconds{250});
    auto *input = quickItem(composer, "terminalComposerInput");
    auto *dialog = composer->findChild<QObject *>(QStringLiteral("scriptActionDialog"));
    auto *runPane = composer->findChild<QObject *>(QStringLiteral("scriptActionRunPane"));
    auto *submit = quickItem(composer, "scriptSubmit");
    if (!input || !dialog || !runPane || !submit)
        return false;
    const auto activate = [&](const QVariantMap &script, bool execute) {
        const bool invoked =
            QMetaObject::invokeMethod(composer, "activateSnippet", Q_ARG(QVariant, script), Q_ARG(QVariant, execute),
                                      Q_ARG(QVariant, QVariant::fromValue(input)));
        processWindowEventsFor(std::chrono::milliseconds{250});
        return invoked;
    };
    const auto submitAction = [&] {
        sendMouseClick(window, *submit, {submit->width() / 2, submit->height() / 2});
        processWindowEventsFor(std::chrono::milliseconds{250});
    };
    bool passed = activate(proxy, false) && dialog->property("opened").toBool()
                  && input->property("text").toString().isEmpty()
                  && runPane->property("preview").toMap().value(QStringLiteral("ok")).toBool();
    passed = captureWindowSmokeItem(window.contentItem(), QStringLiteral("script-parameter-dialog.png")) && passed;
    submitAction();
    passed =
        passed && input->property("text").toString() == QStringLiteral("$env:HTTP_PROXY = 'http://127.0.0.1:7890'");
    input->setProperty("text", QStringLiteral("existing draft"));
    passed = activate(proxy, false) && passed;
    QMetaObject::invokeMethod(dialog, "close");
    processWindowEventsFor(std::chrono::milliseconds{250});
    passed = passed && input->property("text").toString() == QStringLiteral("existing draft");
    passed = activate(proxy, true) && passed;
    QMetaObject::invokeMethod(runPane, "setVariableValue", Q_ARG(QVariant, 0),
                              Q_ARG(QVariant, QStringLiteral("http://127.0.0.1:1080")));
    processWindowEventsFor(std::chrono::milliseconds{80});
    submitAction();
    passed = passed && mock.property(QStringLiteral("runs")).property(QStringLiteral("length")).toInt() == 1
             && mock.property(QStringLiteral("runs"))
                        .property(0)
                        .property(QStringLiteral("values"))
                        .property(QStringLiteral("PROXY"))
                        .toString()
                    == QStringLiteral("http://127.0.0.1:1080");
    passed = activate(wait, false) && !runPane->property("insertOnly").toBool() && passed;
    submitAction();
    passed = passed && mock.property(QStringLiteral("runs")).property(QStringLiteral("length")).toInt() == 2
             && input->property("text").toString() == QStringLiteral("existing draft");
    passed = activate(plain, true) && passed;
    passed = passed && mock.property(QStringLiteral("runs")).property(QStringLiteral("length")).toInt() == 3;
    passed = activate(proxy, false) && passed;
    QMetaObject::invokeMethod(runPane, "setVariableValue", Q_ARG(QVariant, 0), Q_ARG(QVariant, QString{}));
    processWindowEventsFor(std::chrono::milliseconds{80});
    passed = passed && !submit->isEnabled();
    composer->setProperty("activeTab", QVariant{});
    processWindowEventsFor(std::chrono::milliseconds{250});
    passed = passed && !dialog->property("visible").toBool()
             && mock.property(QStringLiteral("rawCommands")).property(QStringLiteral("length")).toInt() == 0;
    qInfo() << "Script composer: default and edited parameters, cancellation, required validation, waited steps, "
               "plain execution, target switch, no raw template dispatch: passed="
            << passed;
    return passed;
}
} // namespace ztermy::ui
