#include "core/diagnostics/LatencyHistogram.h"

#include <QTest>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

class LatencyHistogramTests final : public QObject
{
    Q_OBJECT

private slots:
    void emptySummaryIsZero();
    void reportsBoundedPercentilesAndActualMaximum();
    void resetClearsAllSamples();
    void takeSummaryStartsANewWindow();
    void concurrentDrainsDoNotLoseSamples();
};

void LatencyHistogramTests::emptySummaryIsZero()
{
    ztermy::diagnostics::LatencyHistogram histogram;
    QCOMPARE(histogram.summary(), ztermy::diagnostics::LatencySummary{});
}

void LatencyHistogramTests::reportsBoundedPercentilesAndActualMaximum()
{
    ztermy::diagnostics::LatencyHistogram histogram;
    for (int sample = 1; sample <= 100; ++sample)
    {
        histogram.record(std::chrono::microseconds{sample * 100});
    }

    const auto summary = histogram.summary();
    QCOMPARE(summary.count, 100U);
    QCOMPARE(summary.p50UpperBoundMicroseconds, 8'000U);
    QCOMPARE(summary.p95UpperBoundMicroseconds, 16'000U);
    QCOMPARE(summary.p99UpperBoundMicroseconds, 16'000U);
    QCOMPARE(summary.maxMicroseconds, 10'000U);
}

void LatencyHistogramTests::resetClearsAllSamples()
{
    ztermy::diagnostics::LatencyHistogram histogram;
    histogram.record(4ms);
    histogram.reset();
    QCOMPARE(histogram.summary(), ztermy::diagnostics::LatencySummary{});
}

void LatencyHistogramTests::takeSummaryStartsANewWindow()
{
    ztermy::diagnostics::LatencyHistogram histogram;
    histogram.record(4ms);
    histogram.record(8ms);

    const auto firstWindow = histogram.takeSummary();
    QCOMPARE(firstWindow.count, 2U);
    QCOMPARE(firstWindow.p95UpperBoundMicroseconds, 8'000U);
    QCOMPARE(firstWindow.maxMicroseconds, 8'000U);
    QCOMPARE(histogram.summary(), ztermy::diagnostics::LatencySummary{});

    histogram.record(250us);
    const auto secondWindow = histogram.takeSummary();
    QCOMPARE(secondWindow.count, 1U);
    QCOMPARE(secondWindow.p95UpperBoundMicroseconds, 250U);
    QCOMPARE(secondWindow.maxMicroseconds, 250U);
}

void LatencyHistogramTests::concurrentDrainsDoNotLoseSamples()
{
    constexpr std::uint64_t sampleCount = 100'000;
    ztermy::diagnostics::LatencyHistogram histogram;
    std::atomic_bool finished = false;
    std::jthread producer([&histogram, &finished] {
        for (std::uint64_t sample = 0; sample < sampleCount; ++sample)
        {
            histogram.record(250us);
        }
        finished.store(true);
    });

    std::uint64_t observedSamples = 0;
    while (!finished.load())
    {
        observedSamples += histogram.takeSummary().count;
        std::this_thread::yield();
    }
    producer.join();
    observedSamples += histogram.takeSummary().count;

    QCOMPARE(observedSamples, sampleCount);
}

QTEST_GUILESS_MAIN(LatencyHistogramTests)

#include "latency_histogram_tests.moc"
