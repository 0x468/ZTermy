#include "core/diagnostics/LatencyHistogram.h"

#include <QTest>

#include <chrono>

using namespace std::chrono_literals;

class LatencyHistogramTests final : public QObject
{
    Q_OBJECT

private slots:
    void emptySummaryIsZero();
    void reportsBoundedPercentilesAndActualMaximum();
    void resetClearsAllSamples();
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

QTEST_GUILESS_MAIN(LatencyHistogramTests)

#include "latency_histogram_tests.moc"
