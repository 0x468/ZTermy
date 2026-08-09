#include "infrastructure/workbench/QuickCommandStore.h"
#include "infrastructure/workbench/ScriptStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <vector>

namespace
{

[[nodiscard]] ztermy::workbench::ScriptDefinition sampleScript(std::string id = "script-1")
{
    using namespace ztermy::workbench;
    return {.id = std::move(id),
            .name = "Deploy",
            .description = "Deploys a selected service",
            .shellScope = ShellScope::posix,
            .variables = {{.name = "service",
                           .label = "Service",
                           .type = ScriptVariableType::choice,
                           .defaultValue = "api",
                           .choices = {"api", "worker"}}},
            .steps = {{.command = "deploy ${service}"},
                      {.command = "verify ${service}",
                       .continuation = ScriptContinuation::literalOutput,
                       .outputMarker = "ready:${service}",
                       .timeoutMs = 15'000}},
            .createdUtcMs = 10,
            .modifiedUtcMs = 20};
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

} // namespace

class ScriptStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void savesAndLoadsVersionTwoInStableOrder();
    void migratesLegacyQuickCommandsOnce();
    void rejectsMalformedUnsupportedAndDuplicateDocuments();
    void recoversLastKnownGoodScripts();
};

void ScriptStoreTests::savesAndLoadsVersionTwoInStableOrder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nested/scripts.json"));
    const ztermy::workbench::ScriptStore store(path);
    const std::array scripts{sampleScript(), sampleScript("script-2")};

    QVERIFY(store.save(scripts));
    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, std::vector<ztermy::workbench::ScriptDefinition>(scripts.begin(), scripts.end()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray persisted = file.readAll();
    QVERIFY(persisted.contains("\"version\": 2"));
    QVERIFY(persisted.indexOf("script-1") < persisted.indexOf("script-2"));
}

void ScriptStoreTests::migratesLegacyQuickCommandsOnce()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString legacyPath = directory.filePath(QStringLiteral("quick_commands.json"));
    const QString scriptPath = directory.filePath(QStringLiteral("scripts.json"));
    const ztermy::workbench::QuickCommand command{.id = "legacy",
                                                  .name = "Legacy",
                                                  .command = "echo one\necho two",
                                                  .description = "Imported",
                                                  .shellScope = ztermy::workbench::ShellScope::powershell,
                                                  .createdUtcMs = 11,
                                                  .modifiedUtcMs = 12};
    const std::array commands{command};
    QVERIFY(ztermy::workbench::QuickCommandStore(legacyPath).save(commands));

    const ztermy::workbench::ScriptStore store(scriptPath);
    const auto migrated = store.loadOrMigrate(legacyPath);
    QVERIFY(migrated);
    QCOMPARE(migrated->size(), std::size_t{1});
    QCOMPARE(migrated->front().steps.front().command, command.command);
    QVERIFY(QFile::exists(scriptPath));

    const std::array replacement{sampleScript("new")};
    QVERIFY(store.save(replacement));
    const auto secondLoad = store.loadOrMigrate(legacyPath);
    QVERIFY(secondLoad);
    QCOMPARE(secondLoad->front().id, std::string("new"));
}

void ScriptStoreTests::rejectsMalformedUnsupportedAndDuplicateDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("scripts.json"));
    const ztermy::workbench::ScriptStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral("{not-json")));
    const auto malformed = store.load();
    QVERIFY(!malformed);
    QCOMPARE(malformed.error(), ztermy::workbench::ScriptStoreError::invalidFormat);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":3,"scripts":[]})")));
    const auto unsupported = store.load();
    QVERIFY(!unsupported);
    QCOMPARE(unsupported.error(), ztermy::workbench::ScriptStoreError::unsupportedVersion);

    const auto script = sampleScript();
    const std::array duplicates{script, script};
    const auto duplicate = store.save(duplicates);
    QVERIFY(!duplicate);
    QCOMPARE(duplicate.error(), ztermy::workbench::ScriptStoreError::invalidFormat);
}

void ScriptStoreTests::recoversLastKnownGoodScripts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("scripts.json"));
    const ztermy::workbench::ScriptStore store(path);

    const auto first = sampleScript();
    auto second = first;
    second.name = "Updated deploy";
    const std::array firstGeneration{first};
    const std::array secondGeneration{second};
    QVERIFY(store.save(firstGeneration));
    QVERIFY(store.save(secondGeneration));
    QVERIFY(writeFile(path, QByteArrayLiteral("{truncated")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, std::vector<ztermy::workbench::ScriptDefinition>{first});
    QVERIFY(store.lastLoadRecoveredFromBackup());
}

QTEST_GUILESS_MAIN(ScriptStoreTests)

#include "script_store_tests.moc"
