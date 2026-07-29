#include "application/terminal/LocalTerminalSession.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <QTest>

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

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
    void measuresInteractiveInputQueueLatency();
};

void LocalTerminalSessionTests::runsPowerShellAndStopsPromptly()
{
    try
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
    catch (const std::exception &exception)
    {
        QFAIL(exception.what());
    }
}

void LocalTerminalSessionTests::measuresInteractiveInputQueueLatency()
{
    if (qEnvironmentVariableIntValue("ZTERMY_RUN_LOCAL_LATENCY_GATE") != 1)
    {
        QSKIP("Set ZTERMY_RUN_LOCAL_LATENCY_GATE=1 to run the local input latency gate");
    }

    try
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

        constexpr std::uint64_t sampleCount = 120;
        constexpr int simulatedKeyIntervalMilliseconds = 5;
        for (std::uint64_t sample = 0; sample < sampleCount; ++sample)
        {
            session.queueInput(QByteArrayLiteral(" "));
            QTest::qWait(simulatedKeyIntervalMilliseconds);
        }

        QTRY_VERIFY_WITH_TIMEOUT(session.inputQueueLatencySummary().count >= sampleCount, 5000);
        const ztermy::diagnostics::LatencySummary latency = session.inputQueueLatencySummary();
        QVERIFY2(latency.p95UpperBoundMicroseconds <= 16'000,
                 qPrintable(QStringLiteral("Local input queue P95 was %1 us across %2 samples")
                                .arg(latency.p95UpperBoundMicroseconds)
                                .arg(latency.count)));
        qInfo().noquote() << "Local input latency gate:"
                          << "samples=" << latency.count << "p50Us=" << latency.p50UpperBoundMicroseconds
                          << "p95Us=" << latency.p95UpperBoundMicroseconds
                          << "p99Us=" << latency.p99UpperBoundMicroseconds << "maxUs=" << latency.maxMicroseconds;

        session.stop();
    }
    catch (const std::exception &exception)
    {
        QFAIL(exception.what());
    }
}

} // namespace

QTEST_GUILESS_MAIN(LocalTerminalSessionTests)

#include "local_terminal_session_tests.moc"
