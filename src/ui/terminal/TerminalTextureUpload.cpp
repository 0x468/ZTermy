#include "ui/terminal/TerminalTextureUpload.h"

#include <QElapsedTimer>
#include <QImage>
#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QRegion>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <rhi/qrhi.h>

#include <atomic>
#include <cmath>
#include <memory>

namespace ztermy::ui
{
namespace
{
struct Counters
{
    std::atomic_bool enabled = false;
    std::atomic_bool gpuSupported = false;
    std::atomic_uint64_t creations = 0;
    std::atomic_uint64_t uploadBytes = 0;
    std::atomic_uint64_t factoryBytesEstimate = 0;
    std::atomic_uint64_t uploadRegions = 0;
    std::atomic_uint64_t enqueueNanoseconds = 0;
    std::atomic_uint64_t gpuSamples = 0;
    std::atomic_uint64_t gpuNanoseconds = 0;
};
Counters counters;

class UploadTexture final : public QSGTexture
{
public:
    UploadTexture(QRhi *owner, QSize size) : m_owner(owner)
    {
        m_texture.reset(owner->newTexture(QRhiTexture::BGRA8, size));
        m_ready = m_texture && m_texture->create();
        if (m_ready && counters.enabled)
            ++counters.creations;
    }
    [[nodiscard]] bool ready() const { return m_ready; }
    [[nodiscard]] QRhi *owner() const { return m_owner; }
    [[nodiscard]] qint64 comparisonKey() const override
    {
        return static_cast<qint64>(reinterpret_cast<quintptr>(m_texture.get()));
    }
    [[nodiscard]] QRhiTexture *rhiTexture() const override { return m_texture.get(); }
    [[nodiscard]] QSize textureSize() const override { return m_texture->pixelSize(); }
    [[nodiscard]] bool hasAlphaChannel() const override { return true; }
    [[nodiscard]] bool hasMipmaps() const override { return false; }

    void prepare(const QImage &image, const QRegion &bands, bool allowBands)
    {
        m_pending = image; // Owned, implicitly shared image retained by the update batch.
        m_fullPending = m_fullPending || !m_initialized || !allowBands;
        if (allowBands)
            m_bands += bands;
        if (m_bands.rectCount() > 8)
            m_fullPending = true;
        qint64 area = 0;
        for (const QRect &rect : m_bands)
            area += static_cast<qint64>(rect.width()) * rect.height();
        if (area > static_cast<qint64>(image.width()) * image.height() / 2)
            m_fullPending = true;
    }

    void commitTextureOperations(QRhi *rhi, QRhiResourceUpdateBatch *batch) override
    {
        if (rhi != m_owner || m_pending.isNull())
            return;
        const bool collect = counters.enabled.load();
        QElapsedTimer elapsed;
        if (collect)
            elapsed.start();
        QList<QRhiTextureUploadEntry> entries;
        const QRegion regions = m_fullPending ? QRegion(m_pending.rect()) : m_bands;
        for (const QRect &rect : regions)
        {
            // An update batch can outlive this frame. Do not retain the full
            // CPU canvas for a small upload: that would detach it on the next
            // paint and copy the whole viewport again.
            QRhiTextureSubresourceUploadDescription source(rect == m_pending.rect() ? m_pending : m_pending.copy(rect));
            if (rect != m_pending.rect())
            {
                source.setDestinationTopLeft(rect.topLeft());
            }
            entries.append(QRhiTextureUploadEntry(0, 0, source));
            if (collect)
            {
                counters.uploadBytes += static_cast<std::uint64_t>(rect.width()) * rect.height() * 4U;
                ++counters.uploadRegions;
            }
        }
        if (!entries.isEmpty())
        {
            QRhiTextureUploadDescription upload;
            upload.setEntries(entries.cbegin(), entries.cend());
            batch->uploadTexture(m_texture.get(), upload);
        }
        if (collect)
            counters.enqueueNanoseconds += static_cast<std::uint64_t>(elapsed.nsecsElapsed());
        m_pending = {};
        m_bands = {};
        m_fullPending = false;
        m_initialized = true;
    }

private:
    QRhi *m_owner;
    std::unique_ptr<QRhiTexture> m_texture;
    QImage m_pending;
    QRegion m_bands;
    bool m_ready = false;
    bool m_initialized = false;
    bool m_fullPending = false;
};
} // namespace

void configureTerminalTextureMetrics(QQuickWindow &window)
{
    if (qEnvironmentVariableIntValue("ZTERMY_TEXTURE_METRICS") != 1)
        return;
    counters.enabled = true;
    auto configuration = window.graphicsConfiguration();
    configuration.setTimestamps(true);
    window.setGraphicsConfiguration(configuration);
    QObject::connect(
        &window, &QQuickWindow::afterFrameEnd, &window,
        [&window] {
            auto *rhi = window.rhi();
            if (!rhi)
                return;
            counters.gpuSupported = rhi->isFeatureSupported(QRhi::Timestamps);
            auto *swapchain = window.swapChain();
            if (!counters.gpuSupported || !swapchain)
                return;
            const double seconds = swapchain->currentFrameCommandBuffer()->lastCompletedGpuTime();
            if (seconds > 0 && std::isfinite(seconds))
            {
                ++counters.gpuSamples;
                counters.gpuNanoseconds += static_cast<std::uint64_t>(seconds * 1e9);
            }
        },
        Qt::DirectConnection);
}

TerminalTextureUploadSample terminalTextureUploadSample()
{
    return {.creations = counters.creations.load(),
            .uploadBytes = counters.uploadBytes.load(),
            .factoryBytesEstimate = counters.factoryBytesEstimate.load(),
            .uploadRegions = counters.uploadRegions.load(),
            .enqueueNanoseconds = counters.enqueueNanoseconds.load(),
            .gpuSamples = counters.gpuSamples.load(),
            .gpuNanoseconds = counters.gpuNanoseconds.load()};
}

QJsonObject terminalTextureUploadDelta(const TerminalTextureUploadSample &before,
                                       const TerminalTextureUploadSample &after)
{
    const auto samples = after.gpuSamples - before.gpuSamples;
    return {{QStringLiteral("mode"), qEnvironmentVariable("ZTERMY_TEXTURE_UPLOAD_MODE", QStringLiteral("bands"))},
            {QStringLiteral("textureCreations"), static_cast<qint64>(after.creations - before.creations)},
            {QStringLiteral("submittedBytes"), static_cast<qint64>(after.uploadBytes - before.uploadBytes)},
            {QStringLiteral("factoryBytesEstimate"),
             static_cast<qint64>(after.factoryBytesEstimate - before.factoryBytesEstimate)},
            {QStringLiteral("submittedRegions"), static_cast<qint64>(after.uploadRegions - before.uploadRegions)},
            {QStringLiteral("enqueueNanoseconds"),
             static_cast<qint64>(after.enqueueNanoseconds - before.enqueueNanoseconds)},
            {QStringLiteral("gpuTimestampsSupported"), counters.gpuSupported.load()},
            {QStringLiteral("gpuSamples"), static_cast<qint64>(samples)},
            {QStringLiteral("gpuAverageUs"), samples == 0
                                                 ? 0.0
                                                 : static_cast<double>(after.gpuNanoseconds - before.gpuNanoseconds)
                                                       / static_cast<double>(samples) / 1000.0}};
}

QSGTexture *prepareTerminalTexture(QQuickWindow &window, QSGTexture *current, const QImage &image, const QRegion &bands,
                                   const bool bandsAllowed)
{
    const QString mode = qEnvironmentVariable("ZTERMY_TEXTURE_UPLOAD_MODE");
    if (mode == QStringLiteral("factory") || image.isNull() || image.format() != QImage::Format_ARGB32_Premultiplied)
        return nullptr;
    auto *rhi = window.rhi();
    if (!rhi || window.rendererInterface()->graphicsApi() != QSGRendererInterface::Direct3D11
        || !rhi->isTextureFormatSupported(QRhiTexture::BGRA8))
        return nullptr;
    auto *texture = dynamic_cast<UploadTexture *>(current);
    if (!texture || texture->textureSize() != image.size() || texture->owner() != rhi)
    {
        auto fresh = std::make_unique<UploadTexture>(rhi, image.size());
        if (!fresh->ready())
            return nullptr;
        texture = fresh.release(); // Node takes ownership after prepare.
    }
    texture->prepare(image, bands, bandsAllowed && mode != QStringLiteral("full"));
    return texture;
}

void recordFactoryTextureUpload(const QImage &image)
{
    if (counters.enabled)
    {
        ++counters.creations;
        counters.factoryBytesEstimate += static_cast<std::uint64_t>(image.width()) * image.height() * 4U;
    }
}
} // namespace ztermy::ui
