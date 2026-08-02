#include "domain/workbench/ShellHistory.h"

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
};

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

QTEST_GUILESS_MAIN(WorkbenchDomainTests)

#include "workbench_domain_tests.moc"
