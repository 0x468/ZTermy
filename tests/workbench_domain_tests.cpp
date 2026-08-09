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
    void mutatesBoundedTerminalWorkspaceTreeAtomically();
    void rejectsInvalidTerminalWorkspaceTopology();
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

void WorkbenchDomainTests::mutatesBoundedTerminalWorkspaceTreeAtomically()
{
    using namespace ztermy::workbench;
    TerminalWorkspaceLayout layout = makeSinglePaneTerminalWorkspace(
        "workspace", "pane-a", TerminalRestoreIntent{.id = "intent-a", .title = "PowerShell"});
    QVERIFY(validTerminalWorkspaceLayout(layout));

    QVERIFY(splitTerminalPane(layout, "pane-a", "split-root", "pane-b",
                              TerminalRestoreIntent{
                                  .id = "intent-b",
                                  .profileId = "profile-b",
                                  .title = "Host B",
                                  .kind = TerminalRestoreKind::SshProfile,
                              },
                              TerminalSplitOrientation::Vertical, 0.6));
    QVERIFY(splitTerminalPane(layout, "pane-b", "split-right", "pane-c",
                              TerminalRestoreIntent{.id = "intent-c", .title = "PowerShell 2"},
                              TerminalSplitOrientation::Horizontal, 0.4, false));
    QCOMPARE(terminalPaneOrder(layout), std::vector<std::string>({"pane-a", "pane-c", "pane-b"}));
    QCOMPARE(layout.activePaneId, std::string("pane-c"));

    QVERIFY(resizeTerminalSplit(layout, "split-root", 0.55));
    QVERIFY(!resizeTerminalSplit(layout, "split-root", 0.1));
    QVERIFY(swapTerminalPanes(layout, "pane-a", "pane-b"));
    QVERIFY(closeTerminalPane(layout, "pane-c"));
    QCOMPARE(terminalPaneOrder(layout), std::vector<std::string>({"pane-a", "pane-b"}));
    QVERIFY(validTerminalWorkspaceLayout(layout));

    const TerminalWorkspaceLayout unchanged = layout;
    QVERIFY(!closeTerminalPane(layout, "missing"));
    QCOMPARE(layout, unchanged);
}

void WorkbenchDomainTests::rejectsInvalidTerminalWorkspaceTopology()
{
    using namespace ztermy::workbench;
    TerminalWorkspaceLayout layout = makeSinglePaneTerminalWorkspace(
        "workspace", "pane-a", TerminalRestoreIntent{.id = "intent-a", .title = "PowerShell"});
    QVERIFY(validTerminalWorkspaceLayout(layout));

    TerminalWorkspaceLayout orphaned = layout;
    orphaned.nodes.push_back(TerminalLayoutNode{.id = "orphan", .restoreIntentId = "intent-a"});
    QVERIFY(!validTerminalWorkspaceLayout(orphaned));

    TerminalWorkspaceLayout cyclic = layout;
    cyclic.nodes.front() = TerminalLayoutNode{
        .id = "pane-a",
        .firstChildId = "pane-a",
        .secondChildId = "pane-a",
        .kind = TerminalLayoutNodeKind::Split,
    };
    QVERIFY(!validTerminalWorkspaceLayout(cyclic));

    TerminalWorkspaceLayout duplicateIntent = layout;
    duplicateIntent.restoreIntents.push_back(duplicateIntent.restoreIntents.front());
    QVERIFY(!validTerminalWorkspaceLayout(duplicateIntent));

    WorkspaceState state{.terminalWorkspaces = {layout}, .activeTerminalWorkspaceId = "workspace"};
    QVERIFY(validWorkspaceState(state));
    state.activeTerminalWorkspaceId = "missing";
    QVERIFY(!validWorkspaceState(state));
}

QTEST_GUILESS_MAIN(WorkbenchDomainTests)

#include "workbench_domain_tests.moc"
