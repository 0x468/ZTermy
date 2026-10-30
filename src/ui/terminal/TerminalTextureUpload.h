#pragma once

#include <QJsonObject>

#include <cstdint>

class QImage;
class QQuickWindow;
class QRegion;
class QSGTexture;

namespace ztermy::ui
{
// Opt-in diagnostics for the D3D11 upload path; no persistent configuration.
struct TerminalTextureUploadSample
{
    std::uint64_t creations = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t factoryBytesEstimate = 0;
    std::uint64_t uploadRegions = 0;
    std::uint64_t enqueueNanoseconds = 0;
    std::uint64_t gpuSamples = 0;
    std::uint64_t gpuNanoseconds = 0;
};

void configureTerminalTextureMetrics(QQuickWindow &window);
[[nodiscard]] TerminalTextureUploadSample terminalTextureUploadSample();
[[nodiscard]] QJsonObject terminalTextureUploadDelta(const TerminalTextureUploadSample &before,
                                                     const TerminalTextureUploadSample &after);
[[nodiscard]] QSGTexture *prepareTerminalTexture(QQuickWindow &window, QSGTexture *current, const QImage &image,
                                                 const QRegion &bands, bool bandsAllowed);
void recordFactoryTextureUpload(const QImage &image);
} // namespace ztermy::ui
