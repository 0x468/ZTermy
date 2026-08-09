#include "domain/workbench/ScriptDefinition.h"

#include <QTest>

#include <string>

namespace
{

[[nodiscard]] ztermy::workbench::ScriptDefinition sampleScript()
{
    using namespace ztermy::workbench;
    return {
        .id = "script-1",
        .name = "Inspect service",
        .description = "Checks a service and prints its mode",
        .shellScope = ShellScope::posix,
        .variables =
            {{.name = "service", .label = "Service", .type = ScriptVariableType::text, .required = true},
             {.name = "count", .label = "Count", .type = ScriptVariableType::integer, .defaultValue = "3"},
             {.name = "verbose", .label = "Verbose", .type = ScriptVariableType::boolean, .defaultValue = "false"},
             {.name = "mode",
              .label = "Mode",
              .type = ScriptVariableType::choice,
              .defaultValue = "brief",
              .choices = {"brief", "full"}}},
        .steps = {{.command = "systemctl status ${service} --lines=${count}"},
                  {.command = "echo ${mode}:${verbose}",
                   .continuation = ScriptContinuation::literalOutput,
                   .outputMarker = "ready:${service}",
                   .timeoutMs = 5'000}},
        .createdUtcMs = 100,
        .modifiedUtcMs = 200,
    };
}

} // namespace

class ScriptDefinitionTests final : public QObject
{
    Q_OBJECT

private slots:
    void validatesTypedVariablesAndBounds();
    void rendersTemplatesAndDefaults();
    void rejectsMissingInvalidAndUnknownValues();
    void migratesQuickCommandWithoutLoss();
};

void ScriptDefinitionTests::validatesTypedVariablesAndBounds()
{
    auto script = sampleScript();
    QVERIFY(ztermy::workbench::validScriptDefinition(script));

    script.variables.push_back(script.variables.front());
    QVERIFY(!ztermy::workbench::validScriptDefinition(script));
    script = sampleScript();
    script.steps.front().command.assign(ztermy::workbench::maximumScriptStepBytes + 1, 'x');
    QVERIFY(!ztermy::workbench::validScriptDefinition(script));
    script = sampleScript();
    script.steps.back().timeoutMs = ztermy::workbench::maximumOutputTimeoutMs + 1;
    QVERIFY(!ztermy::workbench::validScriptDefinition(script));
}

void ScriptDefinitionTests::rendersTemplatesAndDefaults()
{
    const auto rendered = ztermy::workbench::renderScript(sampleScript(), {{"service", "sshd"}});
    QVERIFY(rendered);
    QCOMPARE(rendered->steps.size(), std::size_t{2});
    QCOMPARE(rendered->steps[0].command, std::string("systemctl status sshd --lines=3"));
    QCOMPARE(rendered->steps[1].command, std::string("echo brief:false"));
    QCOMPARE(rendered->steps[1].outputMarker, std::string("ready:sshd"));
}

void ScriptDefinitionTests::rejectsMissingInvalidAndUnknownValues()
{
    using ztermy::workbench::ScriptRenderError;
    const auto missing = ztermy::workbench::renderScript(sampleScript(), {});
    QVERIFY(!missing);
    QCOMPARE(missing.error(), ScriptRenderError::missingVariable);

    const auto invalid = ztermy::workbench::renderScript(sampleScript(), {{"service", "sshd"}, {"count", "many"}});
    QVERIFY(!invalid);
    QCOMPARE(invalid.error(), ScriptRenderError::invalidVariableValue);

    auto unknownTemplate = sampleScript();
    unknownTemplate.steps.front().command = "echo ${missing}";
    const auto unknown = ztermy::workbench::renderScript(unknownTemplate, {{"service", "sshd"}});
    QVERIFY(!unknown);
    QCOMPARE(unknown.error(), ScriptRenderError::unknownTemplateVariable);
}

void ScriptDefinitionTests::migratesQuickCommandWithoutLoss()
{
    const ztermy::workbench::QuickCommand command{.id = "legacy",
                                                  .name = "Legacy",
                                                  .command = "echo one\necho two",
                                                  .description = "Imported",
                                                  .shellScope = ztermy::workbench::ShellScope::powershell,
                                                  .createdUtcMs = 10,
                                                  .modifiedUtcMs = 20};
    const auto script = ztermy::workbench::scriptFromQuickCommand(command);
    QVERIFY(ztermy::workbench::validScriptDefinition(script));
    QCOMPARE(script.id, command.id);
    QCOMPARE(script.steps.front().command, command.command);
    QCOMPARE(script.createdUtcMs, command.createdUtcMs);
}

QTEST_GUILESS_MAIN(ScriptDefinitionTests)

#include "script_definition_tests.moc"
