#include "domain/telemetry/RemoteTelemetry.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace ztermy::telemetry
{
namespace
{

constexpr std::string_view protocolHeader = "ZTERMY_TELEMETRY_V1";

[[nodiscard]] std::vector<std::string_view> split(const std::string_view value, const char delimiter)
{
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (begin <= value.size())
    {
        const std::size_t end = value.find(delimiter, begin);
        fields.push_back(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos)
        {
            break;
        }
        begin = end + 1;
    }
    return fields;
}

template <typename Integer>
[[nodiscard]] bool parseInteger(const std::string_view text, Integer &value)
{
    if (text.empty())
    {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] bool parseDouble(const std::string_view text, double &value)
{
    if (text.empty())
    {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && std::isfinite(value);
}

[[nodiscard]] double counterPercent(const CpuCounter &previous, const CpuCounter &current)
{
    if (current.total <= previous.total || current.idle < previous.idle)
    {
        return 0.0;
    }
    const std::uint64_t totalDelta = current.total - previous.total;
    const std::uint64_t idleDelta = std::min(current.idle - previous.idle, totalDelta);
    return std::clamp(100.0 * static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta), 0.0,
                      100.0);
}

[[nodiscard]] std::uint64_t rate(const std::uint64_t previous, const std::uint64_t current, const double seconds)
{
    if (seconds <= 0.0 || current < previous)
    {
        return 0;
    }
    const double value = static_cast<double>(current - previous) / seconds;
    if (!std::isfinite(value) || value <= 0.0)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(std::min(value, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
}

constexpr std::string_view fastCommand =
    "LC_ALL=C; printf 'ZTERMY_TELEMETRY_V1\\n'; os=$(uname -s 2>/dev/null || printf Unknown); "
    "printf 'OS\\t%s\\n' \"$os\"; if [ \"$os\" != Linux ]; then printf 'END\\n'; exit 0; fi; "
    "awk '/^cpu /{t=0;for(i=2;i<=NF;i++)t+=$i;printf \"CPU\\t%.0f\\t%.0f\\t%d\\n\",t,$5,n}' "
    "n=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 0) /proc/stat 2>/dev/null; "
    "awk '/^MemTotal:/{t=$2}/^MemAvailable:/{a=$2}/^MemFree:/{f=$2}/^Buffers:/{b=$2}"
    "/^Cached:/{c=$2}/^SReclaimable:/{r=$2}/^SwapTotal:/{st=$2}/^SwapFree:/{sf=$2}"
    "END{printf \"MEM\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\n\","
    "t,a,f,b,c,r,st,sf}' /proc/meminfo 2>/dev/null; "
    "df -Pk / 2>/dev/null | awk 'NR==2{gsub(/%/,\"\",$5);printf \"DISK\\t%s\\t%s\\t%s\\t%s\\n\","
    "$5,$3,$2,$6}'; "
    "awk 'NR>2&&n<32{gsub(/^[ \\t]+/,\"\");split($0,a,\":\");name=a[1];split(a[2],b);"
    "if(name!=\"lo\"){printf \"NET\\t%s\\t%s\\t%s\\n\",name,b[1],b[9];n++}}' /proc/net/dev 2>/dev/null; "
    "printf 'END\\n'";

constexpr std::string_view detailedCommand =
    "LC_ALL=C; printf 'ZTERMY_TELEMETRY_V1\\n'; os=$(uname -s 2>/dev/null || printf Unknown); "
    "printf 'OS\\t%s\\n' \"$os\"; if [ \"$os\" != Linux ]; then printf 'END\\n'; exit 0; fi; "
    "awk '/^cpu /{t=0;for(i=2;i<=NF;i++)t+=$i;printf \"CPU\\t%.0f\\t%.0f\\t%d\\n\",t,$5,n}"
    "/^cpu[0-9]+ /&&c<32{t=0;for(i=2;i<=NF;i++)t+=$i;printf \"CORE\\t%d\\t%.0f\\t%.0f\\n\",c,t,$5;c++}' "
    "n=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 0) /proc/stat 2>/dev/null; "
    "awk '/^MemTotal:/{t=$2}/^MemAvailable:/{a=$2}/^MemFree:/{f=$2}/^Buffers:/{b=$2}"
    "/^Cached:/{c=$2}/^SReclaimable:/{r=$2}/^SwapTotal:/{st=$2}/^SwapFree:/{sf=$2}"
    "END{printf \"MEM\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\t%.0f\\n\","
    "t,a,f,b,c,r,st,sf}' /proc/meminfo 2>/dev/null; "
    "df -Pk 2>/dev/null | awk 'NR>1&&n<32&&$2~/^[0-9]+$/{gsub(/%/,\"\",$5);"
    "printf \"DISK\\t%s\\t%s\\t%s\\t%s\\n\",$5,$3,$2,$6;n++}'; "
    "awk 'NR>2&&n<32{gsub(/^[ \\t]+/,\"\");split($0,a,\":\");name=a[1];split(a[2],b);"
    "if(name!=\"lo\"){printf \"NET\\t%s\\t%s\\t%s\\n\",name,b[1],b[9];n++}}' /proc/net/dev 2>/dev/null; "
    "ps -eo pid=,pmem=,comm= --sort=-pmem 2>/dev/null | awk 'n<8&&NF>=3{cmd=$3;gsub(/[\\t\\r\\n]/,\" \",cmd);"
    "printf \"PROC\\t%s\\t%s\\t%s\\n\",$1,$2,cmd;n++}'; printf 'END\\n'";

} // namespace

std::expected<RawSample, ParseError> parseRemoteTelemetry(const std::string_view source)
{
    if (source.empty())
    {
        return std::unexpected(ParseError::empty);
    }
    if (source.size() > maximumProtocolBytes)
    {
        return std::unexpected(ParseError::tooLarge);
    }

    RawSample sample;
    bool headerSeen = false;
    bool endSeen = false;
    bool cpuSeen = false;
    bool memorySeen = false;
    std::size_t begin = 0;
    while (begin <= source.size())
    {
        const std::size_t end = source.find('\n', begin);
        std::string_view line =
            source.substr(begin, end == std::string_view::npos ? source.size() - begin : end - begin);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (!headerSeen)
        {
            if (line != protocolHeader)
            {
                return std::unexpected(ParseError::invalidHeader);
            }
            headerSeen = true;
        }
        else if (line == "END")
        {
            endSeen = true;
            break;
        }
        else if (!line.empty())
        {
            const auto fields = split(line, '\t');
            if (fields[0] == "OS" && fields.size() == 2)
            {
                sample.osName = std::string(fields[1]);
            }
            else if (fields[0] == "CPU" && fields.size() == 4)
            {
                if (!parseInteger(fields[1], sample.cpu.total) || !parseInteger(fields[2], sample.cpu.idle)
                    || !parseInteger(fields[3], sample.cpuCoreCount) || sample.cpuCoreCount == 0
                    || sample.cpuCoreCount > 4096 || sample.cpu.idle > sample.cpu.total)
                {
                    return std::unexpected(ParseError::malformed);
                }
                cpuSeen = true;
            }
            else if (fields[0] == "CORE" && fields.size() == 4 && sample.cores.size() < maximumCoreCount)
            {
                std::uint32_t index = 0;
                CpuCounter counter;
                if (!parseInteger(fields[1], index) || index != sample.cores.size()
                    || !parseInteger(fields[2], counter.total) || !parseInteger(fields[3], counter.idle)
                    || counter.idle > counter.total)
                {
                    return std::unexpected(ParseError::malformed);
                }
                sample.cores.push_back(counter);
            }
            else if (fields[0] == "MEM" && fields.size() == 9)
            {
                auto &memory = sample.memory;
                if (!parseInteger(fields[1], memory.totalKiB) || !parseInteger(fields[2], memory.availableKiB)
                    || !parseInteger(fields[3], memory.freeKiB) || !parseInteger(fields[4], memory.buffersKiB)
                    || !parseInteger(fields[5], memory.cachedKiB) || !parseInteger(fields[6], memory.reclaimableKiB)
                    || !parseInteger(fields[7], memory.swapTotalKiB) || !parseInteger(fields[8], memory.swapFreeKiB)
                    || memory.totalKiB == 0 || memory.availableKiB > memory.totalKiB
                    || memory.swapFreeKiB > memory.swapTotalKiB)
                {
                    return std::unexpected(ParseError::malformed);
                }
                memorySeen = true;
            }
            else if (fields[0] == "DISK" && fields.size() == 5 && sample.disks.size() < maximumDiskCount)
            {
                DiskCounters disk;
                if (!parseDouble(fields[1], disk.usedPercent) || !parseInteger(fields[2], disk.usedKiB)
                    || !parseInteger(fields[3], disk.totalKiB) || disk.usedPercent < 0.0 || disk.usedPercent > 100.0
                    || disk.usedKiB > disk.totalKiB || fields[4].empty())
                {
                    return std::unexpected(ParseError::malformed);
                }
                disk.mountPoint = std::string(fields[4]);
                sample.disks.push_back(std::move(disk));
            }
            else if (fields[0] == "NET" && fields.size() == 4 && sample.interfaces.size() < maximumInterfaceCount)
            {
                NetworkCounters interface;
                interface.name = std::string(fields[1]);
                if (interface.name.empty() || !parseInteger(fields[2], interface.receivedBytes)
                    || !parseInteger(fields[3], interface.transmittedBytes))
                {
                    return std::unexpected(ParseError::malformed);
                }
                sample.interfaces.push_back(std::move(interface));
            }
            else if (fields[0] == "PROC" && fields.size() == 4 && sample.processes.size() < maximumProcessCount)
            {
                ProcessMemory process;
                process.command = std::string(fields[3]);
                if (!parseInteger(fields[1], process.pid) || !parseDouble(fields[2], process.memoryPercent)
                    || process.memoryPercent < 0.0 || process.memoryPercent > 100.0 || process.command.empty())
                {
                    return std::unexpected(ParseError::malformed);
                }
                sample.processes.push_back(std::move(process));
            }
            else
            {
                return std::unexpected(ParseError::malformed);
            }
        }

        if (end == std::string_view::npos)
        {
            break;
        }
        begin = end + 1;
    }

    if (!headerSeen || !endSeen || sample.osName.empty())
    {
        return std::unexpected(ParseError::malformed);
    }
    if (sample.osName != "Linux")
    {
        return std::unexpected(ParseError::unsupported);
    }
    if (!cpuSeen || !memorySeen)
    {
        return std::unexpected(ParseError::malformed);
    }
    return sample;
}

std::string_view linuxRemoteTelemetryCommand(const bool includeDetails) noexcept
{
    return includeDetails ? detailedCommand : fastCommand;
}

Sample Accumulator::consume(RawSample raw, const std::uint32_t latencyMs,
                            const std::chrono::steady_clock::time_point capturedAt)
{
    Sample result;
    result.osName = raw.osName;
    result.cpuCoreCount = raw.cpuCoreCount;
    if (!raw.cores.empty())
    {
        if (m_previousCores && m_previousCores->size() == raw.cores.size() && capturedAt > m_previousCoresAt)
        {
            m_latestCorePercents.clear();
            m_latestCorePercents.reserve(raw.cores.size());
            for (std::size_t index = 0; index < raw.cores.size(); ++index)
            {
                m_latestCorePercents.push_back(counterPercent((*m_previousCores)[index], raw.cores[index]));
            }
        }
        m_previousCores = raw.cores;
        m_previousCoresAt = capturedAt;
    }
    result.corePercents = m_latestCorePercents;

    if (!raw.processes.empty())
    {
        m_latestProcesses = raw.processes;
    }
    result.processes = m_latestProcesses;

    if (raw.disks.size() > 1)
    {
        m_latestDisks = raw.disks;
    }
    else if (!raw.disks.empty())
    {
        const DiskCounters &root = raw.disks.front();
        const auto existing = std::ranges::find(m_latestDisks, root.mountPoint, &DiskCounters::mountPoint);
        if (existing == m_latestDisks.end())
        {
            m_latestDisks.insert(m_latestDisks.begin(), root);
        }
        else
        {
            *existing = root;
        }
    }
    result.disks = m_latestDisks;
    result.memory = raw.memory;
    result.memoryUsedKiB = raw.memory.totalKiB - raw.memory.availableKiB;
    result.sshProbeLatencyMs = latencyMs;
    result.capturedAt = capturedAt;

    if (m_previous && capturedAt > m_previousAt)
    {
        result.cpuPercent = counterPercent(m_previous->cpu, raw.cpu);
        const double seconds = std::chrono::duration<double>(capturedAt - m_previousAt).count();
        result.interfaces.reserve(raw.interfaces.size());
        for (const NetworkCounters &current : raw.interfaces)
        {
            const auto previous = std::ranges::find(m_previous->interfaces, current.name, &NetworkCounters::name);
            NetworkRate networkRate{.name = current.name};
            if (previous != m_previous->interfaces.end())
            {
                networkRate.receivedBytesPerSecond = rate(previous->receivedBytes, current.receivedBytes, seconds);
                networkRate.transmittedBytesPerSecond =
                    rate(previous->transmittedBytes, current.transmittedBytes, seconds);
            }
            result.receivedBytesPerSecond += networkRate.receivedBytesPerSecond;
            result.transmittedBytesPerSecond += networkRate.transmittedBytesPerSecond;
            result.interfaces.push_back(std::move(networkRate));
        }
    }
    else
    {
        result.interfaces.reserve(raw.interfaces.size());
        for (const NetworkCounters &current : raw.interfaces)
        {
            result.interfaces.push_back(NetworkRate{.name = current.name});
        }
    }

    m_previous = std::move(raw);
    m_previousAt = capturedAt;
    m_history.push_back(result);
    while (m_history.size() > maximumHistorySamples)
    {
        m_history.pop_front();
    }
    return result;
}

const std::deque<Sample> &Accumulator::history() const noexcept
{
    return m_history;
}

void Accumulator::reset() noexcept
{
    m_previous.reset();
    m_previousAt = {};
    m_previousCores.reset();
    m_previousCoresAt = {};
    m_latestCorePercents.clear();
    m_latestDisks.clear();
    m_latestProcesses.clear();
    m_history.clear();
}

void Scheduler::setVisible(const bool visible, const std::chrono::steady_clock::time_point now) noexcept
{
    if (m_visible == visible)
    {
        return;
    }
    m_visible = visible;
    m_inFlight = false;
    if (visible)
    {
        reset(now);
    }
}

bool Scheduler::visible() const noexcept
{
    return m_visible;
}

bool Scheduler::due(const std::chrono::steady_clock::time_point now) const noexcept
{
    return m_visible && !m_inFlight && !suspended() && now >= m_nextSample;
}

bool Scheduler::detailsDue(const std::chrono::steady_clock::time_point now) const noexcept
{
    return now >= m_nextDetails;
}

void Scheduler::markStarted(const std::chrono::steady_clock::time_point) noexcept
{
    m_inFlight = true;
}

void Scheduler::markSucceeded(const std::chrono::steady_clock::time_point now, const bool includedDetails) noexcept
{
    m_inFlight = false;
    m_failures = 0;
    m_nextSample = now + sampleInterval;
    if (includedDetails)
    {
        ++m_detailSamples;
        m_nextDetails = now + (m_detailSamples < 2 ? sampleInterval : detailsInterval);
    }
}

void Scheduler::markFailed(const std::chrono::steady_clock::time_point now) noexcept
{
    m_inFlight = false;
    ++m_failures;
    const unsigned exponent = std::min(m_failures - 1U, 2U);
    m_nextSample = now + sampleInterval * (1U << exponent);
}

bool Scheduler::suspended() const noexcept
{
    return m_failures >= maximumConsecutiveFailures;
}

unsigned Scheduler::consecutiveFailures() const noexcept
{
    return m_failures;
}

void Scheduler::reset(const std::chrono::steady_clock::time_point now) noexcept
{
    m_inFlight = false;
    m_failures = 0;
    m_detailSamples = 0;
    m_nextSample = now;
    m_nextDetails = now;
}

} // namespace ztermy::telemetry
