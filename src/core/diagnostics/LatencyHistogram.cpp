#include "core/diagnostics/LatencyHistogram.h"

#include <algorithm>
#include <limits>

namespace ztermy::diagnostics
{

namespace
{

[[nodiscard]] std::uint64_t percentileRank(const std::uint64_t count, const std::uint64_t percentile) noexcept
{
    const std::uint64_t whole = (count / 100U) * percentile;
    const std::uint64_t remainder = count % 100U;
    return whole + ((remainder * percentile) + 99U) / 100U;
}

} // namespace

void LatencyHistogram::record(const std::chrono::steady_clock::duration latency) noexcept
{
    const auto nonNegative = (std::max)(latency, std::chrono::steady_clock::duration::zero());
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(nonNegative).count();
    const auto value = static_cast<std::uint64_t>(microseconds);

    const auto bucket = std::ranges::lower_bound(bucketUpperBoundsMicroseconds, value);
    const std::size_t index =
        bucket == bucketUpperBoundsMicroseconds.end()
            ? bucketUpperBoundsMicroseconds.size() - 1U
            : static_cast<std::size_t>(std::distance(bucketUpperBoundsMicroseconds.begin(), bucket));
    m_buckets[index].fetch_add(1U, std::memory_order_relaxed);

    std::uint64_t maximum = m_maxMicroseconds.load(std::memory_order_relaxed);
    while (maximum < value
           && !m_maxMicroseconds.compare_exchange_weak(maximum, value, std::memory_order_relaxed,
                                                       std::memory_order_relaxed))
    {
    }
}

void LatencyHistogram::reset() noexcept
{
    for (auto &bucket : m_buckets)
    {
        bucket.store(0U, std::memory_order_relaxed);
    }
    m_maxMicroseconds.store(0U, std::memory_order_relaxed);
}

LatencySummary LatencyHistogram::summary() const noexcept
{
    std::array<std::uint64_t, bucketUpperBoundsMicroseconds.size()> buckets{};
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < buckets.size(); ++index)
    {
        buckets[index] = m_buckets[index].load(std::memory_order_relaxed);
        count += buckets[index];
    }

    return LatencySummary{
        .count = count,
        .p50UpperBoundMicroseconds = percentileUpperBound(buckets, count, 50U),
        .p95UpperBoundMicroseconds = percentileUpperBound(buckets, count, 95U),
        .p99UpperBoundMicroseconds = percentileUpperBound(buckets, count, 99U),
        .maxMicroseconds = m_maxMicroseconds.load(std::memory_order_relaxed),
    };
}

LatencySummary LatencyHistogram::takeSummary() noexcept
{
    std::array<std::uint64_t, bucketUpperBoundsMicroseconds.size()> buckets{};
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < buckets.size(); ++index)
    {
        buckets[index] = m_buckets[index].exchange(0U, std::memory_order_relaxed);
        count += buckets[index];
    }

    return LatencySummary{
        .count = count,
        .p50UpperBoundMicroseconds = percentileUpperBound(buckets, count, 50U),
        .p95UpperBoundMicroseconds = percentileUpperBound(buckets, count, 95U),
        .p99UpperBoundMicroseconds = percentileUpperBound(buckets, count, 99U),
        .maxMicroseconds = m_maxMicroseconds.exchange(0U, std::memory_order_relaxed),
    };
}

std::uint64_t
LatencyHistogram::percentileUpperBound(const std::array<std::uint64_t, bucketUpperBoundsMicroseconds.size()> &buckets,
                                       const std::uint64_t count, const std::uint64_t percentile) noexcept
{
    if (count == 0)
    {
        return 0;
    }

    const std::uint64_t target = percentileRank(count, percentile);
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < buckets.size(); ++index)
    {
        cumulative += buckets[index];
        if (cumulative >= target)
        {
            return bucketUpperBoundsMicroseconds[index];
        }
    }
    return (std::numeric_limits<std::uint64_t>::max)();
}

} // namespace ztermy::diagnostics
