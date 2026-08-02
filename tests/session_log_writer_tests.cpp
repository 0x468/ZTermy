#include "infrastructure/logging/SessionLogWriter.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstddef>
#include <span>

namespace
{

void append(ztermy::logging::SessionLogWriter &writer, const QByteArray &bytes)
{
    writer.append(std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
}

} // namespace

class SessionLogWriterTests final : public QObject
{
    Q_OBJECT

private slots:
    void writesOutputAsynchronouslyAndFlushesOnStop();
    void reportsOpenFailureWithoutCreatingAFile();
    void ignoresOutputWhileIdle();
    void reportsDroppedOutputWithoutBlocking();
};

void SessionLogWriterTests::writesOutputAsynchronouslyAndFlushesOnStop()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("session.log"));
    ztermy::logging::SessionLogWriter writer;
    QSignalSpy changed(&writer, &ztermy::logging::SessionLogWriter::stateChanged);

    QVERIFY(writer.start(path));
    QTRY_COMPARE(writer.state(), ztermy::logging::SessionLogState::Active);
    append(writer, QByteArrayLiteral("first\r\n"));
    append(writer, QByteArrayLiteral("second\x1b[0m"));
    writer.stop();

    QCOMPARE(writer.state(), ztermy::logging::SessionLogState::Idle);
    QCOMPARE(writer.droppedBytes(), std::uint64_t{0});
    QVERIFY(changed.count() >= 2);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("first\r\nsecond\x1b[0m"));
}

void SessionLogWriterTests::reportsOpenFailureWithoutCreatingAFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("missing/session.log"));
    ztermy::logging::SessionLogWriter writer;

    QVERIFY(writer.start(path));
    QTRY_COMPARE(writer.state(), ztermy::logging::SessionLogState::Failed);
    QVERIFY(!writer.errorString().isEmpty());
    QVERIFY(!QFile::exists(path));
    writer.stop();
}

void SessionLogWriterTests::ignoresOutputWhileIdle()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("idle.log"));
    ztermy::logging::SessionLogWriter writer;

    append(writer, QByteArrayLiteral("not recorded"));
    QVERIFY(!QFile::exists(path));
    QVERIFY(!writer.start(QString{}));
    QCOMPARE(writer.state(), ztermy::logging::SessionLogState::Idle);
}

void SessionLogWriterTests::reportsDroppedOutputWithoutBlocking()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ztermy::logging::SessionLogWriter writer;
    QSignalSpy changed(&writer, &ztermy::logging::SessionLogWriter::stateChanged);

    QVERIFY(writer.start(directory.filePath(QStringLiteral("bounded.log"))));
    QTRY_COMPARE(writer.state(), ztermy::logging::SessionLogState::Active);
    changed.clear();
    const QByteArray oversized(5 * 1024 * 1024, 'x');
    append(writer, oversized);

    QCOMPARE(writer.droppedBytes(), static_cast<std::uint64_t>(oversized.size()));
    QTRY_VERIFY(changed.count() >= 1);
    writer.stop();
}

QTEST_MAIN(SessionLogWriterTests)

#include "session_log_writer_tests.moc"
