#include "core/persistence/LastKnownGoodFile.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{

using ztermy::persistence::LastKnownGoodError;
using ztermy::persistence::PayloadValidation;

[[nodiscard]] PayloadValidation validatePayload(const QByteArrayView payload)
{
    if (payload.startsWith("future:"))
    {
        return PayloadValidation::unsupportedVersion;
    }
    return payload.startsWith("valid:") ? PayloadValidation::valid : PayloadValidation::invalid;
}

[[nodiscard]] bool writeFile(const QString &path, const QByteArrayView payload)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(payload.data(), payload.size()) == payload.size();
}

class LastKnownGoodFileTests final : public QObject
{
    Q_OBJECT

private slots:
    void missingPrimaryUsesValidBackup();
    void malformedPrimaryUsesValidBackup();
    void futurePrimaryFailsClosed();
    void saveBacksUpOnlyValidPrimary();
};

void LastKnownGoodFileTests::missingPrimaryUsesValidBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    QVERIFY(writeFile(path + QStringLiteral(".bak"), QByteArrayLiteral("valid:old")));

    const auto loaded = ztermy::persistence::loadLastKnownGood(path, 128, validatePayload);
    QVERIFY(loaded);
    QVERIFY(loaded->has_value());
    QVERIFY(loaded->value().recoveredFromBackup);
    QCOMPARE(loaded->value().bytes, QByteArrayLiteral("valid:old"));
}

void LastKnownGoodFileTests::malformedPrimaryUsesValidBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    QVERIFY(writeFile(path, QByteArrayLiteral("damaged")));
    QVERIFY(writeFile(path + QStringLiteral(".bak"), QByteArrayLiteral("valid:old")));

    const auto loaded = ztermy::persistence::loadLastKnownGood(path, 128, validatePayload);
    QVERIFY(loaded && loaded->has_value());
    QVERIFY(loaded->value().recoveredFromBackup);
    QCOMPARE(loaded->value().bytes, QByteArrayLiteral("valid:old"));
}

void LastKnownGoodFileTests::futurePrimaryFailsClosed()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));
    QVERIFY(writeFile(path, QByteArrayLiteral("future:new")));
    QVERIFY(writeFile(path + QStringLiteral(".bak"), QByteArrayLiteral("valid:old")));

    const auto loaded = ztermy::persistence::loadLastKnownGood(path, 128, validatePayload);
    QVERIFY(!loaded);
    QCOMPARE(loaded.error(), LastKnownGoodError::unsupportedVersion);

    const auto saved =
        ztermy::persistence::saveLastKnownGood(path, QByteArrayLiteral("valid:replacement"), 128, validatePayload);
    QVERIFY(!saved);
    QCOMPARE(saved.error(), LastKnownGoodError::unsupportedVersion);
    QFile primary(path);
    QVERIFY(primary.open(QIODevice::ReadOnly));
    QCOMPARE(primary.readAll(), QByteArrayLiteral("future:new"));
}

void LastKnownGoodFileTests::saveBacksUpOnlyValidPrimary()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.json"));

    QVERIFY(ztermy::persistence::saveLastKnownGood(path, QByteArrayLiteral("valid:first"), 128, validatePayload));
    QVERIFY(ztermy::persistence::saveLastKnownGood(path, QByteArrayLiteral("valid:second"), 128, validatePayload));
    QFile backup(path + QStringLiteral(".bak"));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), QByteArrayLiteral("valid:first"));
    backup.close();

    QVERIFY(writeFile(path, QByteArrayLiteral("damaged")));
    QVERIFY(ztermy::persistence::saveLastKnownGood(path, QByteArrayLiteral("valid:third"), 128, validatePayload));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), QByteArrayLiteral("valid:first"));
}

} // namespace

QTEST_GUILESS_MAIN(LastKnownGoodFileTests)

#include "last_known_good_file_tests.moc"
