#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{

struct Color final
{
    std::uint8_t blue;
    std::uint8_t green;
    std::uint8_t red;
    std::uint8_t alpha;
};

struct IconImage final
{
    std::uint32_t size;
    std::vector<Color> pixels;
};

void appendByte(std::vector<std::uint8_t> &bytes, const std::uint8_t value)
{
    bytes.push_back(value);
}

void appendWord(std::vector<std::uint8_t> &bytes, const std::uint16_t value)
{
    appendByte(bytes, static_cast<std::uint8_t>(value & 0xffU));
    appendByte(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void appendDword(std::vector<std::uint8_t> &bytes, const std::uint32_t value)
{
    appendWord(bytes, static_cast<std::uint16_t>(value & 0xffffU));
    appendWord(bytes, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

[[nodiscard]] double distanceToSegment(const double x, const double y, const double x1, const double y1,
                                       const double x2, const double y2)
{
    const double deltaX = x2 - x1;
    const double deltaY = y2 - y1;
    const double lengthSquared = (deltaX * deltaX) + (deltaY * deltaY);
    const double projection = std::clamp((((x - x1) * deltaX) + ((y - y1) * deltaY)) / lengthSquared, 0.0, 1.0);
    const double nearestX = x1 + (projection * deltaX);
    const double nearestY = y1 + (projection * deltaY);
    return std::hypot(x - nearestX, y - nearestY);
}

[[nodiscard]] bool insideRoundedSquare(const double x, const double y, const double size)
{
    const double inset = size * 0.04;
    const double radius = size * 0.24;
    const double center = size / 2.0;
    const double halfInnerSize = center - inset;
    const double cornerX = std::max(std::abs(x - center) - (halfInnerSize - radius), 0.0);
    const double cornerY = std::max(std::abs(y - center) - (halfInnerSize - radius), 0.0);
    return std::hypot(cornerX, cornerY) <= radius;
}

[[nodiscard]] bool insideTerminalGlyph(const double x, const double y, const double size)
{
    const double stroke = std::max(0.8, size * 0.043);
    const bool chevron = distanceToSegment(x, y, size * 0.29, size * 0.34, size * 0.47, size * 0.50) <= stroke
                         || distanceToSegment(x, y, size * 0.47, size * 0.50, size * 0.29, size * 0.66) <= stroke;
    const bool underscore = distanceToSegment(x, y, size * 0.53, size * 0.66, size * 0.75, size * 0.66) <= stroke;
    return chevron || underscore;
}

[[nodiscard]] IconImage renderIcon(const std::uint32_t size)
{
    constexpr std::uint32_t samplesPerAxis = 4;
    constexpr std::uint32_t samplesPerPixel = samplesPerAxis * samplesPerAxis;
    constexpr Color accent{.blue = 0x5e, .green = 0xc5, .red = 0x22, .alpha = 0xff};
    constexpr Color glyph{.blue = 0x0b, .green = 0x13, .red = 0x07, .alpha = 0xff};

    IconImage image{.size = size, .pixels = {}};
    image.pixels.reserve(static_cast<std::size_t>(size) * size);
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            std::uint32_t backgroundCoverage = 0;
            std::uint32_t glyphCoverage = 0;
            for (std::uint32_t sampleY = 0; sampleY < samplesPerAxis; ++sampleY)
            {
                for (std::uint32_t sampleX = 0; sampleX < samplesPerAxis; ++sampleX)
                {
                    const double samplePositionX =
                        static_cast<double>(x) + ((static_cast<double>(sampleX) + 0.5) / samplesPerAxis);
                    const double samplePositionY =
                        static_cast<double>(y) + ((static_cast<double>(sampleY) + 0.5) / samplesPerAxis);
                    if (insideRoundedSquare(samplePositionX, samplePositionY, static_cast<double>(size)))
                    {
                        ++backgroundCoverage;
                        if (insideTerminalGlyph(samplePositionX, samplePositionY, static_cast<double>(size)))
                        {
                            ++glyphCoverage;
                        }
                    }
                }
            }

            const std::uint32_t accentCoverage = backgroundCoverage - glyphCoverage;
            const auto mixChannel = [accentCoverage, glyphCoverage](const std::uint8_t accentChannel,
                                                                    const std::uint8_t glyphChannel) {
                const std::uint32_t value = (accentCoverage * accentChannel) + (glyphCoverage * glyphChannel);
                return static_cast<std::uint8_t>(value / samplesPerPixel);
            };
            image.pixels.push_back(Color{
                .blue = mixChannel(accent.blue, glyph.blue),
                .green = mixChannel(accent.green, glyph.green),
                .red = mixChannel(accent.red, glyph.red),
                .alpha = static_cast<std::uint8_t>((backgroundCoverage * 0xffU) / samplesPerPixel),
            });
        }
    }
    return image;
}

[[nodiscard]] std::uint32_t imageByteCount(const std::uint32_t size)
{
    constexpr std::uint32_t bitmapInfoHeaderBytes = 40;
    const std::uint32_t colorBytes = size * size * 4U;
    const std::uint32_t maskRowBytes = ((size + 31U) / 32U) * 4U;
    return bitmapInfoHeaderBytes + colorBytes + (maskRowBytes * size);
}

void appendImage(std::vector<std::uint8_t> &bytes, const IconImage &image)
{
    const std::uint32_t colorBytes = image.size * image.size * 4U;
    const std::uint32_t maskRowBytes = ((image.size + 31U) / 32U) * 4U;

    appendDword(bytes, 40);
    appendDword(bytes, image.size);
    appendDword(bytes, image.size * 2U);
    appendWord(bytes, 1);
    appendWord(bytes, 32);
    appendDword(bytes, 0);
    appendDword(bytes, colorBytes);
    appendDword(bytes, 0);
    appendDword(bytes, 0);
    appendDword(bytes, 0);
    appendDword(bytes, 0);

    for (std::uint32_t y = image.size; y > 0; --y)
    {
        const std::size_t rowOffset = static_cast<std::size_t>(y - 1U) * image.size;
        for (std::uint32_t x = 0; x < image.size; ++x)
        {
            const Color &color = image.pixels[rowOffset + x];
            appendByte(bytes, color.blue);
            appendByte(bytes, color.green);
            appendByte(bytes, color.red);
            appendByte(bytes, color.alpha);
        }
    }
    bytes.insert(bytes.end(), static_cast<std::size_t>(maskRowBytes) * image.size, 0);
}

[[nodiscard]] std::vector<std::uint8_t> createIcon()
{
    constexpr std::array<std::uint32_t, 7> sizes{16, 20, 24, 32, 48, 64, 256};
    constexpr std::uint32_t directoryHeaderBytes = 6;
    constexpr std::uint32_t directoryEntryBytes = 16;

    std::vector<IconImage> images;
    images.reserve(sizes.size());
    for (const std::uint32_t size : sizes)
    {
        images.push_back(renderIcon(size));
    }

    std::vector<std::uint8_t> bytes;
    appendWord(bytes, 0);
    appendWord(bytes, 1);
    appendWord(bytes, static_cast<std::uint16_t>(images.size()));

    std::uint32_t imageOffset =
        directoryHeaderBytes + (directoryEntryBytes * static_cast<std::uint32_t>(images.size()));
    for (const IconImage &image : images)
    {
        appendByte(bytes, image.size == 256U ? 0 : static_cast<std::uint8_t>(image.size));
        appendByte(bytes, image.size == 256U ? 0 : static_cast<std::uint8_t>(image.size));
        appendByte(bytes, 0);
        appendByte(bytes, 0);
        appendWord(bytes, 1);
        appendWord(bytes, 32);
        appendDword(bytes, imageByteCount(image.size));
        appendDword(bytes, imageOffset);
        imageOffset += imageByteCount(image.size);
    }

    for (const IconImage &image : images)
    {
        appendImage(bytes, image);
    }
    return bytes;
}

} // namespace

// The helper catches both standard and non-standard exceptions at the process
// boundary. clang-tidy does not model that catch-all for filesystem calls in a
// main function.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(const int argc, const char *const argv[]) noexcept
{
    if (argc != 2)
    {
        std::cerr << "usage: ztermy_windows_icon_generator <output.ico>\n";
        return 1;
    }

    try
    {
        const std::filesystem::path outputPath{argv[1]};
        std::filesystem::create_directories(outputPath.parent_path());
        const std::vector<std::uint8_t> bytes = createIcon();
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
        {
            std::cerr << "could not write " << outputPath << '\n';
            return 1;
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "could not generate the Windows icon: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "could not generate the Windows icon: unknown failure\n";
        return 1;
    }
    return 0;
}
