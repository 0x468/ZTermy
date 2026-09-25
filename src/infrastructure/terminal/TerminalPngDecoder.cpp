#include "infrastructure/terminal/TerminalPngDecoder.h"

#include <QBuffer>
#include <QImageReader>
#include <QtEndian>
extern "C"
{
#include <ghostty/vt/sys.h>
}

#include <cstring>
#include <mutex>
#include <new>
#include <utility>

namespace ztermy::terminal
{
namespace
{
constexpr qsizetype maximumInputBytes = qsizetype{8} * 1024 * 1024;
constexpr std::uint64_t maximumRasterBytes = std::uint64_t{32} * 1024 * 1024;
constexpr QByteArrayView pngSignature("\x89PNG\r\n\x1a\n", 8);

QByteArray pixelChunks(QByteArrayView bytes)
{
    if (bytes.size() < 33 || bytes.size() > maximumInputBytes || !bytes.startsWith(pngSignature))
        return {};
    QByteArray result(pngSignature.data(), pngSignature.size());
    bool header = false;
    bool palette = false;
    bool transparency = false;
    bool imageData = false;
    bool imageDataEnded = false;
    for (qsizetype offset = 8; offset < bytes.size();)
    {
        if (bytes.size() - offset < 12)
            return {};
        const auto length = qFromBigEndian<quint32>(bytes.data() + offset);
        if (std::cmp_greater(length, bytes.size() - offset - 12))
            return {};
        const auto type = bytes.sliced(offset + 4, 4);
        const auto payload = bytes.sliced(offset + 8, length);
        const qsizetype chunkBytes = static_cast<qsizetype>(length) + 12;
        bool retain = true;
        if (!header && type != "IHDR")
            return {};
        if (type == "IHDR")
        {
            if (header || length != 13)
                return {};
            header = true;
            const auto width = qFromBigEndian<quint32>(payload.data());
            const auto height = qFromBigEndian<quint32>(payload.data() + 4);
            // Qt may initially decode 16-bit PNGs to an eight-byte-per-pixel image.
            const std::uint64_t pixelBytes = payload[8] == 16 ? 8 : 4;
            if (!width || !height || width > 8192 || height > 8192
                || std::uint64_t{width} * height * pixelBytes > maximumRasterBytes)
                return {};
        }
        else if (type == "PLTE")
        {
            if (palette || imageData || transparency || length == 0 || length > 768 || length % 3 != 0)
                return {};
            palette = true;
        }
        else if (type == "tRNS")
        {
            if (transparency || imageData || length > 256)
                return {};
            transparency = true;
        }
        else if (type == "IDAT")
        {
            if (imageDataEnded)
                return {};
            imageData = true;
        }
        else if (type == "IEND")
        {
            if (!imageData || length != 0 || offset + chunkBytes != bytes.size())
                return {};
            result.append(bytes.data() + offset, chunkBytes);
            return result;
        }
        else
        {
            // Do not inflate text/ICC metadata or decode APNG animation frames.
            // Preserve critical pixels and transparency; unknown critical chunks are an error.
            if ((static_cast<unsigned char>(type[0]) & 0x20U) == 0)
                return {};
            retain = false;
        }
        if (imageData && type != "IDAT")
            imageDataEnded = true;
        if (retain)
            result.append(bytes.data() + offset, chunkBytes);
        offset += chunkBytes;
    }
    return {};
}

bool decodeForGhostty(void *, const GhosttyAllocator *allocator, const std::uint8_t *data, std::size_t size,
                      GhosttySysImage *out) noexcept
{
    if (!out)
        return false;
    *out = {};
    if (!data || size > static_cast<std::size_t>(maximumInputBytes))
        return false;
    try
    {
        const auto image = decodeTerminalPng({reinterpret_cast<const char *>(data), static_cast<qsizetype>(size)});
        if (image.isNull())
            return false;
        const auto rowBytes = static_cast<std::size_t>(image.width()) * 4;
        const auto byteCount = rowBytes * static_cast<std::size_t>(image.height());
        auto *pixels = ghostty_alloc(allocator, byteCount);
        if (!pixels)
            return false;
        for (int row = 0; row < image.height(); ++row)
            std::memcpy(pixels + static_cast<std::size_t>(row) * rowBytes, image.constScanLine(row), rowBytes);
        *out = {.width = static_cast<std::uint32_t>(image.width()),
                .height = static_cast<std::uint32_t>(image.height()),
                .data = pixels,
                .data_len = byteCount};
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace

QImage decodeTerminalPng(QByteArrayView bytes)
{
    auto encoded = pixelChunks(bytes);
    if (encoded.isEmpty())
        return {};
    QBuffer buffer(&encoded);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer, "PNG");
    reader.setAutoDetectImageFormat(false);
    reader.setAutoTransform(false);
    auto decoded = reader.read();
    if (decoded.isNull() || std::cmp_greater(decoded.sizeInBytes(), maximumRasterBytes))
        return {};
    return decoded.convertToFormat(QImage::Format_RGBA8888);
}

bool installTerminalPngDecoder()
{
    static std::once_flag once;
    static bool installed = false;
    std::call_once(once, [] {
        installed = ghostty_sys_set(GHOSTTY_SYS_OPT_DECODE_PNG, reinterpret_cast<const void *>(&decodeForGhostty))
                    == GHOSTTY_SUCCESS;
    });
    return installed;
}
} // namespace ztermy::terminal
