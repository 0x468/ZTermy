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
    std::uint64_t rowReuseAnalysisFrames = 0;
    std::uint64_t rowReuseCandidateRows = 0;
    std::uint64_t rowReuseReusableRows = 0;
    std::uint64_t rowReuseRepaintRows = 0;
    std::uint64_t rowReuseShiftedFrames = 0;
    diagnostics::LatencySummary imagePreparationLatency;
    diagnostics::LatencySummary snapshotPreparationLatency;
    diagnostics::LatencySummary backgroundPaintLatency;
    diagnostics::LatencySummary textPaintLatency;
    diagnostics::LatencySummary overlayPaintLatency;
};

struct TerminalPaintPhaseDurations final
{
    std::chrono::steady_clock::duration imagePreparation{};
    std::chrono::steady_clock::duration snapshotPreparation{};
    std::chrono::steady_clock::duration backgroundPaint{};
    std::chrono::steady_clock::duration textPaint{};
    std::chrono::steady_clock::duration overlayPaint{};
};

class TerminalRenderMetrics final
{
public:
    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void reset() noexcept;
    void recordSnapshot(terminal::TerminalDamageKind damage, std::size_t damagedRows) noexcept;
    void recordCursorInvalidation() noexcept;
    void recordRowReuse(std::size_t candidateRows, std::size_t reusableRows, bool shifted) noexcept;
    void recordPaintPhases(const TerminalPaintPhaseDurations &durations) noexcept;
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
    std::atomic_uint64_t m_rowReuseAnalysisFrames = 0;
    std::atomic_uint64_t m_rowReuseCandidateRows = 0;
    std::atomic_uint64_t m_rowReuseReusableRows = 0;
    std::atomic_uint64_t m_rowReuseRepaintRows = 0;
    std::atomic_uint64_t m_rowReuseShiftedFrames = 0;
    diagnostics::LatencyHistogram m_imagePreparationLatency;
    diagnostics::LatencyHistogram m_snapshotPreparationLatency;
    diagnostics::LatencyHistogram m_backgroundPaintLatency;
    diagnostics::LatencyHistogram m_textPaintLatency;
    diagnostics::LatencyHistogram m_overlayPaintLatency;
};

} // namespace ztermy::ui
