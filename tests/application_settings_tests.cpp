#include "core/config/ApplicationSettings.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

class ApplicationSettingsTests final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileUsesDefaults();
    void savesAndLoadsEveryPreference();
    void migratesLegacyWindowOpacityAndNoneBackdrop();
    void migratesMaterialSchemaWithOpaqueTerminalDefault();
    void rejectsMalformedUnsupportedAndIncompleteDocuments();
    void rejectsOutOfRangeValues();
    void createsMissingParentDirectory();
};

void ApplicationSettingsTests::missingFileUsesDefaults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ztermy::config::ApplicationSettingsStore store(directory.filePath(QStringLiteral("settings.json")));
    const auto loaded = store.load();

    QVERIFY(loaded);
    QCOMPARE(*loaded, ztermy::config::ApplicationSettings{});
}

void ApplicationSettingsTests::savesAndLoadsEveryPreference()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ztermy::config::ApplicationSettings expected{
        .theme = ztermy::config::ThemePreference::light,
        .backdropOpacity = 0.82,
        .backdrop = ztermy::config::BackdropPreference::micaAlt,
        .terminalFontFamily = QStringLiteral("Cascadia Code"),
        .terminalFontSize = 18,
        .terminalBackgroundOpacity = 0.45,
        .cursor = ztermy::config::CursorPreference::bar,
        .cursorBlink = false,
        .copyOnSelect = true,
        .confirmMultilinePaste = false,
    };
    const ztermy::config::ApplicationSettingsStore store(directory.filePath(QStringLiteral("settings.json")));

    QVERIFY(store.save(expected));
    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, expected);
}

void ApplicationSettingsTests::migratesLegacyWindowOpacityAndNoneBackdrop()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({
        "version": 1,
        "theme": "dark",
        "windowOpacity": 0.75,
        "backdrop": "none",
        "terminalFontFamily": "Cascadia Mono",
        "terminalFontSize": 14,
        "cursor": "terminal",
        "cursorBlink": true,
        "copyOnSelect": false,
        "confirmMultilinePaste": true
    })")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(loaded->backdropOpacity, 0.75);
    QCOMPARE(loaded->backdrop, ztermy::config::BackdropPreference::transparent);
    QCOMPARE(loaded->terminalBackgroundOpacity, 1.0);

    QVERIFY(store.save(*loaded));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray persisted = saved.readAll();
    QVERIFY(persisted.contains(QByteArrayLiteral("\"version\": 3")));
    QVERIFY(persisted.contains(QByteArrayLiteral("\"backdropOpacity\": 0.75")));
    QVERIFY(persisted.contains(QByteArrayLiteral("\"backdrop\": \"transparent\"")));
    QVERIFY(!persisted.contains(QByteArrayLiteral("windowOpacity")));
}

void ApplicationSettingsTests::migratesMaterialSchemaWithOpaqueTerminalDefault()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({
        "version": 2,
        "theme": "light",
        "backdropOpacity": 0.5,
        "backdrop": "mica",
        "terminalFontFamily": "Cascadia Mono",
        "terminalFontSize": 16,
        "cursor": "bar",
        "cursorBlink": false,
        "copyOnSelect": true,
        "confirmMultilinePaste": false
    })")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(loaded->terminalBackgroundOpacity, 1.0);
    QCOMPARE(loaded->terminalFontSize, 16);
}

void ApplicationSettingsTests::rejectsMalformedUnsupportedAndIncompleteDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral("{not-json")));
    const auto malformed = store.load();
    QVERIFY(!malformed);
    QCOMPARE(malformed.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":4})")));
    const auto unsupported = store.load();
    QVERIFY(!unsupported);
    QCOMPARE(unsupported.error(), ztermy::config::ApplicationSettingsStoreError::unsupportedVersion);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":1,"theme":"dark"})")));
    const auto incomplete = store.load();
    QVERIFY(!incomplete);
    QCOMPARE(incomplete.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);
}

void ApplicationSettingsTests::rejectsOutOfRangeValues()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::config::ApplicationSettingsStore store(directory.filePath(QStringLiteral("settings.json")));

    auto invalidOpacity = ztermy::config::ApplicationSettings{};
    invalidOpacity.backdropOpacity = -0.01;
    const auto opacityResult = store.save(invalidOpacity);
    QVERIFY(!opacityResult);
    QCOMPARE(opacityResult.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);

    auto invalidFontSize = ztermy::config::ApplicationSettings{};
    invalidFontSize.terminalFontSize = 33;
    const auto fontSizeResult = store.save(invalidFontSize);
    QVERIFY(!fontSizeResult);
    QCOMPARE(fontSizeResult.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);

    auto invalidTerminalOpacity = ztermy::config::ApplicationSettings{};
    invalidTerminalOpacity.terminalBackgroundOpacity = 1.01;
    const auto terminalOpacityResult = store.save(invalidTerminalOpacity);
    QVERIFY(!terminalOpacityResult);
    QCOMPARE(terminalOpacityResult.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);
}

void ApplicationSettingsTests::createsMissingParentDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nested/config/settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(store.save(ztermy::config::ApplicationSettings{}));
    QVERIFY(QFile::exists(path));
}

QTEST_APPLESS_MAIN(ApplicationSettingsTests)

#include "application_settings_tests.moc"
