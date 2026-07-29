#include "application/terminal/LocalTerminalSession.h"

#include <QElapsedTimer>
#include <QTest>

#include <string>

namespace
{

[[nodiscard]] std::u32string snapshotText(const ztermy::terminal::TerminalSnapshot &snapshot)
{
    std::u32string text;
    for (quint16 row = 0; row < snapshot.rows; ++row)
    {
        for (quint16 column = 0; column < snapshot.columns; ++column)
        {
            const auto &grapheme = snapshot.cell(column, row).grapheme;
            text.append(grapheme);
        }
        text.push_back(U'\n');
    }
    return text;
}

class LocalTerminalSessionTests final : public QObject
{
    Q_OBJECT

private slots:
    void runsPowerShellAndStopsPromptly();
};

void LocalTerminalSessionTests::runsPowerShellAndStopsPromptly()
{
    ztermy::terminal::LocalTerminalSession session;
    ztermy::terminal::TerminalSnapshotPtr latestSnapshot;
    connect(&session, &ztermy::terminal::LocalTerminalSession::snapshotReady, this,
            [&latestSnapshot](ztermy::terminal::TerminalSnapshotPtr snapshot) {
                latestSnapshot = std::move(snapshot);
            });

    const std::error_code startError = session.start({.columns = 80, .rows = 24});
    if (startError)
    {
        QFAIL(startError.message().c_str());
    }

    QTRY_VERIFY_WITH_TIMEOUT(latestSnapshot && latestSnapshot->cursor.visible, 5000);

    session.queueInput(QByteArrayLiteral("Write-Output ZTERMY_SESSION_OK\r"));

    QTRY_VERIFY_WITH_TIMEOUT(
        latestSnapshot && snapshotText(*latestSnapshot).find(U"ZTERMY_SESSION_OK") != std::u32string::npos, 5000);

    QElapsedTimer timer;
    timer.start();
    session.stop();
    QVERIFY2(timer.elapsed() < 2000, "Stopping the local terminal session took too long");
}

} // namespace

QTEST_GUILESS_MAIN(LocalTerminalSessionTests)

#include "local_terminal_session_tests.moc"
