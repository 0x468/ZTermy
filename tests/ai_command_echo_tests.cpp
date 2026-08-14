#include "domain/ai/AiCommandEcho.h"

#include <QtTest/QTest>

namespace
{

using ztermy::ai::AiCommandEcho;

class AiCommandEchoTests final : public QObject
{
    Q_OBJECT

private slots:
    void producesShellCommentMarkers();
    void collapsesMultilineCommandsToSingleLine();
    void neverBreaksOutOfTheComment();
};

void AiCommandEchoTests::producesShellCommentMarkers()
{
    QCOMPARE(QString::fromStdString(AiCommandEcho::markerLine("git status", "pwsh")),
             QStringLiteral("# [ztermy agent] $ git status"));
    QCOMPARE(QString::fromStdString(AiCommandEcho::markerLine("ls -la", "bash")),
             QStringLiteral("# [ztermy agent] $ ls -la"));
    QCOMPARE(QString::fromStdString(AiCommandEcho::markerLine("dir", "cmd")),
             QStringLiteral("rem [ztermy agent] $ dir"));
    // Unknown shells default to the POSIX-compatible comment form.
    QCOMPARE(QString::fromStdString(AiCommandEcho::markerLine("pwd", "")), QStringLiteral("# [ztermy agent] $ pwd"));
}

void AiCommandEchoTests::collapsesMultilineCommandsToSingleLine()
{
    const std::string marker = AiCommandEcho::markerLine("Get-ChildItem\n    -Path C:\\temp", "pwsh");
    QVERIFY(marker.find('\n') == std::string::npos);
    QVERIFY(marker.find('\r') == std::string::npos);
    QCOMPARE(QString::fromStdString(marker), QStringLiteral("# [ztermy agent] $ Get-ChildItem -Path C:\\temp"));
}

void AiCommandEchoTests::neverBreaksOutOfTheComment()
{
    // A command containing a comment terminator must not be able to escape
    // the marker line into an executable statement.
    const std::string marker = AiCommandEcho::markerLine("echo hi\nrm -rf /", "bash");
    QVERIFY(marker.find('\n') == std::string::npos);
    QCOMPARE(QString::fromStdString(marker), QStringLiteral("# [ztermy agent] $ echo hi rm -rf /"));
}

} // namespace

QTEST_GUILESS_MAIN(AiCommandEchoTests)

#include "ai_command_echo_tests.moc"
