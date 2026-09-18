#include "core/config/TerminalThemeCatalog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

namespace
{

using ztermy::config::TerminalThemeCatalog;
using ztermy::terminal::TerminalColor;

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(content) == content.size();
}

class TerminalThemeCatalogTests final : public QObject
{
    Q_OBJECT

private slots:
    void parsesAndFormatsHexColors();
    void slugsThemeNames();
    void providesBuiltInThemesAndFallbacks();
    void roundTripsJsonThemes();
    void importsWindowsTerminalSchemes();
    void importsGhosttyThemes();
    void storesAndRemovesCustomThemes();
};

void TerminalThemeCatalogTests::parsesAndFormatsHexColors()
{
    const auto color = ztermy::config::parseHexColor(QStringLiteral(" #1a2B3c "));
    QVERIFY(color.has_value());
    QCOMPARE(*color, (TerminalColor{.red = 0x1A, .green = 0x2B, .blue = 0x3C}));
    QCOMPARE(ztermy::config::hexColor(*color), QStringLiteral("#1A2B3C"));
    QVERIFY(ztermy::config::parseHexColor(QStringLiteral("1A2B3C")).has_value());
    QVERIFY(!ztermy::config::parseHexColor(QStringLiteral("#1A2B")).has_value());
    QVERIFY(!ztermy::config::parseHexColor(QStringLiteral("#GGGGGG")).has_value());
    QVERIFY(ztermy::config::darkBackground({.red = 11, .green = 16, .blue = 23}));
    QVERIFY(!ztermy::config::darkBackground({.red = 253, .green = 246, .blue = 227}));
}

void TerminalThemeCatalogTests::slugsThemeNames()
{
    QCOMPARE(ztermy::config::terminalThemeSlug(QStringLiteral("  Solarized  Dark (v2) ")),
             QStringLiteral("solarized-dark-v2"));
    QCOMPARE(ztermy::config::terminalThemeSlug(QStringLiteral("主题")), QStringLiteral("theme"));
    QCOMPARE(ztermy::config::terminalThemeSlug(QStringLiteral("../x")), QStringLiteral("x"));
}

void TerminalThemeCatalogTests::providesBuiltInThemesAndFallbacks()
{
    QTemporaryDir directory;
    const TerminalThemeCatalog catalog(directory.filePath(QStringLiteral("missing")));
    QCOMPARE(catalog.themes().size(), TerminalThemeCatalog::builtInThemes().size());
    QVERIFY(catalog.themes().size() >= 4);
    for (const auto &theme : catalog.themes())
    {
        QVERIFY(theme.builtIn);
        QCOMPARE(theme.id, ztermy::config::terminalThemeSlug(theme.id));
        QCOMPARE(theme.dark, ztermy::config::darkBackground(theme.scheme.background));
    }
    const auto dark = catalog.find(QString::fromLatin1(TerminalThemeCatalog::defaultDarkThemeId));
    QVERIFY(dark.has_value());
    const auto darkTheme = dark.value_or(ztermy::config::TerminalTheme{});
    QCOMPARE(darkTheme.scheme.background, (TerminalColor{.red = 0x0B, .green = 0x10, .blue = 0x17}));
    QCOMPARE(catalog.resolve(QStringLiteral("unknown"), true).id, darkTheme.id);
    QCOMPARE(catalog.resolve(QStringLiteral("unknown"), false).id,
             QString::fromLatin1(TerminalThemeCatalog::defaultLightThemeId));
    QCOMPARE(catalog.resolve(QStringLiteral("nord"), false).name, QStringLiteral("Nord"));
}

void TerminalThemeCatalogTests::roundTripsJsonThemes()
{
    const auto nord = *TerminalThemeCatalog(QString{}).find(QStringLiteral("nord"));
    const QJsonObject json = ztermy::config::terminalThemeToJson(nord);
    const auto parsed = ztermy::config::terminalThemeFromJson(json);
    QVERIFY(parsed.has_value());
    auto expected = nord;
    expected.builtIn = false;
    QCOMPARE(*parsed, expected);

    auto broken = json;
    broken.insert(QStringLiteral("background"), QStringLiteral("blue"));
    QVERIFY(!ztermy::config::terminalThemeFromJson(broken).has_value());
    broken = json;
    broken.insert(QStringLiteral("id"), QStringLiteral("Not A Slug"));
    QVERIFY(!ztermy::config::terminalThemeFromJson(broken).has_value());
    broken = json;
    broken.remove(QStringLiteral("ansi"));
    QVERIFY(!ztermy::config::terminalThemeFromJson(broken).has_value());
}

void TerminalThemeCatalogTests::importsWindowsTerminalSchemes()
{
    const QByteArray settings = R"({"schemes": [{
        "name": "Campbell Test", "background": "#0C0C0C", "foreground": "#CCCCCC", "cursorColor": "#FFFFFF",
        "selectionBackground": "#FFFFFF",
        "black": "#0C0C0C", "red": "#C50F1F", "green": "#13A10E", "yellow": "#C19C00", "blue": "#0037DA",
        "purple": "#881798", "cyan": "#3A96DD", "white": "#CCCCCC", "brightBlack": "#767676",
        "brightRed": "#E74856", "brightGreen": "#16C60C", "brightYellow": "#F9F1A5", "brightBlue": "#3B78FF",
        "brightPurple": "#B4009E", "brightCyan": "#61D6D6", "brightWhite": "#F2F2F2"}]})";
    const auto themes = ztermy::config::parseWindowsTerminalThemes(settings);
    QVERIFY(themes.has_value());
    QCOMPARE(themes->size(), std::size_t{1});
    const auto &theme = themes->front();
    QCOMPARE(theme.id, QStringLiteral("campbell-test"));
    QCOMPARE(theme.name, QStringLiteral("Campbell Test"));
    QVERIFY(theme.dark);
    QCOMPARE(theme.scheme.ansi[5], (TerminalColor{.red = 0x88, .green = 0x17, .blue = 0x98}));
    QCOMPARE(theme.scheme.cursor, (TerminalColor{.red = 255, .green = 255, .blue = 255}));
    QCOMPARE(theme.selectionBackground, (TerminalColor{.red = 255, .green = 255, .blue = 255}));
    QCOMPARE(theme.selectionForeground, (TerminalColor{.red = 0x0F, .green = 0x17, .blue = 0x2A}));

    const auto missing = ztermy::config::parseWindowsTerminalThemes(R"({"name": "x", "background": "#000000"})");
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error(), ztermy::config::TerminalThemeImportError::invalidColor);
    const auto garbage = ztermy::config::parseWindowsTerminalThemes("not json");
    QVERIFY(!garbage.has_value());
    QCOMPARE(garbage.error(), ztermy::config::TerminalThemeImportError::unsupportedFormat);
}

void TerminalThemeCatalogTests::importsGhosttyThemes()
{
    QByteArray text = "# comment\nbackground = 1d1f21\nforeground = #c5c8c6\ncursor-color = #ff0000\n"
                      "selection-background = #444444\nselection-foreground = #eeeeee\ncursor-style = block\n";
    for (int index = 0; index < 16; ++index)
    {
        text += QStringLiteral("palette = %1=#%2%2%2\n")
                    .arg(index)
                    .arg((index * 16) + 15, 2, 16, QLatin1Char('0'))
                    .toUtf8();
    }
    const auto theme = ztermy::config::parseGhosttyTheme(text, QStringLiteral("Tomorrow Night"));
    QVERIFY(theme.has_value());
    QCOMPARE(theme->id, QStringLiteral("tomorrow-night"));
    QVERIFY(theme->dark);
    QCOMPARE(theme->scheme.background, (TerminalColor{.red = 0x1D, .green = 0x1F, .blue = 0x21}));
    QCOMPARE(theme->scheme.cursor, (TerminalColor{.red = 255, .green = 0, .blue = 0}));
    QCOMPARE(theme->scheme.ansi[15], (TerminalColor{.red = 0xFF, .green = 0xFF, .blue = 0xFF}));
    QCOMPARE(theme->selectionForeground, (TerminalColor{.red = 0xEE, .green = 0xEE, .blue = 0xEE}));

    const auto incomplete =
        ztermy::config::parseGhosttyTheme("background = #000000\nforeground = #ffffff\n", QStringLiteral("x"));
    QVERIFY(!incomplete.has_value());
    QCOMPARE(incomplete.error(), ztermy::config::TerminalThemeImportError::unsupportedFormat);
    const auto badPalette = ztermy::config::parseGhosttyTheme(text + "palette = 16=#000000\n", QStringLiteral("x"));
    QVERIFY(!badPalette.has_value());
    QCOMPARE(badPalette.error(), ztermy::config::TerminalThemeImportError::invalidColor);
}

void TerminalThemeCatalogTests::storesAndRemovesCustomThemes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString themesDirectory = directory.filePath(QStringLiteral("themes"));
    TerminalThemeCatalog catalog(themesDirectory);
    const std::size_t builtInCount = TerminalThemeCatalog::builtInThemes().size();

    auto custom = *catalog.find(QStringLiteral("nord"));
    custom.name = QStringLiteral("Nord Copy");
    const QString sourcePath = directory.filePath(QStringLiteral("nord-copy.json"));
    QVERIFY(writeFile(sourcePath, QJsonDocument(ztermy::config::terminalThemeToJson(custom)).toJson()));
    const auto imported = catalog.importFile(sourcePath);
    QVERIFY(imported.has_value());
    QCOMPARE(imported->size(), std::size_t{1});
    QCOMPARE(imported->front(), QStringLiteral("nord-2"));
    QCOMPARE(catalog.themes().size(), builtInCount + 1);
    const auto stored = catalog.find(QStringLiteral("nord-2"));
    QVERIFY(stored.has_value());
    const auto storedTheme = stored.value_or(ztermy::config::TerminalTheme{});
    QVERIFY(!storedTheme.builtIn);
    QCOMPARE(storedTheme.name, QStringLiteral("Nord Copy"));
    QVERIFY(QFile::exists(QDir(themesDirectory).filePath(QStringLiteral("nord-2.json"))));

    const auto again = catalog.importFile(sourcePath);
    QVERIFY(again.has_value());
    QCOMPARE(again->front(), QStringLiteral("nord-3"));

    const TerminalThemeCatalog reloaded(themesDirectory);
    QCOMPARE(reloaded.themes().size(), builtInCount + 2);
    QVERIFY(reloaded.find(QStringLiteral("nord-3")).has_value());

    QVERIFY(!catalog.remove(QStringLiteral("nord")));
    QVERIFY(!catalog.remove(QStringLiteral("missing")));
    QVERIFY(catalog.remove(QStringLiteral("nord-2")));
    QVERIFY(!catalog.find(QStringLiteral("nord-2")).has_value());
    QVERIFY(!QFile::exists(QDir(themesDirectory).filePath(QStringLiteral("nord-2.json"))));

    const auto unreadable = catalog.importFile(directory.filePath(QStringLiteral("missing.json")));
    QVERIFY(!unreadable.has_value());
    QCOMPARE(unreadable.error(), ztermy::config::TerminalThemeImportError::unreadable);
}

} // namespace

QTEST_GUILESS_MAIN(TerminalThemeCatalogTests)

#include "terminal_theme_catalog_tests.moc"
