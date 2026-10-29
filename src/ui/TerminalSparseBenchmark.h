#pragma once

#include "core/diagnostics/LatencyHistogram.h"
#include "domain/terminal/GhosttyTerminalEngine.h"
#include "platform/windows/NativeWindow.h"
#include "ui/WindowStateRuntimeSmoke.h"
#include "ui/terminal/TerminalItem.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <array>
#include <span>
#include <string>
#include <utility>

namespace ztermy::ui
{
// Synthetic engine -> snapshot -> production viewport probe. No Shell/network,
// worker scheduling or background-session policy is inferred from these results.
inline bool runTerminalSparseBenchmark(NativeWindow &window, const QString &directory)
{
    using namespace std::chrono_literals;
    if (!QCoreApplication::arguments().contains(QStringLiteral("--data-dir")))
        return false;
    window.rootObject()->setVisible(false);
    const auto restoreRoot = qScopeGuard([&] {
        window.rootObject()->setVisible(true);
    });
    TerminalItem item(window.contentItem());
    item.setFontPixelSize(14);
    item.setCursorBlink(false);
    item.setTerminalCursorVisible(false);
    item.setPerformanceMetricsEnabled(true);
    QJsonArray cases;
    bool passed = true;
    for (const auto geometry :
         std::array<terminal::TerminalGeometry, 3>{terminal::TerminalGeometry{.columns = 80, .rows = 24},
                                                   {.columns = 160, .rows = 45},
                                                   {.columns = 240, .rows = 65}})
    {
        auto created = terminal::GhosttyTerminalEngine::create(geometry);
        if (!created)
            return false;
        auto &engine = **created;
        const auto feed = [&](const std::string &text) {
            return !engine.feed(std::as_bytes(std::span{text}));
        };
        if (!feed("\x1b[?25l\x1b[2J"))
            return false;
        for (int row = 1; std::cmp_less_equal(row, geometry.rows); ++row)
        {
            std::string command{"\x1b["};
            command += std::to_string(row);
            command += ";1H";
            command += std::string(geometry.columns - 2, static_cast<char>('A' + row % 26));
            if (!feed(command))
                return false;
        }
        window.resize({qCeil(item.property("cellPixelWidth").toReal() * geometry.columns) + 30,
                       qCeil(item.property("cellPixelHeight").toReal() * geometry.rows) + 30});
        item.setSize(window.size());
        showForRuntimeSmoke(window);
        window.requestActivate();
        QImage visibleFrame;
        for (const bool visible : {true, false})
        {
            item.setVisible(visible);
            auto initial = engine.snapshot();
            if (!initial)
                return false;
            item.setSnapshot(std::make_shared<terminal::TerminalSnapshot>(std::move(*initial)));
            processWindowEventsFor(200ms);
            item.resetPerformanceMetrics();
            diagnostics::LatencyHistogram snapshotLatency;
            qint64 snapshotNanoseconds = 0;
            constexpr int updates = 120;
            for (int update = 0; update < updates; ++update)
            {
                const std::string value = std::to_string(update + (visible ? 0 : 1000)) + std::string(24, 'x');
                std::string command{"\x1b[1;1H"};
                command += value;
                command += "\x1b[";
                command += std::to_string(geometry.rows / 2);
                command += ";1H";
                command += value;
                if (!feed(command))
                    return false;
                const auto before = std::chrono::steady_clock::now();
                auto snapshot = engine.snapshot();
                const auto elapsed = std::chrono::steady_clock::now() - before;
                snapshotLatency.record(elapsed);
                snapshotNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
                if (!snapshot)
                    return false;
                item.setSnapshot(std::make_shared<terminal::TerminalSnapshot>(std::move(*snapshot)));
                processWindowEventsFor(16ms);
            }
            processWindowEventsFor(100ms);
            const auto metrics = item.performanceMetrics();
            const auto latency = snapshotLatency.summary();
            const bool effectivelyVisible = item.isVisible();
            bool restoredLatest = true;
            if (visible)
                visibleFrame = window.grabWindow();
            else
            {
                item.setVisible(true);
                processWindowEventsFor(100ms);
                const auto restoredFrame = window.grabWindow();
                restoredLatest = !restoredFrame.isNull() && !visibleFrame.isNull() && restoredFrame != visibleFrame
                                 && item.performanceMetrics().renderedFrames > metrics.renderedFrames;
            }
            const bool casePassed = effectivelyVisible == visible && restoredLatest
                                    && metrics.snapshotUpdates == updates
                                    && (visible ? metrics.renderedFrames > 0 : metrics.renderedFrames == 0);
            passed = passed && casePassed;
            cases.append(QJsonObject{
                {QStringLiteral("columns"), geometry.columns},
                {QStringLiteral("rows"), geometry.rows},
                {QStringLiteral("visible"), visible},
                {QStringLiteral("effectivelyVisible"), effectivelyVisible},
                {QStringLiteral("restoredLatest"), restoredLatest},
                {QStringLiteral("updates"), updates},
                {QStringLiteral("changedRowsPerUpdate"), 2},
                {QStringLiteral("snapshotAverageUs"), static_cast<double>(snapshotNanoseconds) / updates / 1000.0},
                {QStringLiteral("snapshotP95UpperBoundUs"), static_cast<qint64>(latency.p95UpperBoundMicroseconds)},
                {QStringLiteral("paintP95UpperBoundUs"),
                 static_cast<qint64>(metrics.paintLatency.p95UpperBoundMicroseconds)},
                {QStringLiteral("paintMaxUs"), static_cast<qint64>(metrics.paintLatency.maxMicroseconds)},
                {QStringLiteral("paintAverageUs"), metrics.renderedFrames == 0
                                                       ? 0.0
                                                       : static_cast<double>(metrics.paintTotalNanoseconds)
                                                             / static_cast<double>(metrics.renderedFrames) / 1000.0},
                {QStringLiteral("rowRasterHits"), static_cast<qint64>(metrics.rowRasterHits)},
                {QStringLiteral("rowRasterMisses"), static_cast<qint64>(metrics.rowRasterMisses)},
                {QStringLiteral("rowRasterPeakBytes"), static_cast<qint64>(metrics.rowRasterPeakBytes)},
                {QStringLiteral("textPaintP95UpperBoundUs"),
                 static_cast<qint64>(metrics.textPaintLatency.p95UpperBoundMicroseconds)},
                {QStringLiteral("renderedFrames"), static_cast<qint64>(metrics.renderedFrames)},
                {QStringLiteral("partialSnapshots"), static_cast<qint64>(metrics.partialSnapshotUpdates)},
                {QStringLiteral("fullDamageFrames"), static_cast<qint64>(metrics.fullFrames)},
                {QStringLiteral("uploadedBytesEstimate"), static_cast<qint64>(metrics.uploadedBytes)},
                {QStringLiteral("passed"), casePassed}});
        }
        item.setVisible(true);
    }
    QDir().mkpath(directory);
    QSaveFile file(QDir(directory).filePath(QStringLiteral("terminal-sparse.json")));
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{QStringLiteral("passed"), passed}, {QStringLiteral("cases"), cases}}).toJson();
    const bool saved = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    qInfo() << "Sparse terminal benchmark passed=" << passed << "saved=" << saved;
    return passed && saved;
}
} // namespace ztermy::ui
