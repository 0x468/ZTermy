#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ztermy::diagnostics
{

struct LatencySummary final
{
    std::uint64_t count = 0;
    std::uint64_t p50UpperBoundMicroseconds = 0;
    std::uint64_t p95UpperBoundMicroseconds = 0;
    std::uint64_t p99UpperBoundMicroseconds = 0;
    std::uint64_t maxMicroseconds = 0;

    [[nodiscard]] friend bool operator==(const LatencySummary &, const LatencySummary &) = default;
};

class LatencyHistogram final
{
public:
    void record(std::chrono::steady_clock::duration latency) noexcept;
    void reset() noexcept;
    [[nodiscard]] LatencySummary summary() const noexcept;

private:
    static constexpr std::array<std::uint64_t, 16> bucketUpperBoundsMicroseconds{
        50U,     100U,    250U,    500U,     1'000U,   2'000U,   4'000U,     8'000U,
        16'000U, 32'000U, 64'000U, 128'000U, 256'000U, 512'000U, 1'000'000U, UINT64_MAX,
    };

    [[nodiscard]] static std::uint64_t
    percentileUpperBound(const std::array<std::uint64_t, bucketUpperBoundsMicroseconds.size()> &buckets,
                         std::uint64_t count, std::uint64_t percentile) noexcept;

    std::array<std::atomic_uint64_t, bucketUpperBoundsMicroseconds.size()> m_buckets{};
    std::atomic_uint64_t m_maxMicroseconds = 0;
};

} // namespace ztermy::diagnostics
