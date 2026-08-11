#include "domain/ai/AiProviderRetryPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ztermy::ai
{
namespace
{

[[nodiscard]] bool supportsAutomaticRetry(const AiProviderErrorCode code) noexcept
{
    switch (code)
    {
        case AiProviderErrorCode::network:
        case AiProviderErrorCode::rateLimited:
        case AiProviderErrorCode::server:
        case AiProviderErrorCode::protocol:
            return true;
        case AiProviderErrorCode::authentication:
        case AiProviderErrorCode::quotaExceeded:
        case AiProviderErrorCode::invalidRequest:
        case AiProviderErrorCode::cancelled:
            return false;
    }
    return false;
}

[[nodiscard]] std::uint64_t saturatedDouble(const std::uint64_t value) noexcept
{
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    return value > maximum / 2 ? maximum : value * 2;
}

} // namespace

AiProviderRetryPolicy::AiProviderRetryPolicy(AiProviderRetryLimits limits) : m_limits(limits)
{
    m_limits.baseDelayMilliseconds = std::max<std::uint64_t>(1, m_limits.baseDelayMilliseconds);
    m_limits.maxDelayMilliseconds = std::max(m_limits.baseDelayMilliseconds, m_limits.maxDelayMilliseconds);
    m_limits.jitterPercent = std::min<std::uint32_t>(m_limits.jitterPercent, 100);
}

const AiProviderRetryLimits &AiProviderRetryPolicy::limits() const noexcept
{
    return m_limits;
}

AiProviderRetryDecision AiProviderRetryPolicy::decide(const AiProviderError &error,
                                                      const std::uint32_t completedRetries,
                                                      const double jitterSample) const noexcept
{
    if (!error.retryable || !supportsAutomaticRetry(error.code) || completedRetries >= m_limits.maxRetries)
    {
        return {};
    }

    if (error.retryAfterMilliseconds.has_value())
    {
        return {.retry = true,
                .delayMilliseconds =
                    std::clamp(*error.retryAfterMilliseconds, std::uint64_t{1}, m_limits.maxDelayMilliseconds)};
    }

    auto delay = m_limits.baseDelayMilliseconds;
    for (std::uint32_t retry = 0; retry < completedRetries; ++retry)
    {
        delay = saturatedDouble(delay);
    }
    delay = std::min(delay, m_limits.maxDelayMilliseconds);

    const auto sample = std::clamp(jitterSample, 0.0, 1.0);
    const auto jitter = static_cast<double>(m_limits.jitterPercent) / 100.0;
    const auto multiplier = (1.0 - jitter) + ((2.0 * jitter) * sample);
    const auto jittered = static_cast<std::uint64_t>(std::llround(static_cast<double>(delay) * multiplier));
    return {.retry = true, .delayMilliseconds = std::clamp(jittered, std::uint64_t{1}, m_limits.maxDelayMilliseconds)};
}

} // namespace ztermy::ai
