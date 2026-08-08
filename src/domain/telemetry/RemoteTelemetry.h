#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::telemetry
{

inline constexpr std::size_t maximumCoreCount = 32;
inline constexpr std::size_t maximumDiskCount = 32;
inline constexpr std::size_t maximumInterfaceCount = 32;
inline constexpr std::size_t maximumProcessCount = 8;
inline constexpr std::size_t maximumHistorySamples = 24;
inline constexpr std::size_t maximumProtocolBytes = 65'536;

struct CpuCounter final
{
    std::uint64_t total = 0;
    std::uint64_t idle = 0;
};

struct MemoryCounters final
{
    std::uint64_t totalKiB = 0;
    std::uint64_t availableKiB = 0;
    std::uint64_t freeKiB = 0;
    std::uint64_t buffersKiB = 0;
    std::uint64_t cachedKiB = 0;
    std::uint64_t reclaimableKiB = 0;
    std::uint64_t swapTotalKiB = 0;
    std::uint64_t swapFreeKiB = 0;
};

struct DiskCounters final
{
    std::string mountPoint;
    std::uint64_t usedKiB = 0;
    std::uint64_t totalKiB = 0;
    double usedPercent = 0.0;
};

struct NetworkCounters final
{
    std::string name;
    std::uint64_t receivedBytes = 0;
    std::uint64_t transmittedBytes = 0;
};

struct ProcessMemory final
{
    std::uint32_t pid = 0;
    double memoryPercent = 0.0;
    std::string command;
};

struct RawSample final
{
    std::string osName;
    CpuCounter cpu;
    std::uint32_t cpuCoreCount = 0;
    std::vector<CpuCounter> cores;
    MemoryCounters memory;
    std::vector<DiskCounters> disks;
    std::vector<NetworkCounters> interfaces;
    std::vector<ProcessMemory> processes;
};

enum class ParseError : std::uint8_t
{
    empty,
    tooLarge,
    invalidHeader,
    malformed,
    unsupported,
};

struct NetworkRate final
{
    std::string name;
    std::uint64_t receivedBytesPerSecond = 0;
    std::uint64_t transmittedBytesPerSecond = 0;
};

struct Sample final
{
    std::string osName;
    std::uint32_t cpuCoreCount = 0;
    std::optional<double> cpuPercent;
    std::vector<double> corePercents;
    MemoryCounters memory;
    std::uint64_t memoryUsedKiB = 0;
    std::vector<DiskCounters> disks;
    std::vector<NetworkRate> interfaces;
    std::uint64_t receivedBytesPerSecond = 0;
    std::uint64_t transmittedBytesPerSecond = 0;
    std::vector<ProcessMemory> processes;
    std::uint32_t sshProbeLatencyMs = 0;
    std::chrono::steady_clock::time_point capturedAt{};
};

[[nodiscard]] std::expected<RawSample, ParseError> parseRemoteTelemetry(std::string_view source);
[[nodiscard]] std::string_view linuxRemoteTelemetryCommand(bool includeDetails) noexcept;

class Accumulator final
{
public:
    [[nodiscard]] Sample consume(RawSample raw, std::uint32_t latencyMs,
                                 std::chrono::steady_clock::time_point capturedAt);
    [[nodiscard]] const std::deque<Sample> &history() const noexcept;
    void reset() noexcept;

private:
    std::optional<RawSample> m_previous;
    std::chrono::steady_clock::time_point m_previousAt{};
    std::optional<std::vector<CpuCounter>> m_previousCores;
    std::chrono::steady_clock::time_point m_previousCoresAt{};
    std::vector<double> m_latestCorePercents;
    std::vector<DiskCounters> m_latestDisks;
    std::vector<ProcessMemory> m_latestProcesses;
    std::deque<Sample> m_history;
};

class Scheduler final
{
public:
    static constexpr auto sampleInterval = std::chrono::seconds{5};
    static constexpr auto detailsInterval = std::chrono::seconds{30};
    static constexpr auto probeTimeout = std::chrono::seconds{3};
    static constexpr unsigned maximumConsecutiveFailures = 3;

    void setVisible(bool visible, std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool due(std::chrono::steady_clock::time_point now) const noexcept;
    [[nodiscard]] bool detailsDue(std::chrono::steady_clock::time_point now) const noexcept;
    void markStarted(std::chrono::steady_clock::time_point now) noexcept;
    void markSucceeded(std::chrono::steady_clock::time_point now, bool includedDetails) noexcept;
    void markFailed(std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] bool suspended() const noexcept;
    [[nodiscard]] unsigned consecutiveFailures() const noexcept;
    void reset(std::chrono::steady_clock::time_point now) noexcept;

private:
    bool m_visible = false;
    bool m_inFlight = false;
    unsigned m_failures = 0;
    unsigned m_detailSamples = 0;
    std::chrono::steady_clock::time_point m_nextSample{};
    std::chrono::steady_clock::time_point m_nextDetails{};
};

} // namespace ztermy::telemetry
