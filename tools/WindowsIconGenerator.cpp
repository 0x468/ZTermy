#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <QSvgRenderer>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

constexpr std::array<std::uint32_t, 9> iconSizes{16, 20, 24, 32, 40, 48, 64, 128, 256};

struct IconImage final
{
    std::uint32_t size;
    QByteArray png;
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

[[nodiscard]] QImage renderSvg(const QString &path, const std::uint32_t size)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid())
    {
        throw std::runtime_error(QStringLiteral("could not load SVG source: %1").arg(path).toStdString());
    }

    QImage image(static_cast<int>(size), static_cast<int>(size), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter);
    if (!painter.end())
    {
        throw std::runtime_error(QStringLiteral("could not render SVG source: %1").arg(path).toStdString());
    }
    return image;
}

[[nodiscard]] QByteArray encodePng(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    {
        throw std::runtime_error("could not encode PNG icon layer");
    }
    return png;
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(contents) != contents.size())
    {
        throw std::runtime_error(QStringLiteral("could not write %1").arg(path).toStdString());
    }
}

void writeFile(const QString &path, const std::vector<std::uint8_t> &contents)
{
    QFile file(path);
    const auto byteCount = static_cast<qint64>(contents.size());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(reinterpret_cast<const char *>(contents.data()), byteCount) != byteCount)
    {
        throw std::runtime_error(QStringLiteral("could not write %1").arg(path).toStdString());
    }
}

[[nodiscard]] std::vector<IconImage> renderIconLayers(const QString &fullSource, const QString &smallSource,
                                                      const QString &pngDirectory)
{
    if (!QDir().mkpath(pngDirectory))
    {
        throw std::runtime_error(QStringLiteral("could not create %1").arg(pngDirectory).toStdString());
    }

    std::vector<IconImage> images;
    images.reserve(iconSizes.size());
    for (const std::uint32_t size : iconSizes)
    {
        const QString &source = size <= 20U ? smallSource : fullSource;
        IconImage image{.size = size, .png = encodePng(renderSvg(source, size))};
        writeFile(QDir(pngDirectory).filePath(QStringLiteral("ztermy-%1.png").arg(size)), image.png);
        images.push_back(std::move(image));
    }
    return images;
}

[[nodiscard]] std::vector<std::uint8_t> createIcon(const std::vector<IconImage> &images)
{
    constexpr std::uint32_t directoryHeaderBytes = 6;
    constexpr std::uint32_t directoryEntryBytes = 16;

    std::vector<std::uint8_t> bytes;
    appendWord(bytes, 0);
    appendWord(bytes, 1);
    appendWord(bytes, static_cast<std::uint16_t>(images.size()));

    std::uint32_t imageOffset =
        directoryHeaderBytes + (directoryEntryBytes * static_cast<std::uint32_t>(images.size()));
    for (const IconImage &image : images)
    {
        if (image.png.size() < 0 || std::cmp_greater(image.png.size(), std::numeric_limits<std::uint32_t>::max()))
        {
            throw std::runtime_error("PNG icon layer is too large for an ICO directory entry");
        }
        const auto imageByteCount = static_cast<std::uint32_t>(image.png.size());
        appendByte(bytes, image.size == 256U ? 0 : static_cast<std::uint8_t>(image.size));
        appendByte(bytes, image.size == 256U ? 0 : static_cast<std::uint8_t>(image.size));
        appendByte(bytes, 0);
        appendByte(bytes, 0);
        appendWord(bytes, 1);
        appendWord(bytes, 32);
        appendDword(bytes, imageByteCount);
        appendDword(bytes, imageOffset);
        imageOffset += imageByteCount;
    }

    for (const IconImage &image : images)
    {
        for (const char byte : image.png)
        {
            appendByte(bytes, static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));
        }
    }
    return bytes;
}

} // namespace

// The helper catches both standard and non-standard exceptions at the process
// boundary. clang-tidy does not model that catch-all for Qt file operations in
// a main function.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[]) noexcept
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() != 5)
    {
        std::cerr << "usage: ztermy_windows_icon_generator <full.svg> <small.svg> <output.ico> <png-directory>\n";
        return 1;
    }

    try
    {
        const QString &fullSource = arguments.at(1);
        const QString &smallSource = arguments.at(2);
        const QString &outputPath = arguments.at(3);
        const QString &pngDirectory = arguments.at(4);
        const QFileInfo outputInfo(outputPath);
        if (!QDir().mkpath(outputInfo.absolutePath()))
        {
            throw std::runtime_error(
                QStringLiteral("could not create %1").arg(outputInfo.absolutePath()).toStdString());
        }
        const std::vector<IconImage> images = renderIconLayers(fullSource, smallSource, pngDirectory);
        writeFile(outputPath, createIcon(images));
    }
    catch (const std::exception &error)
    {
        std::cerr << "could not generate ztermy branding assets: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "could not generate ztermy branding assets: unknown failure\n";
        return 1;
    }
    return 0;
}
