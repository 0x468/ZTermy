#include "application/FontCatalog.h"

#include <QTest>

class FontCatalogTests final : public QObject
{
    Q_OBJECT

private slots:
    void exposesSystemAndMonospacedFonts();
    void resolvesUiFontFallbacks();
};

void FontCatalogTests::exposesSystemAndMonospacedFonts()
{
    const ztermy::FontCatalog catalog;

    QVERIFY(!catalog.systemUiFamily().isEmpty());
    QVERIFY(!catalog.allFamilies().isEmpty());
    QVERIFY(!catalog.monospacedFamilies().isEmpty());
    for (const QString &family : catalog.monospacedFamilies())
    {
        QVERIFY(catalog.allFamilies().contains(family));
        QVERIFY(catalog.isMonospaced(family));
    }
}

void FontCatalogTests::resolvesUiFontFallbacks()
{
    const ztermy::FontCatalog catalog;

    QCOMPARE(catalog.effectiveUiFamily({}), catalog.systemUiFamily());
    QCOMPARE(catalog.effectiveUiFamily(QStringLiteral("ztermy-font-that-does-not-exist")), catalog.systemUiFamily());
    const QString installed = catalog.allFamilies().constFirst();
    QCOMPARE(catalog.effectiveUiFamily(installed), installed);
}

QTEST_MAIN(FontCatalogTests)

#include "font_catalog_tests.moc"
