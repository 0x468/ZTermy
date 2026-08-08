#include "domain/workbench/QuickCommand.h"
#include "infrastructure/workbench/PowerShellHistoryReader.h"
#include "infrastructure/workbench/QuickCommandStore.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <vector>

namespace
{

[[nodiscard]] ztermy::workbench::QuickCommand sampleCommand(std::string id = "command-1")
{
    return {
        .id = std::move(id),
        .name = "Inspect services",
        .command = "systemctl --failed",
        .description = "Lists failed system services",
        .shellScope = ztermy::workbench::ShellScope::posix,
        .createdUtcMs = 1'754'000'000'123,
        .modifiedUtcMs = 1'754'000'000'456,
    };
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

} // namespace

class QuickCommandStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void missingFileLoadsAsEmpty();
    void savesAndLoadsStableOrder();
    void rejectsMalformedUnsupportedAndDuplicateDocuments();
    void rejectsUnsafeOrOversizedCommands();
    void readsBoundedPowerShellHistoryTail();
    void readsLargePowerShellHistoryWithinBudget();
};

void QuickCommandStoreTests::missingFileLoadsAsEmpty()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::workbench::QuickCommandStore store(directory.filePath(QStringLiteral("quick_commands.json")));

    const auto loaded = store.load();
    QVERIFY(loaded);
    QVERIFY(loaded->empty());
}

void QuickCommandStoreTests::savesAndLoadsStableOrder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nested/quick_commands.json"));
    const ztermy::workbench::QuickCommandStore store(path);
    auto second = sampleCommand("command-2");
    second.name = "PowerShell version";
    second.command = "$PSVersionTable.PSVersion";
    second.shellScope = ztermy::workbench::ShellScope::powershell;
    const std::array commands{sampleCommand(), second};

    QVERIFY(store.save(commands));
    const auto loaded = store.load();
    QVERIFY(loaded);
    QCOMPARE(*loaded, std::vector<ztermy::workbench::QuickCommand>(commands.begin(), commands.end()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray persisted = file.readAll();
    QVERIFY(persisted.contains("\"version\": 1"));
    QVERIFY(persisted.indexOf("command-1") < persisted.indexOf("command-2"));
}

void QuickCommandStoreTests::rejectsMalformedUnsupportedAndDuplicateDocuments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("quick_commands.json"));
    const ztermy::workbench::QuickCommandStore store(path);

    QVERIFY(writeFile(path, QByteArrayLiteral("{not-json")));
    const auto malformed = store.load();
    QVERIFY(!malformed);
    QCOMPARE(malformed.error(), ztermy::workbench::QuickCommandStoreError::invalidFormat);

    QVERIFY(writeFile(path, QByteArrayLiteral(R"({"version":2,"commands":[]})")));
    const auto unsupported = store.load();
    QVERIFY(!unsupported);
    QCOMPARE(unsupported.error(), ztermy::workbench::QuickCommandStoreError::unsupportedVersion);

    const auto command = sampleCommand();
    const std::array duplicateCommands{command, command};
    const auto duplicate = store.save(duplicateCommands);
    QVERIFY(!duplicate);
    QCOMPARE(duplicate.error(), ztermy::workbench::QuickCommandStoreError::invalidFormat);
}

void QuickCommandStoreTests::rejectsUnsafeOrOversizedCommands()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const ztermy::workbench::QuickCommandStore store(directory.filePath(QStringLiteral("quick_commands.json")));

    auto unsafe = sampleCommand();
    unsafe.command = std::string("echo safe\x1b[2J", 13);
    const std::array unsafeCommands{unsafe};
    const auto unsafeResult = store.save(unsafeCommands);
    QVERIFY(!unsafeResult);
    QCOMPARE(unsafeResult.error(), ztermy::workbench::QuickCommandStoreError::invalidFormat);

    auto oversized = sampleCommand();
    oversized.command.assign((64 * 1024) + 1, 'x');
    const std::array oversizedCommands{oversized};
    const auto oversizedResult = store.save(oversizedCommands);
    QVERIFY(!oversizedResult);
    QCOMPARE(oversizedResult.error(), ztermy::workbench::QuickCommandStoreError::invalidFormat);
}

void QuickCommandStoreTests::readsBoundedPowerShellHistoryTail()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ConsoleHost_history.txt"));
    QVERIFY(writeFile(path, QByteArrayLiteral("partial-old-line\nGet-Date\nWrite-Output `\n  newest\n")));

    const auto entries = ztermy::workbench::readPowerShellHistory(path, 10, 43);
    QVERIFY(entries);
    QCOMPARE(entries->size(), std::size_t{2});
    QCOMPARE(entries->at(0).command, std::string("Write-Output `\n  newest"));
    QCOMPARE(entries->at(1).command, std::string("Get-Date"));

    const auto missing = ztermy::workbench::readPowerShellHistory(directory.filePath(QStringLiteral("missing.txt")));
    QVERIFY(missing);
    QVERIFY(missing->empty());
}

void QuickCommandStoreTests::readsLargePowerShellHistoryWithinBudget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ConsoleHost_history.txt"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    constexpr qsizetype sourceBytes = qsizetype{8} * 1024 * 1024;
    constexpr qsizetype blockBytes = qsizetype{64} * 1024;
    const QByteArray line = QByteArrayLiteral("Get-ChildItem -Force | Select-Object Name,Length\n");
    QByteArray block;
    block.reserve(blockBytes);
    while (block.size() + line.size() <= blockBytes)
    {
        block.append(line);
    }
    for (qsizetype written = 0; written < sourceBytes; written += block.size())
    {
        QVERIFY(file.write(block) == block.size());
    }
    file.close();

    QElapsedTimer timer;
    timer.start();
    constexpr qint64 maximumReadBytes = qint64{1024} * 1024;
    const auto entries = ztermy::workbench::readPowerShellHistory(path, 2'000, maximumReadBytes);
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY(entries);
    QCOMPARE(entries->size(), std::size_t{2'000});
    qInfo("V2.3 history budget: %lld ms for an 8 MiB source with a 1 MiB bounded tail",
          static_cast<long long>(elapsedMs));
    QVERIFY2(elapsedMs <= 2'000, "Large history tail exceeded the V2.3 2 s Debug budget");
}

QTEST_GUILESS_MAIN(QuickCommandStoreTests)

#include "quick_command_store_tests.moc"
