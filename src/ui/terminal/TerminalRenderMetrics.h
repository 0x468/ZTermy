#pragma once

#include "core/diagnostics/LatencyHistogram.h"
#include "domain/terminal/TerminalEngine.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ztermy::ui
{

struct TerminalRenderMetricsSnapshot final
{
    diagnostics::LatencySummary paintLatency;
    diagnostics::LatencySummary textureLatency;
    std::uint64_t renderedFrames = 0;
    std::uint64_t fullFrames = 0;
    std::uint64_t partialFrames = 0;
    std::uint64_t cleanFrames = 0;
    std::uint64_t renderedDamagedRows = 0;
    std::uint64_t snapshotUpdates = 0;
    std::uint64_t fullSnapshotUpdates = 0;
    std::uint64_t partialSnapshotUpdates = 0;
    std::uint64_t cleanSnapshotUpdates = 0;
    std::uint64_t snapshotDamagedRows = 0;
    std::uint64_t cursorInvalidations = 0;
    std::uint64_t uploadedBytes = 0;
    std::uint64_t maximumFramePixels = 0;
};

class TerminalRenderMetrics final
{
public:
    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void reset() noexcept;
    void recordSnapshot(terminal::TerminalDamageKind damage, std::size_t damagedRows) noexcept;
    void recordCursorInvalidation() noexcept;
    void recordFrame(std::chrono::steady_clock::duration paintLatency,
                     std::chrono::steady_clock::duration textureLatency, std::uint64_t framePixels,
                     terminal::TerminalDamageKind damage, std::size_t damagedRows) noexcept;
    [[nodiscard]] TerminalRenderMetricsSnapshot snapshot() const noexcept;

private:
    static void updateMaximum(std::atomic_uint64_t &maximum, std::uint64_t candidate) noexcept;

    diagnostics::LatencyHistogram m_paintLatency;
    diagnostics::LatencyHistogram m_textureLatency;
    std::atomic_bool m_enabled = false;
    std::atomic_uint64_t m_renderedFrames = 0;
    std::atomic_uint64_t m_fullFrames = 0;
    std::atomic_uint64_t m_partialFrames = 0;
    std::atomic_uint64_t m_cleanFrames = 0;
    std::atomic_uint64_t m_renderedDamagedRows = 0;
    std::atomic_uint64_t m_snapshotUpdates = 0;
    std::atomic_uint64_t m_fullSnapshotUpdates = 0;
    std::atomic_uint64_t m_partialSnapshotUpdates = 0;
    std::atomic_uint64_t m_cleanSnapshotUpdates = 0;
    std::atomic_uint64_t m_snapshotDamagedRows = 0;
    std::atomic_uint64_t m_cursorInvalidations = 0;
    std::atomic_uint64_t m_uploadedBytes = 0;
    std::atomic_uint64_t m_maximumFramePixels = 0;
};

} // namespace ztermy::ui
