#include "ui/icons/SvgIconImageProvider.h"

#include <QColor>
#include <QImage>
#include <QTest>

namespace
{

class SvgIconImageProviderTests final : public QObject
{
    Q_OBJECT

private slots:
    void rendersKnownIconAtRequestedSizeAndColor();
    void rendersBrandAssetAtRequestedSize();
    void rejectsInvalidOrUnknownNames();
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

QTEST_GUILESS_MAIN(SvgIconImageProviderTests)

#include "svg_icon_image_provider_tests.moc"
