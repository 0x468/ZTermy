#include "core/config/ApplicationSettings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <array>

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
    void roundTripsEveryAiProviderPreference();
    void migratesLegacyWindowOpacityAndNoneBackdrop();
    void migratesMaterialSchemaWithOpaqueTerminalDefault();
    void migratesTerminalAppearanceSchemaWithDefaultAccent();
    void migratesCredentialSchemaWithSystemLanguage();
    void migratesLocalizationSchemaWithFontDefaults();
    void migratesFontOptionsSchema();
    void migratesEveryIntermediateSchema();
    void migratesAiProviderSchemaWithPermissionDefault();
    void migratesRecentPermissionSchemasAndAllowsResave();
    void migratesRetiredExternalAgentSchemaWithoutLosingProvider();
    void migratesNativeChatGptSchemaWithSystemAiProxy();
    void rejectsMalformedUnsupportedAndIncompleteDocuments();
    void rejectsOutOfRangeValues();
    void recoversLastKnownGoodSettings();
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
        .accent = ztermy::config::AccentPreference::custom,
        .customAccent = QStringLiteral("#3366CC"),
        .uiFontFamily = QStringLiteral("Microsoft YaHei UI"),
        .terminalFontFamily = QStringLiteral("Cascadia Code"),
        .terminalFontSize = 18,
        .showAllTerminalFonts = true,
        .terminalLigatures = false,
        .terminalBackgroundOpacity = 0.45,
        .cursor = ztermy::config::CursorPreference::bar,
        .cursorBlink = false,
        .copyOnSelect = true,
        .confirmMultilinePaste = false,
        .sftpShowHiddenFiles = true,
        .sftpConfirmDelete = false,
        .closeToTray = true,
        .credentialStorage = ztermy::config::CredentialStoragePreference::portable,
        .language = ztermy::config::LanguagePreference::simplifiedChinese,
        .shortcutOverrides = {{QStringLiteral("terminal.find"), QStringLiteral("Ctrl+Alt+F")}},
        .aiProvider = ztermy::config::AiProviderPreference::openAiCompatible,
        .aiBaseUrl = QStringLiteral("https://gateway.example.test/v1"),
        .aiEndpointPath = QStringLiteral("/chat/completions"),
        .aiModel = QStringLiteral("terminal-model"),
        .aiCredentialReference = QStringLiteral("ai-custom"),
        .aiAutomaticContext = false,
        .aiPermission = ztermy::config::AiPermissionPreference::yolo,
        .aiConversationHistoryEnabled = true,
        .aiDebugTraceEnabled = true,
        .aiReasoning = ztermy::config::AiReasoningPreference::maximum,
        .aiProxy = ztermy::config::AiProxyPreference::custom,
        .aiProxyUrl = QStringLiteral("socks5://127.0.0.1:1080"),
        .aiProxyUsername = QStringLiteral("proxy-user"),
    };
    const ztermy::config::ApplicationSettingsStore store(directory.filePath(QStringLiteral("settings.json")));

    QVERIFY(store.save(expected));
    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, expected);
}

void ApplicationSettingsTests::roundTripsEveryAiProviderPreference()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::config::ApplicationSettingsStore store(directory.filePath(QStringLiteral("settings.json")));

    const std::array providers{
        ztermy::config::AiProviderPreference::openAiResponses,
        ztermy::config::AiProviderPreference::openAiChatGpt,
        ztermy::config::AiProviderPreference::anthropic,
        ztermy::config::AiProviderPreference::deepSeek,
        ztermy::config::AiProviderPreference::kimi,
        ztermy::config::AiProviderPreference::zai,
        ztermy::config::AiProviderPreference::gemini,
        ztermy::config::AiProviderPreference::openRouter,
        ztermy::config::AiProviderPreference::qwen,
        ztermy::config::AiProviderPreference::ollama,
        ztermy::config::AiProviderPreference::openAiCompatible,
    };
    for (const auto provider : providers)
    {
        auto expected = ztermy::config::ApplicationSettings{};
        expected.aiProvider = provider;
        QVERIFY(store.save(expected));
        const auto loaded = store.load();
        QVERIFY(loaded);
        QCOMPARE(loaded->aiProvider, provider);
    }
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
    QVERIFY(!loaded->sftpShowHiddenFiles);
    QVERIFY(loaded->sftpConfirmDelete);

    QVERIFY(store.save(*loaded));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray persisted = saved.readAll();
    QVERIFY(persisted.contains(QByteArrayLiteral("\"version\": 23")));
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

void ApplicationSettingsTests::migratesTerminalAppearanceSchemaWithDefaultAccent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({
        "version": 3,
        "theme": "dark",
        "backdropOpacity": 0.65,
        "backdrop": "acrylic",
        "terminalFontFamily": "Cascadia Mono",
        "terminalFontSize": 14,
        "terminalBackgroundOpacity": 0.4,
        "cursor": "terminal",
        "cursorBlink": true,
        "copyOnSelect": false,
        "confirmMultilinePaste": true
    })")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(loaded->accent, ztermy::config::AccentPreference::ztermy);
    QCOMPARE(loaded->customAccent, QStringLiteral("#22C55E"));
    QCOMPARE(loaded->terminalBackgroundOpacity, 0.4);
    QCOMPARE(loaded->credentialStorage, ztermy::config::CredentialStoragePreference::automatic);
}

void ApplicationSettingsTests::migratesCredentialSchemaWithSystemLanguage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({
        "version": 5,
        "theme": "dark",
        "backdropOpacity": 1.0,
        "backdrop": "acrylic",
        "accent": "ztermy",
        "customAccent": "#22C55E",
        "terminalFontFamily": "Cascadia Mono",
        "terminalFontSize": 14,
        "terminalBackgroundOpacity": 1.0,
        "cursor": "terminal",
        "cursorBlink": true,
        "copyOnSelect": false,
        "confirmMultilinePaste": true,
        "credentialStorage": "portable"
    })")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(loaded->credentialStorage, ztermy::config::CredentialStoragePreference::portable);
    QCOMPARE(loaded->language, ztermy::config::LanguagePreference::system);
}

void ApplicationSettingsTests::migratesLocalizationSchemaWithFontDefaults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({
        "version": 6,
        "theme": "dark",
        "backdropOpacity": 1.0,
        "backdrop": "acrylic",
        "accent": "ztermy",
        "customAccent": "#22C55E",
        "terminalFontFamily": "Cascadia Mono",
        "terminalFontSize": 14,
        "terminalBackgroundOpacity": 1.0,
        "cursor": "terminal",
        "cursorBlink": true,
        "copyOnSelect": false,
        "confirmMultilinePaste": true,
        "credentialStorage": "system",
        "language": "zh_CN"
    })")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(loaded->language, ztermy::config::LanguagePreference::simplifiedChinese);
    QVERIFY(loaded->uiFontFamily.isEmpty());
    QVERIFY(!loaded->showAllTerminalFonts);
    QVERIFY(loaded->terminalLigatures);
}

void ApplicationSettingsTests::migratesFontOptionsSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({
        "version": 7,
        "theme": "dark",
        "backdropOpacity": 1.0,
        "backdrop": "acrylic",
        "accent": "ztermy",
        "customAccent": "#22C55E",
        "uiFontFamily": "",
        "terminalFontFamily": "Cascadia Mono",
        "terminalFontSize": 14,
        "showAllTerminalFonts": false,
        "terminalLigatures": true,
        "terminalBackgroundOpacity": 1.0,
        "cursor": "terminal",
        "cursorBlink": true,
        "copyOnSelect": false,
        "confirmMultilinePaste": true,
        "credentialStorage": "system",
        "language": "system"
    })")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(loaded->terminalFontFamily, QStringLiteral("Cascadia Mono"));
}

void ApplicationSettingsTests::migratesEveryIntermediateSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto base = [](const int version) {
        QJsonObject root{
            {QStringLiteral("version"), version},
            {QStringLiteral("theme"), QStringLiteral("dark")},
            {QStringLiteral("backdropOpacity"), 0.8},
            {QStringLiteral("backdrop"), QStringLiteral("acrylic")},
            {QStringLiteral("accent"), QStringLiteral("custom")},
            {QStringLiteral("customAccent"), QStringLiteral("#3366CC")},
            {QStringLiteral("uiFontFamily"), QStringLiteral("Microsoft YaHei UI")},
            {QStringLiteral("terminalFontFamily"), QStringLiteral("Cascadia Mono")},
            {QStringLiteral("terminalFontSize"), 15},
            {QStringLiteral("showAllTerminalFonts"), false},
            {QStringLiteral("terminalLigatures"), true},
            {QStringLiteral("terminalBackgroundOpacity"), 0.7},
            {QStringLiteral("cursor"), QStringLiteral("bar")},
            {QStringLiteral("cursorBlink"), true},
            {QStringLiteral("copyOnSelect"), false},
            {QStringLiteral("confirmMultilinePaste"), true},
            {QStringLiteral("credentialStorage"), QStringLiteral("system")},
            {QStringLiteral("language"), QStringLiteral("zh_CN")},
        };
        if (version >= 9)
        {
            root.insert(QStringLiteral("shortcutOverrides"),
                        QJsonObject{{QStringLiteral("terminal.find"), QStringLiteral("Ctrl+Alt+F")}});
        }
        if (version >= 10)
        {
            root.insert(QStringLiteral("sftpShowHiddenFiles"), false);
            root.insert(QStringLiteral("sftpConfirmDelete"), true);
        }
        return root;
    };

    for (const int version : {4, 8, 9, 10})
    {
        const QString path = directory.filePath(QStringLiteral("settings-v%1.json").arg(version));
        QVERIFY(writeFile(path, QJsonDocument(base(version)).toJson(QJsonDocument::Compact)));
        const auto loaded = ztermy::config::ApplicationSettingsStore(path).load();
        QVERIFY2(loaded, qPrintable(QStringLiteral("Schema %1 did not load").arg(version)));
        QCOMPARE(loaded->terminalFontSize, 15);
        QCOMPARE(loaded->accent, ztermy::config::AccentPreference::custom);
        QCOMPARE(loaded->credentialStorage, version >= 5 ? ztermy::config::CredentialStoragePreference::system
                                                         : ztermy::config::CredentialStoragePreference::automatic);
        QCOMPARE(loaded->language, version >= 6 ? ztermy::config::LanguagePreference::simplifiedChinese
                                                : ztermy::config::LanguagePreference::system);
        QCOMPARE(loaded->shortcutOverrides.size(), version >= 9 ? 1 : 0);
        QVERIFY(!loaded->sftpShowHiddenFiles);
        QVERIFY(loaded->sftpConfirmDelete);
        QCOMPARE(loaded->aiProvider, ztermy::config::AiProviderPreference::openAiResponses);
        QCOMPARE(loaded->aiBaseUrl, QStringLiteral("https://api.openai.com/v1"));
        QVERIFY(loaded->aiModel.isEmpty());
        QCOMPARE(loaded->aiCredentialReference, QStringLiteral("ai-default"));
        QVERIFY(!loaded->aiAutomaticContext);
        QCOMPARE(loaded->aiPermission, ztermy::config::AiPermissionPreference::ask);
        QVERIFY(loaded->aiConversationHistoryEnabled);
    }
}

void ApplicationSettingsTests::migratesAiProviderSchemaWithPermissionDefault()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);
    auto legacy = ztermy::config::ApplicationSettings{};
    legacy.aiProvider = ztermy::config::AiProviderPreference::ollama;
    legacy.aiBaseUrl = QStringLiteral("http://127.0.0.1:11434");
    legacy.aiEndpointPath = QStringLiteral("/api/chat");
    legacy.aiModel = QStringLiteral("qwen3");
    QVERIFY(store.save(legacy));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    root.insert(QStringLiteral("version"), 11);
    root.remove(QStringLiteral("aiPermission"));
    QVERIFY(writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    const auto loaded = store.load();
    QVERIFY2(loaded, qPrintable(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact))));
    QCOMPARE(loaded->aiProvider, ztermy::config::AiProviderPreference::ollama);
    QCOMPARE(loaded->aiModel, QStringLiteral("qwen3"));
    QCOMPARE(loaded->aiPermission, ztermy::config::AiPermissionPreference::ask);
    QVERIFY(loaded->aiConversationHistoryEnabled);
}

void ApplicationSettingsTests::migratesRecentPermissionSchemasAndAllowsResave()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    for (const int version : {17, 18})
    {
        const QString path = directory.filePath(QStringLiteral("settings-v%1.json").arg(version));
        const ztermy::config::ApplicationSettingsStore store(path);
        auto expected = ztermy::config::ApplicationSettings{};
        expected.theme = ztermy::config::ThemePreference::light;
        expected.aiProvider = ztermy::config::AiProviderPreference::openAiCompatible;
        expected.aiBaseUrl = QStringLiteral("https://gateway.example.test/v1");
        expected.aiModel = QStringLiteral("saved-model");
        QVERIFY(store.save(expected));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        root.insert(QStringLiteral("version"), version);
        root.insert(QStringLiteral("aiPermission"), QStringLiteral("edit"));
        QVERIFY(writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Compact)));

        const auto loaded = store.load();
        QVERIFY2(loaded, qPrintable(QStringLiteral("Schema %1 did not load").arg(version)));
        QCOMPARE(loaded->theme, ztermy::config::ThemePreference::light);
        QCOMPARE(loaded->aiModel, QStringLiteral("saved-model"));
        QCOMPARE(loaded->aiPermission, ztermy::config::AiPermissionPreference::ask);

        auto updated = *loaded;
        updated.terminalFontSize = 16;
        QVERIFY2(store.save(updated), qPrintable(QStringLiteral("Schema %1 could not be upgraded").arg(version)));

        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto persisted = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(persisted.value(QStringLiteral("version")).toInt(), 23);
        QCOMPARE(persisted.value(QStringLiteral("aiPermission")).toString(), QStringLiteral("ask"));
        QCOMPARE(persisted.value(QStringLiteral("terminalFontSize")).toInt(), 16);
    }
}

void ApplicationSettingsTests::migratesRetiredExternalAgentSchemaWithoutLosingProvider()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    auto expected = ztermy::config::ApplicationSettings{};
    expected.aiProvider = ztermy::config::AiProviderPreference::openAiCompatible;
    expected.aiBaseUrl = QStringLiteral("https://gateway.example.test/v1");
    expected.aiModel = QStringLiteral("saved-model");
    expected.aiCredentialReference = QStringLiteral("ai-default");
    expected.aiPermission = ztermy::config::AiPermissionPreference::automatic;
    expected.aiDebugTraceEnabled = true;
    QVERIFY(store.save(expected));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    root.insert(QStringLiteral("version"), 20);
    root.insert(QStringLiteral("aiAgent"), QStringLiteral("retired-value"));
    QVERIFY(writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, expected);

    QVERIFY(store.save(*loaded));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto persisted = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(persisted.value(QStringLiteral("version")).toInt(), 23);
    QVERIFY(!persisted.contains(QStringLiteral("aiAgent")));
    QCOMPARE(persisted.value(QStringLiteral("aiBaseUrl")).toString(), expected.aiBaseUrl);
    QCOMPARE(persisted.value(QStringLiteral("aiModel")).toString(), expected.aiModel);
    QCOMPARE(persisted.value(QStringLiteral("aiCredentialReference")).toString(), expected.aiCredentialReference);
}

void ApplicationSettingsTests::migratesNativeChatGptSchemaWithSystemAiProxy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    auto expected = ztermy::config::ApplicationSettings{};
    expected.aiProvider = ztermy::config::AiProviderPreference::openAiChatGpt;
    expected.aiModel = QStringLiteral("gpt-5.2-codex");
    QVERIFY(store.save(expected));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    root.insert(QStringLiteral("version"), 21);
    root.remove(QStringLiteral("aiProxy"));
    root.remove(QStringLiteral("aiProxyUrl"));
    root.remove(QStringLiteral("aiProxyUsername"));
    QVERIFY(writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(loaded->aiProvider, ztermy::config::AiProviderPreference::openAiChatGpt);
    QCOMPARE(loaded->aiModel, QStringLiteral("gpt-5.2-codex"));
    QCOMPARE(loaded->aiProxy, ztermy::config::AiProxyPreference::system);
    QVERIFY(loaded->aiProxyUrl.isEmpty());
    QVERIFY(loaded->aiProxyUsername.isEmpty());

    QVERIFY(store.save(*loaded));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto persisted = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(persisted.value(QStringLiteral("version")).toInt(), 23);
    QCOMPARE(persisted.value(QStringLiteral("aiProxy")).toString(), QStringLiteral("system"));
    QVERIFY(!loaded->closeToTray);
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

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":24})")));
    const auto unsupported = store.load();
    QVERIFY(!unsupported);
    QCOMPARE(unsupported.error(), ztermy::config::ApplicationSettingsStoreError::unsupportedVersion);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":1,"theme":"dark"})")));
    const auto incomplete = store.load();
    QVERIFY(!incomplete);
    QCOMPARE(incomplete.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({
        "version": 8,
        "theme": "dark",
        "backdropOpacity": 1.0,
        "backdrop": "acrylic",
        "accent": "ztermy",
        "customAccent": "#22C55E",
        "uiFontFamily": "",
        "terminalFontFamily": "Cascadia Mono",
        "terminalFontSize": 14,
        "showAllTerminalFonts": false,
        "terminalLigatures": true,
        "terminalBackgroundOpacity": 1.0,
        "cursor": "terminal",
        "cursorBlink": true,
        "copyOnSelect": false,
        "confirmMultilinePaste": true,
        "language": "system"
    })")));
    const auto incompleteCurrent = store.load();
    QVERIFY(!incompleteCurrent);
    QCOMPARE(incompleteCurrent.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);
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

    auto invalidCustomAccent = ztermy::config::ApplicationSettings{};
    invalidCustomAccent.customAccent = QStringLiteral("green");
    const auto customAccentResult = store.save(invalidCustomAccent);
    QVERIFY(!customAccentResult);
    QCOMPARE(customAccentResult.error(), ztermy::config::ApplicationSettingsStoreError::invalidFormat);
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

void ApplicationSettingsTests::recoversLastKnownGoodSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    const ztermy::config::ApplicationSettingsStore store(path);

    auto first = ztermy::config::ApplicationSettings{};
    first.terminalFontSize = 15;
    auto second = first;
    second.terminalFontSize = 18;
    QVERIFY(store.save(first));
    QVERIFY(store.save(second));
    QVERIFY(writeFile(path, QByteArrayLiteral("{truncated")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, first);
    QVERIFY(store.lastLoadRecoveredFromBackup());
}

QTEST_APPLESS_MAIN(ApplicationSettingsTests)

#include "application_settings_tests.moc"
