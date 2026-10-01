#include "ui/icons/SvgIconImageProvider.h"

#include <QColor>
#include <QDir>
#include <QImage>
#include <QTest>

#include <cstddef>

namespace
{

class SvgIconImageProviderTests final : public QObject
{
    Q_OBJECT

private slots:
    void rendersKnownIconAtRequestedSizeAndColor();
    void rendersAllInterfaceIconsWithThemeColors();
    void rendersBrandAssetAtRequestedSize();
    void rejectsInvalidOrUnknownNames();
    void reusesRenderedIconsPerIdAndSize();
};

void SvgIconImageProviderTests::rendersKnownIconAtRequestedSizeAndColor()
{
    ztermy::ui::SvgIconImageProvider provider(QStringLiteral(ZTERMY_TEST_ICON_DIRECTORY));
    QSize renderedSize;

    const QImage image = provider.requestImage(QStringLiteral("search/12ab34"), &renderedSize, QSize{40, 36});

    QCOMPARE(renderedSize, QSize(40, 36));
    QCOMPARE(image.size(), renderedSize);
    QVERIFY(!image.isNull());

    bool foundColoredPixel = false;
    for (int y = 0; y < image.height() && !foundColoredPixel; ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 192 && pixel.green() > pixel.red() && pixel.green() > pixel.blue())
            {
                foundColoredPixel = true;
                break;
            }
        }
    }
    QVERIFY(foundColoredPixel);
}

void SvgIconImageProviderTests::rendersAllInterfaceIconsWithThemeColors()
{
    ztermy::ui::SvgIconImageProvider provider(QStringLiteral(ZTERMY_TEST_ICON_DIRECTORY));
    const auto files =
        QDir(QStringLiteral(ZTERMY_TEST_ICON_DIRECTORY)).entryList({QStringLiteral("*.svg")}, QDir::Files);
    QCOMPARE(files.size(), 91);
    for (const auto &file : files)
        for (const auto *color : {"eeeeee", "222222"})
            for (const int size : {16, 20, 30, 40})
            {
                const auto image =
                    provider.requestImage(QStringLiteral("%1/%2").arg(file.chopped(4), color), nullptr, {size, size});
                QVERIFY2(!image.isNull(), qPrintable(file));
                int painted = 0;
                const QColor expected(QStringLiteral("#%1").arg(color));
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                    {
                        const auto pixel = image.pixelColor(x, y);
                        if (pixel.alpha() > 128)
                        {
                            ++painted;
                            const auto detail = QStringLiteral("%1 size=%2 at %3,%4 actual=%5 expected=%6")
                                                    .arg(file)
                                                    .arg(size)
                                                    .arg(x)
                                                    .arg(y)
                                                    .arg(pixel.name(QColor::HexArgb), expected.name());
                            // Compare premultiplied channels: unpremultiplying
                            // half-covered antialiased edges amplifies rounding.
                            const QRgb actual = image.pixel(x, y);
                            const QRgb reference =
                                qPremultiply(qRgba(expected.red(), expected.green(), expected.blue(), pixel.alpha()));
                            QVERIFY2(qAbs(qRed(actual) - qRed(reference)) <= 2, qPrintable(detail));
                            QVERIFY2(qAbs(qGreen(actual) - qGreen(reference)) <= 2, qPrintable(detail));
                            QVERIFY2(qAbs(qBlue(actual) - qBlue(reference)) <= 2, qPrintable(detail));
                        }
                    }
                QVERIFY2(painted > 0, qPrintable(file));
            }
}

void SvgIconImageProviderTests::rendersBrandAssetAtRequestedSize()
{
    ztermy::ui::SvgIconImageProvider provider(QStringLiteral(ZTERMY_TEST_BRANDING_DIRECTORY));
    QSize renderedSize;

    const QImage image =
        provider.requestImage(QStringLiteral("ztermy-app-icon/000002/light/regular"), &renderedSize, QSize{336, 336});

    QCOMPARE(renderedSize, QSize(336, 336));
    QCOMPARE(image.size(), renderedSize);
    QVERIFY(!image.isNull());
    for (const QPoint sample : {QPoint{84, 84}, QPoint{252, 84}, QPoint{84, 252}, QPoint{252, 252}})
    {
        QVERIFY(image.pixelColor(sample).alpha() > 192);
    }
}

void SvgIconImageProviderTests::rejectsInvalidOrUnknownNames()
{
    ztermy::ui::SvgIconImageProvider provider(QStringLiteral(ZTERMY_TEST_ICON_DIRECTORY));

    QVERIFY(provider.requestImage(QStringLiteral("../search/ffffff"), nullptr, QSize{20, 20}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("does-not-exist/ffffff"), nullptr, QSize{20, 20}).isNull());
}

} // namespace

void SvgIconImageProviderTests::reusesRenderedIconsPerIdAndSize()
{
    ztermy::ui::SvgIconImageProvider provider(QStringLiteral(ZTERMY_TEST_ICON_DIRECTORY));
    QSize renderedSize;

    const QImage first = provider.requestImage(QStringLiteral("search/12ab34"), &renderedSize, QSize{40, 36});
    QCOMPARE(provider.cachedImageCount(), std::size_t{1});
    const QImage again = provider.requestImage(QStringLiteral("search/12ab34"), &renderedSize, QSize{40, 36});
    QCOMPARE(provider.cachedImageCount(), std::size_t{1});
    QCOMPARE(renderedSize, QSize(40, 36));
    QCOMPARE(again, first);

    // A different size or color is a distinct raster.
    const QImage larger = provider.requestImage(QStringLiteral("search/12ab34"), &renderedSize, QSize{80, 72});
    QCOMPARE(larger.size(), QSize(80, 72));
    QCOMPARE(provider.cachedImageCount(), std::size_t{2});
    const QImage recolored = provider.requestImage(QStringLiteral("search/ff0000"), &renderedSize, QSize{40, 36});
    QVERIFY(recolored != first);
    QCOMPARE(provider.cachedImageCount(), std::size_t{3});

    // Rejected names are not cached.
    QVERIFY(provider.requestImage(QStringLiteral("Missing Icon"), &renderedSize, QSize{40, 36}).isNull());
    QCOMPARE(provider.cachedImageCount(), std::size_t{3});
}

QTEST_GUILESS_MAIN(SvgIconImageProviderTests)

#include "svg_icon_image_provider_tests.moc"
