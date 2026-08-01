#include "application/LocalizationManager.h"

#include <QTest>

Q_DECLARE_METATYPE(ztermy::config::LanguagePreference)

class LocalizationManagerTests final : public QObject
{
    Q_OBJECT

private slots:
    void preservesExplicitPreferences();
    void resolvesSystemLocale_data();
    void resolvesSystemLocale();
};

void LocalizationManagerTests::preservesExplicitPreferences()
{
    using ztermy::config::LanguagePreference;

    QCOMPARE(ztermy::LocalizationManager::resolve(LanguagePreference::english, QLocale{QLocale::Chinese}),
             LanguagePreference::english);
    QCOMPARE(ztermy::LocalizationManager::resolve(LanguagePreference::simplifiedChinese, QLocale{QLocale::English}),
             LanguagePreference::simplifiedChinese);
}

void LocalizationManagerTests::resolvesSystemLocale_data()
{
    using ztermy::config::LanguagePreference;

    QTest::addColumn<QLocale>("locale");
    QTest::addColumn<LanguagePreference>("expected");

    QTest::newRow("mainland-china") << QLocale{QLocale::Chinese, QLocale::China}
                                    << LanguagePreference::simplifiedChinese;
    QTest::newRow("singapore") << QLocale{QLocale::Chinese, QLocale::Singapore}
                               << LanguagePreference::simplifiedChinese;
    QTest::newRow("taiwan") << QLocale{QLocale::Chinese, QLocale::Taiwan} << LanguagePreference::english;
    QTest::newRow("english") << QLocale{QLocale::English, QLocale::UnitedStates} << LanguagePreference::english;
}

void LocalizationManagerTests::resolvesSystemLocale()
{
    QFETCH(QLocale, locale);
    QFETCH(ztermy::config::LanguagePreference, expected);

    QCOMPARE(ztermy::LocalizationManager::resolve(ztermy::config::LanguagePreference::system, locale), expected);
}

QTEST_MAIN(LocalizationManagerTests)

#include "localization_manager_tests.moc"
