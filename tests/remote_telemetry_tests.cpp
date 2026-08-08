#include "domain/telemetry/RemoteTelemetry.h"

#include <QTest>

#include <chrono>
#include <string>

using namespace std::chrono_literals;

namespace
{

constexpr std::string_view firstSample = "ZTERMY_TELEMETRY_V1\n"
                                         "OS\tLinux\n"
                                         "CPU\t1000\t400\t2\n"
                                         "CORE\t0\t500\t200\n"
                                         "CORE\t1\t500\t200\n"
                                         "MEM\t8388608\t2097152\t524288\t131072\t1048576\t262144\t1048576\t786432\n"
                                         "DISK\t25\t26214400\t104857600\t/\n"
                                         "NET\teth0\t1000000\t2000000\n"
                                         "PROC\t42\t12.5\tserver\n"
                                         "END\n";

constexpr std::string_view secondSample = "ZTERMY_TELEMETRY_V1\n"
                                          "OS\tLinux\n"
                                          "CPU\t1200\t450\t2\n"
                                          "CORE\t0\t600\t225\n"
                                          "CORE\t1\t600\t225\n"
                                          "MEM\t8388608\t1048576\t262144\t131072\t1048576\t262144\t1048576\t524288\n"
                                          "DISK\t30\t31457280\t104857600\t/\n"
                                          "NET\teth0\t1512000\t2256000\n"
                                          "PROC\t42\t13.0\tserver\n"
                                          "END\n";

class RemoteTelemetryTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesVersionedLinuxSample()
    {
        const auto parsed = ztermy::telemetry::parseRemoteTelemetry(firstSample);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->osName, std::string("Linux"));
        QCOMPARE(parsed->cpuCoreCount, 2U);
        QCOMPARE(parsed->cores.size(), std::size_t{2});
        QCOMPARE(parsed->memory.totalKiB, UINT64_C(8388608));
        QCOMPARE(parsed->disks.front().mountPoint, std::string("/"));
        QCOMPARE(parsed->interfaces.front().name, std::string("eth0"));
        QCOMPARE(parsed->processes.front().pid, 42U);
    }

    void rejectsMalformedAndUnsupportedSamples()
    {
        QCOMPARE(ztermy::telemetry::parseRemoteTelemetry("bad\n").error(),
                 ztermy::telemetry::ParseError::invalidHeader);
        QCOMPARE(ztermy::telemetry::parseRemoteTelemetry("ZTERMY_TELEMETRY_V1\nOS\tDarwin\nEND\n").error(),
                 ztermy::telemetry::ParseError::unsupported);
        QCOMPARE(
            ztermy::telemetry::parseRemoteTelemetry("ZTERMY_TELEMETRY_V1\nOS\tLinux\nCPU\t10\t20\t1\nEND\n").error(),
            ztermy::telemetry::ParseError::malformed);
        const std::string oversized(ztermy::telemetry::maximumProtocolBytes + 1U, 'x');
        QCOMPARE(ztermy::telemetry::parseRemoteTelemetry(oversized).error(), ztermy::telemetry::ParseError::tooLarge);
    }

    void calculatesCounterDeltasAndBoundsHistory()
    {
        ztermy::telemetry::Accumulator accumulator;
        const auto start = std::chrono::steady_clock::time_point{} + 1s;
        auto rawFirst = *ztermy::telemetry::parseRemoteTelemetry(firstSample);
        const auto initial = accumulator.consume(std::move(rawFirst), 8, start);
        QVERIFY(!initial.cpuPercent.has_value());
        QCOMPARE(initial.memoryUsedKiB, UINT64_C(6291456));

        auto rawSecond = *ztermy::telemetry::parseRemoteTelemetry(secondSample);
        const auto current = accumulator.consume(std::move(rawSecond), 11, start + 2s);
        QVERIFY(current.cpuPercent.has_value());
        QCOMPARE(*current.cpuPercent, 75.0);
        QCOMPARE(current.corePercents.size(), std::size_t{2});
        QCOMPARE(current.corePercents.front(), 75.0);
        QCOMPARE(current.receivedBytesPerSecond, UINT64_C(256000));
        QCOMPARE(current.transmittedBytesPerSecond, UINT64_C(128000));
        QCOMPARE(current.sshProbeLatencyMs, 11U);

        for (std::size_t index = 0; index < ztermy::telemetry::maximumHistorySamples + 4U; ++index)
        {
            auto raw = *ztermy::telemetry::parseRemoteTelemetry(secondSample);
            const auto ignored = accumulator.consume(std::move(raw), 1, start + 3s + std::chrono::seconds(index));
            Q_UNUSED(ignored);
        }
        QCOMPARE(accumulator.history().size(), ztermy::telemetry::maximumHistorySamples);
    }

    void schedulerHonorsVisibilityBackoffAndSuspension()
    {
        ztermy::telemetry::Scheduler scheduler;
        const auto start = std::chrono::steady_clock::time_point{} + 1s;
        QVERIFY(!scheduler.due(start));
        scheduler.setVisible(true, start);
        QVERIFY(scheduler.due(start));
        QVERIFY(scheduler.detailsDue(start));
        scheduler.markStarted(start);
        QVERIFY(!scheduler.due(start));
        scheduler.markSucceeded(start, true);
        QVERIFY(!scheduler.due(start + 4s));
        QVERIFY(scheduler.due(start + 5s));
        QVERIFY(scheduler.detailsDue(start + 5s));

        scheduler.markStarted(start + 5s);
        scheduler.markFailed(start + 5s);
        scheduler.markStarted(start + 10s);
        scheduler.markFailed(start + 10s);
        scheduler.markStarted(start + 20s);
        scheduler.markFailed(start + 20s);
        QVERIFY(scheduler.suspended());
        QVERIFY(!scheduler.due(start + 1h));
        scheduler.setVisible(false, start + 1h);
        scheduler.setVisible(true, start + 1h + 1s);
        QVERIFY(!scheduler.suspended());
        QVERIFY(scheduler.due(start + 1h + 1s));
    }

    void commandsAreFixedSingleLineAndBounded()
    {
        for (const bool details : {false, true})
        {
            const std::string_view command = ztermy::telemetry::linuxRemoteTelemetryCommand(details);
            QVERIFY(!command.empty());
            QVERIFY(command.find('\n') == std::string_view::npos);
            QVERIFY(command.find("ZTERMY_TELEMETRY_V1") != std::string_view::npos);
            QVERIFY(command.size() < 8192U);
        }
    }
};

} // namespace

QTEST_GUILESS_MAIN(RemoteTelemetryTests)

#include "remote_telemetry_tests.moc"
