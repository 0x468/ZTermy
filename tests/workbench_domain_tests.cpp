#include "domain/workbench/ScriptRecorder.h"
#include "domain/workbench/ShellHistory.h"
#include "domain/workbench/WorkspaceState.h"

#include <QTest>

#include <string_view>

using namespace std::string_view_literals;

class WorkbenchDomainTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesTimestampedBashAndMultilineEntries();
    void parsesExtendedZshHistory();
    void parsesFishHistoryEscapes();
    void parsesPowerShellContinuationsAndCapsNewestEntries();
    void recordsBoundedRecentRemotePaths();
    void recordsOnlyStructuredCommandsWithBoundedDelays();
};

void WorkbenchDomainTests::recordsOnlyStructuredCommandsWithBoundedDelays()
{
    using namespace std::chrono_literals;
    ztermy::workbench::ScriptRecorder recorder;
    QVERIFY(recorder.start(0ms));
    QVERIFY(recorder.recordCommand("echo first", 100ms));
    QVERIFY(recorder.recordCommand("echo second", 2'500ms));
    QVERIFY(recorder.pause());
    QVERIFY(!recorder.recordCommand("password", 3'000ms));
    QVERIFY(recorder.resume(20'000ms));
    QVERIFY(recorder.recordCommand("pwd", 20'100ms));
    QVERIFY(recorder.stop());

    QCOMPARE(recorder.state(), ztermy::workbench::ScriptRecorderState::Review);
    QCOMPARE(recorder.steps().size(), std::size_t{4});
    QCOMPARE(recorder.steps()[0].kind, ztermy::workbench::RecordedScriptStepKind::Send);
    QCOMPARE(recorder.steps()[1].kind, ztermy::workbench::RecordedScriptStepKind::Delay);
    QCOMPARE(recorder.steps()[1].delayMilliseconds, std::uint32_t{2'400});
    QCOMPARE(recorder.steps()[2].command, std::string("echo second"));
    QCOMPARE(recorder.steps()[3].command, std::string("pwd"));
    recorder.clear();
    QCOMPARE(recorder.state(), ztermy::workbench::ScriptRecorderState::Idle);
    QVERIFY(recorder.steps().empty());
}

void WorkbenchDomainTests::parsesTimestampedBashAndMultilineEntries()
{
    const auto entries =
        ztermy::workbench::parseBashHistory("#1700000000\nprintf '%s\\n' one\ntwo\n#1700000001\necho newest\n"sv);
    QCOMPARE(entries.size(), std::size_t{2});
    QCOMPARE(entries[0].command, std::string("echo newest"));
    QCOMPARE(entries[0].timestampUtcSeconds, std::optional<std::int64_t>{1'700'000'001});
    QCOMPARE(entries[1].command, std::string("printf '%s\\n' one\ntwo"));
}

void WorkbenchDomainTests::parsesExtendedZshHistory()
{
    const auto entries =
        ztermy::workbench::parseZshHistory(": 1700000000:2;echo older\n: 1700000001:0;printf first\nsecond\n"sv);
    QCOMPARE(entries.size(), std::size_t{2});
    QCOMPARE(entries[0].command, std::string("printf first\nsecond"));
    QCOMPARE(entries[0].timestampUtcSeconds, std::optional<std::int64_t>{1'700'000'001});
    QCOMPARE(entries[1].command, std::string("echo older"));
}

void WorkbenchDomainTests::parsesFishHistoryEscapes()
{
    const auto entries = ztermy::workbench::parseFishHistory(
        "- cmd: echo older\n  when: 1700000000\n- cmd: printf one\\ntwo\\\\three\n  when: 1700000001\n"sv);
    QCOMPARE(entries.size(), std::size_t{2});
    QCOMPARE(entries[0].command, std::string("printf one\ntwo\\three"));
    QCOMPARE(entries[0].timestampUtcSeconds, std::optional<std::int64_t>{1'700'000'001});
}

void WorkbenchDomainTests::parsesPowerShellContinuationsAndCapsNewestEntries()
{
    const auto entries =
        ztermy::workbench::parsePowerShellHistory("Get-Date\nWrite-Output `\n  value\nGet-Location\n"sv, 2);
    QCOMPARE(entries.size(), std::size_t{2});
    QCOMPARE(entries[0].command, std::string("Get-Location"));
    QCOMPARE(entries[1].command, std::string("Write-Output `\n  value"));
    QCOMPARE(entries[0].shell, ztermy::workbench::ShellKind::powershell);
}

void WorkbenchDomainTests::recordsBoundedRecentRemotePaths()
{
    ztermy::workbench::WorkspaceState workspace;
    auto &profile = ztermy::workbench::ensureProfileWorkspaceState(workspace, "profile-1");
    for (int index = 0; index < 15; ++index)
    {
        ztermy::workbench::recordRecentRemotePath(profile, "/srv/path-" + std::to_string(index));
    }
    ztermy::workbench::recordRecentRemotePath(profile, "/srv/path-10");

    QCOMPARE(profile.recentRemotePaths.size(), ztermy::workbench::maximumRecentRemotePaths);
    QCOMPARE(profile.recentRemotePaths.front(), std::string("/srv/path-10"));
    QCOMPARE(profile.lastRemotePath, std::string("/srv/path-10"));
    QVERIFY(ztermy::workbench::validProfileWorkspaceState(profile));
    QCOMPARE(ztermy::workbench::findProfileWorkspaceState(workspace, "profile-1"), &profile);
}

QTEST_GUILESS_MAIN(WorkbenchDomainTests)

#include "workbench_domain_tests.moc"
