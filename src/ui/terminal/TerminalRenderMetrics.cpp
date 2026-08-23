#include "ui/terminal/TerminalRenderMetrics.h"

#include <algorithm>
#include <limits>

namespace ztermy::ui
{
namespace
{

constexpr std::uint64_t bytesPerPixel = 4;

void incrementDamageCounters(const terminal::TerminalDamageKind damage, std::atomic_uint64_t &fullFrames,
                             std::atomic_uint64_t &partialFrames, std::atomic_uint64_t &cleanFrames) noexcept
{
    switch (damage)
    {
        case terminal::TerminalDamageKind::full:
            fullFrames.fetch_add(1, std::memory_order_relaxed);
            break;
        case terminal::TerminalDamageKind::partial:
            partialFrames.fetch_add(1, std::memory_order_relaxed);
            break;
        case terminal::TerminalDamageKind::none:
            cleanFrames.fetch_add(1, std::memory_order_relaxed);
            break;
    }
}

} // namespace

void TerminalRenderMetrics::setEnabled(const bool enabled) noexcept
{
    m_enabled.store(enabled, std::memory_order_relaxed);
}

bool TerminalRenderMetrics::enabled() const noexcept
{
    return m_enabled.load(std::memory_order_relaxed);
}

void TerminalRenderMetrics::reset() noexcept
{
    m_paintLatency.reset();
    m_textureLatency.reset();
    m_renderedFrames.store(0, std::memory_order_relaxed);
    m_fullFrames.store(0, std::memory_order_relaxed);
    m_partialFrames.store(0, std::memory_order_relaxed);
    m_cleanFrames.store(0, std::memory_order_relaxed);
    m_renderedDamagedRows.store(0, std::memory_order_relaxed);
    m_snapshotUpdates.store(0, std::memory_order_relaxed);
    m_fullSnapshotUpdates.store(0, std::memory_order_relaxed);
    m_partialSnapshotUpdates.store(0, std::memory_order_relaxed);
    m_cleanSnapshotUpdates.store(0, std::memory_order_relaxed);
    m_snapshotDamagedRows.store(0, std::memory_order_relaxed);
    m_cursorInvalidations.store(0, std::memory_order_relaxed);
    m_uploadedBytes.store(0, std::memory_order_relaxed);
    m_maximumFramePixels.store(0, std::memory_order_relaxed);
    m_rowReuseAnalysisFrames.store(0, std::memory_order_relaxed);
    m_rowReuseCandidateRows.store(0, std::memory_order_relaxed);
    m_rowReuseReusableRows.store(0, std::memory_order_relaxed);
    m_rowReuseRepaintRows.store(0, std::memory_order_relaxed);
    m_rowReuseShiftedFrames.store(0, std::memory_order_relaxed);
    m_imagePreparationLatency.reset();
    m_snapshotPreparationLatency.reset();
    m_backgroundPaintLatency.reset();
    m_textPaintLatency.reset();
    m_overlayPaintLatency.reset();
}

void TerminalRenderMetrics::recordSnapshot(const terminal::TerminalDamageKind damage,
                                           const std::size_t damagedRows) noexcept
{
    if (!enabled())
    {
        return;
    }
    m_snapshotUpdates.fetch_add(1, std::memory_order_relaxed);
    incrementDamageCounters(damage, m_fullSnapshotUpdates, m_partialSnapshotUpdates, m_cleanSnapshotUpdates);
    m_snapshotDamagedRows.fetch_add(damagedRows, std::memory_order_relaxed);
}

void TerminalRenderMetrics::recordCursorInvalidation() noexcept
{
    if (enabled())
    {
        m_cursorInvalidations.fetch_add(1, std::memory_order_relaxed);
    }
}

void TerminalRenderMetrics::recordRowReuse(const std::size_t candidateRows, const std::size_t reusableRows,
                                           const bool shifted) noexcept
{
    if (!enabled())
    {
        return;
    }
    const std::size_t boundedReusableRows = std::min(candidateRows, reusableRows);
    m_rowReuseAnalysisFrames.fetch_add(1, std::memory_order_relaxed);
    m_rowReuseCandidateRows.fetch_add(candidateRows, std::memory_order_relaxed);
    m_rowReuseReusableRows.fetch_add(boundedReusableRows, std::memory_order_relaxed);
    m_rowReuseRepaintRows.fetch_add(candidateRows - boundedReusableRows, std::memory_order_relaxed);
    if (shifted)
    {
        m_rowReuseShiftedFrames.fetch_add(1, std::memory_order_relaxed);
    }
}

void TerminalRenderMetrics::recordPaintPhases(const TerminalPaintPhaseDurations &durations) noexcept
{
    if (!enabled())
    {
        return;
    }
    m_imagePreparationLatency.record(durations.imagePreparation);
    m_snapshotPreparationLatency.record(durations.snapshotPreparation);
    m_backgroundPaintLatency.record(durations.backgroundPaint);
    m_textPaintLatency.record(durations.textPaint);
    m_overlayPaintLatency.record(durations.overlayPaint);
}

void TerminalRenderMetrics::recordFrame(const std::chrono::steady_clock::duration paintLatency,
                                        const std::chrono::steady_clock::duration textureLatency,
                                        const std::uint64_t framePixels, const terminal::TerminalDamageKind damage,
                                        const std::size_t damagedRows) noexcept
{
    if (!enabled())
    {
        return;
    }
    m_paintLatency.record(paintLatency);
    m_textureLatency.record(textureLatency);
    m_renderedFrames.fetch_add(1, std::memory_order_relaxed);
    incrementDamageCounters(damage, m_fullFrames, m_partialFrames, m_cleanFrames);
    m_renderedDamagedRows.fetch_add(damagedRows, std::memory_order_relaxed);
    const std::uint64_t uploadBytes = framePixels > std::numeric_limits<std::uint64_t>::max() / bytesPerPixel
                                          ? std::numeric_limits<std::uint64_t>::max()
                                          : framePixels * bytesPerPixel;
    m_uploadedBytes.fetch_add(uploadBytes, std::memory_order_relaxed);
    updateMaximum(m_maximumFramePixels, framePixels);
}

TerminalRenderMetricsSnapshot TerminalRenderMetrics::snapshot() const noexcept
{
    return {
        .paintLatency = m_paintLatency.summary(),
        .textureLatency = m_textureLatency.summary(),
        .renderedFrames = m_renderedFrames.load(std::memory_order_relaxed),
        .fullFrames = m_fullFrames.load(std::memory_order_relaxed),
        .partialFrames = m_partialFrames.load(std::memory_order_relaxed),
        .cleanFrames = m_cleanFrames.load(std::memory_order_relaxed),
        .renderedDamagedRows = m_renderedDamagedRows.load(std::memory_order_relaxed),
        .snapshotUpdates = m_snapshotUpdates.load(std::memory_order_relaxed),
        .fullSnapshotUpdates = m_fullSnapshotUpdates.load(std::memory_order_relaxed),
        .partialSnapshotUpdates = m_partialSnapshotUpdates.load(std::memory_order_relaxed),
        .cleanSnapshotUpdates = m_cleanSnapshotUpdates.load(std::memory_order_relaxed),
        .snapshotDamagedRows = m_snapshotDamagedRows.load(std::memory_order_relaxed),
        .cursorInvalidations = m_cursorInvalidations.load(std::memory_order_relaxed),
        .uploadedBytes = m_uploadedBytes.load(std::memory_order_relaxed),
        .maximumFramePixels = m_maximumFramePixels.load(std::memory_order_relaxed),
        .rowReuseAnalysisFrames = m_rowReuseAnalysisFrames.load(std::memory_order_relaxed),
        .rowReuseCandidateRows = m_rowReuseCandidateRows.load(std::memory_order_relaxed),
        .rowReuseReusableRows = m_rowReuseReusableRows.load(std::memory_order_relaxed),
        .rowReuseRepaintRows = m_rowReuseRepaintRows.load(std::memory_order_relaxed),
        .rowReuseShiftedFrames = m_rowReuseShiftedFrames.load(std::memory_order_relaxed),
        .imagePreparationLatency = m_imagePreparationLatency.summary(),
        .snapshotPreparationLatency = m_snapshotPreparationLatency.summary(),
        .backgroundPaintLatency = m_backgroundPaintLatency.summary(),
        .textPaintLatency = m_textPaintLatency.summary(),
        .overlayPaintLatency = m_overlayPaintLatency.summary(),
    };
}

void TerminalRenderMetrics::updateMaximum(std::atomic_uint64_t &maximum, const std::uint64_t candidate) noexcept
{
    std::uint64_t current = maximum.load(std::memory_order_relaxed);
    while (current < candidate
           && !maximum.compare_exchange_weak(current, candidate, std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

} // namespace ztermy::ui
