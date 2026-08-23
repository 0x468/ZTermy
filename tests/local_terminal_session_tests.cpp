#include "application/terminal/LocalTerminalSession.h"
#include "application/terminal/PowerShellShellIntegration.h"
#include "domain/terminal/SemanticTerminalObserver.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QTimer>

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

class MemoryOutputSink final : public ztermy::terminal::TerminalOutputSink
{
public:
    void append(const std::span<const std::byte> bytes) noexcept override
    {
        std::scoped_lock lock(m_mutex);
        m_bytes.append(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    }

    [[nodiscard]] bool contains(const QByteArray &needle) const
    {
        std::scoped_lock lock(m_mutex);
        return m_bytes.contains(needle);
    }

private:
    mutable std::mutex m_mutex;
    QByteArray m_bytes;
};

class SemanticOutputSink final : public ztermy::terminal::TerminalOutputSink
{
public:
    explicit SemanticOutputSink(std::shared_ptr<ztermy::terminal::SemanticTerminalObserver> observer)
        : m_observer(std::move(observer))
    {
    }

    void append(const std::span<const std::byte> bytes) noexcept override { m_observer->append(bytes); }

private:
    std::shared_ptr<ztermy::terminal::SemanticTerminalObserver> m_observer;
};

class LocalTerminalSessionTests final : public QObject
{
    Q_OBJECT

private slots:
    void buildsEphemeralPowerShellIntegrationCommand();
    void capturesRichPowerShellCommandLifecycle();
    void runsPowerShellAndStopsPromptly();
    void restoresPromptAfterHelixAlternateScreen();
    void measuresInteractiveInputQueueLatency();
    void processesLargeOutputWithoutStarvingEventLoop();
    void survivesSustainedInteractionWithoutLatencyGrowth();
};

void LocalTerminalSessionTests::buildsEphemeralPowerShellIntegrationCommand()
{
    constexpr std::string_view nonce = "12345678-abcd-4321-abcd-1234567890ab";
    const auto command = ztermy::terminal::powerShellLaunchCommand(L"pwsh.exe", nonce);
    QVERIFY(command.has_value());
    const QString commandText = QString::fromStdWString(*command);
    const QString marker = QStringLiteral(" -EncodedCommand ");
    const qsizetype markerPosition = commandText.indexOf(marker);
    QVERIFY(markerPosition > 0);

    const QByteArray encoded = commandText.sliced(markerPosition + marker.size()).toLatin1();
    const QByteArray utf16 = QByteArray::fromBase64(encoded);
    QVERIFY(!utf16.isEmpty());
    QCOMPARE(utf16.size() % static_cast<qsizetype>(sizeof(char16_t)), qsizetype{0});
    const QString script = QString::fromUtf16(reinterpret_cast<const char16_t *>(utf16.constData()),
                                              utf16.size() / static_cast<qsizetype>(sizeof(char16_t)));
    QVERIFY(script.contains(QString::fromLatin1(nonce)));
    QVERIFY(script.contains(QStringLiteral("Set-PSReadLineKeyHandler -Chord Enter")));
    QVERIFY(script.contains(QStringLiteral("`e]633;E;")));
    QVERIFY(script.contains(QStringLiteral("HasRichCommandDetection=True")));
    QVERIFY(!script.contains(QStringLiteral("Set-Content")));
    QVERIFY(!script.contains(QStringLiteral("$PROFILE")));

    QVERIFY(!ztermy::terminal::powerShellLaunchCommand(L"pwsh.exe", "unsafe';command").has_value());
}

void LocalTerminalSessionTests::capturesRichPowerShellCommandLifecycle()
{
    constexpr std::string_view nonce = "12345678-abcd-4321-abcd-1234567890ab";
    auto observer = std::make_shared<ztermy::terminal::SemanticTerminalObserver>(
        ztermy::terminal::CommandBlockSessionContext{
            .sessionId = "local-test",
            .host = "localhost",
            .shell = "pwsh",
        },
        std::string(nonce));
    const auto sink = std::make_shared<SemanticOutputSink>(observer);

    ztermy::terminal::LocalTerminalSession session;
    session.setOutputSink(sink);
    session.setShellIntegrationNonce(std::string(nonce));
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
    session.queueInput(QStringLiteral("Write-Output '终端'\r").toUtf8());
    QTRY_VERIFY_WITH_TIMEOUT(!observer->snapshot().commandBlocks.empty()
                                 && observer->snapshot().commandBlocks.back().state
                                        == ztermy::terminal::CommandBlockState::finished,
                             5000);

    const auto semantic = observer->snapshot();
    const auto &block = semantic.commandBlocks.back();
    QCOMPARE(block.command, std::string("Write-Output '终端'"));
    QCOMPARE(block.capability, ztermy::terminal::TerminalSemanticCapability::rich);
    QCOMPARE(block.exitStatus, std::optional<int>{0});
    session.stop();
}

void LocalTerminalSessionTests::runsPowerShellAndStopsPromptly()
{
    try
    {
        ztermy::terminal::LocalTerminalSession session;
        const auto output = std::make_shared<MemoryOutputSink>();
        session.setOutputSink(output);
        QSignalSpy selectedTextSpy(&session, &ztermy::terminal::LocalTerminalSession::selectedTextReady);
        qsizetype snapshotCount = 0;
        ztermy::terminal::TerminalSnapshotPtr latestSnapshot;
        connect(&session, &ztermy::terminal::LocalTerminalSession::snapshotReady, this,
                [&latestSnapshot, &snapshotCount](ztermy::terminal::TerminalSnapshotPtr snapshot) {
                    latestSnapshot = std::move(snapshot);
                    ++snapshotCount;
                });

        const std::error_code startError = session.start({.columns = 80, .rows = 24});
        if (startError)
        {
            QFAIL(startError.message().c_str());
        }

        QTRY_VERIFY_WITH_TIMEOUT(latestSnapshot && latestSnapshot->cursor.visible, 5000);

        session.requestSelectedText();
        QTRY_COMPARE_WITH_TIMEOUT(selectedTextSpy.size(), 1, 2000);
        QCOMPARE(selectedTextSpy.constFirst().constFirst().toString(), QString{});

        session.queueInput(QByteArrayLiteral("Write-Output ZTERMY_SESSION_OK\r"));

        QTRY_VERIFY_WITH_TIMEOUT(
            latestSnapshot && snapshotText(*latestSnapshot).find(U"ZTERMY_SESSION_OK") != std::u32string::npos, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(output->contains(QByteArrayLiteral("ZTERMY_SESSION_OK")), 5000);

        QElapsedTimer timer;
        timer.start();
        session.stop();
        QVERIFY2(timer.elapsed() < 2000, "Stopping the local terminal session took too long");
        const qsizetype snapshotsAtStop = snapshotCount;
        QTest::qWait(32);
        QCOMPARE(snapshotCount, snapshotsAtStop);
    }
    catch (const std::exception &exception)
    {
        QFAIL(exception.what());
    }
}

void LocalTerminalSessionTests::restoresPromptAfterHelixAlternateScreen()
{
    if (qEnvironmentVariableIntValue("ZTERMY_RUN_HELIX_GATE") != 1)
    {
        QSKIP("Set ZTERMY_RUN_HELIX_GATE=1 to run the installed Helix alternate-screen gate");
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

        const auto lastContentRow = [](const ztermy::terminal::TerminalSnapshot &snapshot) {
            std::uint16_t result = 0;
            for (std::uint16_t row = 0; row < snapshot.rows; ++row)
            {
                const bool hasContent = std::ranges::any_of(
                    snapshot.cells.begin() + (static_cast<std::ptrdiff_t>(row) * snapshot.columns),
                    snapshot.cells.begin() + (static_cast<std::ptrdiff_t>(row + 1) * snapshot.columns),
                    [](const ztermy::terminal::TerminalCell &cell) {
                        return !cell.grapheme.empty();
                    });
                if (hasContent)
                {
                    result = row;
                }
            }
            return result;
        };

        for (int iteration = 0; iteration < 5; ++iteration)
        {
            session.queueInput(QByteArrayLiteral("hx\r"));
            QElapsedTimer alternateTimer;
            alternateTimer.start();
            while (alternateTimer.elapsed() < 5000
                   && (!latestSnapshot || snapshotText(*latestSnapshot).find(U"PS ") != std::u32string::npos))
            {
                QTest::qWait(20);
            }
            if (iteration == 0
                && (!latestSnapshot || snapshotText(*latestSnapshot).find(U"PS ") != std::u32string::npos))
            {
                session.stop();
                QSKIP("The installed hx command did not enter an alternate screen in this test environment");
            }
            QVERIFY(latestSnapshot && snapshotText(*latestSnapshot).find(U"PS ") == std::u32string::npos);
            session.queueInput(QByteArrayLiteral(":q\r"));
            QTRY_VERIFY_WITH_TIMEOUT(latestSnapshot && latestSnapshot->cursor.visible
                                         && snapshotText(*latestSnapshot).find(U"PS ") != std::u32string::npos,
                                     5000);
            QCOMPARE(latestSnapshot->cursor.row, lastContentRow(*latestSnapshot));
        }
        session.stop();
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

void LocalTerminalSessionTests::processesLargeOutputWithoutStarvingEventLoop()
{
    if (qEnvironmentVariableIntValue("ZTERMY_RUN_LOCAL_OUTPUT_GATE") != 1)
    {
        QSKIP("Set ZTERMY_RUN_LOCAL_OUTPUT_GATE=1 to run the local large-output gate");
    }

    try
    {
        ztermy::terminal::LocalTerminalSession session;
        ztermy::terminal::TerminalSnapshotPtr latestSnapshot;
        std::uint64_t deliveredSnapshots = 0;
        connect(&session, &ztermy::terminal::LocalTerminalSession::snapshotReady, this,
                [&latestSnapshot, &deliveredSnapshots](ztermy::terminal::TerminalSnapshotPtr snapshot) {
                    latestSnapshot = std::move(snapshot);
                    ++deliveredSnapshots;
                });

        const std::error_code startError = session.start({.columns = 100, .rows = 30});
        if (startError)
        {
            QFAIL(startError.message().c_str());
        }
        QTRY_VERIFY_WITH_TIMEOUT(latestSnapshot && latestSnapshot->cursor.visible, 5000);

        std::uint64_t eventLoopTicks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&eventLoopTicks] {
            ++eventLoopTicks;
        });
        heartbeat.start();

        QElapsedTimer elapsed;
        elapsed.start();
        session.queueInput(QByteArrayLiteral(
            "1..20000 | ForEach-Object { \"ztermy output line $_\" }; Write-Output ZTERMY_OUTPUT_GATE_DONE\r"));
        QTRY_VERIFY_WITH_TIMEOUT(
            latestSnapshot && snapshotText(*latestSnapshot).find(U"ZTERMY_OUTPUT_GATE_DONE") != std::u32string::npos,
            20s);
        const qint64 completionMilliseconds = elapsed.elapsed();
        heartbeat.stop();

        QVERIFY2(eventLoopTicks >= 5, "The Qt event loop was starved during large terminal output");
        QVERIFY2(deliveredSnapshots >= 5, "Large terminal output did not deliver progressive snapshots");
        const auto maximumUsefulDeliveries =
            static_cast<std::uint64_t>((std::max<qint64>)(completionMilliseconds / 4, 10));
        QVERIFY2(deliveredSnapshots <= maximumUsefulDeliveries,
                 "Large terminal output flooded the GUI with undisplayable snapshots");

        elapsed.restart();
        session.stop();
        const qint64 stopMilliseconds = elapsed.elapsed();
        QVERIFY2(stopMilliseconds < 2000, "Stopping after large terminal output took too long");

        qInfo().noquote() << "Local large-output gate:"
                          << "completionMs=" << completionMilliseconds << "eventLoopTicks=" << eventLoopTicks
                          << "deliveredSnapshots=" << deliveredSnapshots << "stopMs=" << stopMilliseconds;
    }
    catch (const std::exception &exception)
    {
        QFAIL(exception.what());
    }
}

void LocalTerminalSessionTests::survivesSustainedInteractionWithoutLatencyGrowth()
{
    if (qEnvironmentVariableIntValue("ZTERMY_RUN_LOCAL_SOAK_GATE") != 1)
    {
        QSKIP("Set ZTERMY_RUN_LOCAL_SOAK_GATE=1 to run the sustained local interaction gate");
    }

    bool durationIsValid = false;
    const int configuredDuration = qEnvironmentVariableIntValue("ZTERMY_LOCAL_SOAK_SECONDS", &durationIsValid);
    const int durationSeconds = durationIsValid ? configuredDuration : 1'800;
    QVERIFY2(durationSeconds >= 10 && durationSeconds <= 28'800,
             "ZTERMY_LOCAL_SOAK_SECONDS must be between 10 and 28800");
    const int windowSeconds = (std::max)(1, (std::min)(60, durationSeconds / 6));

    try
    {
        ztermy::terminal::LocalTerminalSession session;
        ztermy::terminal::TerminalSnapshotPtr latestSnapshot;
        std::uint64_t deliveredSnapshots = 0;
        connect(&session, &ztermy::terminal::LocalTerminalSession::snapshotReady, this,
                [&latestSnapshot, &deliveredSnapshots](ztermy::terminal::TerminalSnapshotPtr snapshot) {
                    latestSnapshot = std::move(snapshot);
                    ++deliveredSnapshots;
                });

        const std::error_code startError = session.start({.columns = 100, .rows = 30});
        if (startError)
        {
            QFAIL(startError.message().c_str());
        }
        QTRY_VERIFY_WITH_TIMEOUT(latestSnapshot && latestSnapshot->cursor.visible, 5000);

        DWORD baselineHandles = 0;
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &baselineHandles));

        std::uint64_t eventLoopTicks = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&eventLoopTicks] {
            ++eventLoopTicks;
        });
        heartbeat.start();

        std::vector<ztermy::diagnostics::LatencySummary> windows;
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 nextWindowMilliseconds = static_cast<qint64>(windowSeconds) * 1'000;
        std::uint64_t interaction = 0;
        while (elapsed.elapsed() < static_cast<qint64>(durationSeconds) * 1'000)
        {
            session.queueInput(QByteArrayLiteral("x\b"));
            ++interaction;

            if ((interaction % 50U) == 0U)
            {
                const bool compact = ((interaction / 50U) % 2U) == 0U;
                session.requestResize(compact ? 80 : 120, compact ? 24 : 36, 9, 18);
            }
            if ((interaction % 100U) == 0U)
            {
                session.requestScroll(3);
                session.requestScroll(-3);
                session.requestSelection(0, 0, 8, 0, false);
                session.clearSelection();
                session.search(QStringLiteral("ztermy"), false, false);
                session.clearSearch();
            }

            QTest::qWait(20);
            if (elapsed.elapsed() >= nextWindowMilliseconds)
            {
                windows.push_back(session.takeInputQueueLatencySummary());
                const auto &window = windows.back();
                QVERIFY2(std::cmp_greater_equal(window.count, static_cast<std::uint64_t>(windowSeconds * 20)),
                         qPrintable(QStringLiteral("Soak window %1 contained only %2 latency samples")
                                        .arg(windows.size())
                                        .arg(window.count)));
                QVERIFY2(window.p95UpperBoundMicroseconds <= 16'000U,
                         qPrintable(QStringLiteral("Soak window %1 input queue P95 was %2 us")
                                        .arg(windows.size())
                                        .arg(window.p95UpperBoundMicroseconds)));
                qInfo().noquote() << "Local interaction soak window:"
                                  << "index=" << windows.size() << "elapsedMs=" << elapsed.elapsed()
                                  << "samples=" << window.count << "p50Us=" << window.p50UpperBoundMicroseconds
                                  << "p95Us=" << window.p95UpperBoundMicroseconds
                                  << "p99Us=" << window.p99UpperBoundMicroseconds;
                nextWindowMilliseconds += static_cast<qint64>(windowSeconds) * 1'000;
            }
        }

        const auto trailingWindow = session.takeInputQueueLatencySummary();
        if (trailingWindow.count > 0U)
        {
            windows.push_back(trailingWindow);
        }
        heartbeat.stop();

        QVERIFY2(windows.size() >= 6U, "The soak gate did not produce enough latency windows");
        const std::size_t third = windows.size() / 3U;
        const auto averageP95 = [&windows](const std::size_t begin, const std::size_t end) {
            const std::uint64_t total =
                std::accumulate(windows.begin() + static_cast<std::ptrdiff_t>(begin),
                                windows.begin() + static_cast<std::ptrdiff_t>(end), std::uint64_t{0},
                                [](const std::uint64_t sum, const ztermy::diagnostics::LatencySummary &window) {
                                    return sum + window.p95UpperBoundMicroseconds;
                                });
            return total / static_cast<std::uint64_t>(end - begin);
        };
        const std::uint64_t initialP95Average = averageP95(0, third);
        const std::uint64_t finalP95Average = averageP95(windows.size() - third, windows.size());
        const std::uint64_t permittedFinalP95 = (std::max)(std::uint64_t{1'000}, initialP95Average * std::uint64_t{2});
        QVERIFY2(finalP95Average <= permittedFinalP95,
                 qPrintable(QStringLiteral("Input latency grew from an initial P95 average of %1 us "
                                           "to a final average of %2 us")
                                .arg(initialP95Average)
                                .arg(finalP95Average)));
        QVERIFY2(std::cmp_greater_equal(eventLoopTicks, static_cast<std::uint64_t>(durationSeconds * 20)),
                 "The Qt event loop heartbeat was starved during sustained interaction");
        QVERIFY2(std::cmp_greater_equal(deliveredSnapshots, static_cast<std::uint64_t>(durationSeconds * 2)),
                 "Sustained interaction did not deliver progressive terminal snapshots");

        QElapsedTimer stopTimer;
        stopTimer.start();
        session.stop();
        const qint64 stopMilliseconds = stopTimer.elapsed();
        QVERIFY2(stopMilliseconds < 2'000, "Stopping after sustained interaction took too long");

        DWORD finalHandles = 0;
        QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &finalHandles));
        QVERIFY2(
            finalHandles <= baselineHandles + 4U,
            qPrintable(QStringLiteral("Process handles grew from %1 to %2").arg(baselineHandles).arg(finalHandles)));

        qInfo().noquote() << "Local interaction soak gate:"
                          << "durationSeconds=" << durationSeconds << "windows=" << windows.size()
                          << "interactions=" << interaction << "eventLoopTicks=" << eventLoopTicks
                          << "deliveredSnapshots=" << deliveredSnapshots << "initialP95AverageUs=" << initialP95Average
                          << "finalP95AverageUs=" << finalP95Average << "baselineHandles=" << baselineHandles
                          << "finalHandles=" << finalHandles << "stopMs=" << stopMilliseconds;
    }
    catch (const std::exception &exception)
    {
        QFAIL(exception.what());
    }
}

} // namespace

QTEST_GUILESS_MAIN(LocalTerminalSessionTests)

#include "local_terminal_session_tests.moc"
