#include "infrastructure/ai/AiQuickMessageStore.h"

#include <QtTest/QTest>

#include <QFile>
#include <QTemporaryDir>

namespace
{
using namespace ztermy::ai;

class AiQuickMessageStoreTests final : public QObject
{
    Q_OBJECT

private slots:
    void normalizesAndValidatesSlugs();
    void roundTripsQuickMessages();
    void rejectsDuplicateSlugsAndMalformedFiles();
};

void AiQuickMessageStoreTests::normalizesAndValidatesSlugs()
{
    QCOMPARE(normalizeAiQuickMessageSlug("  Review Docker Logs  "), std::string("review-docker-logs"));
    QCOMPARE(normalizeAiQuickMessageSlug("Already--Separated"), std::string("already-separated"));
    QVERIFY(validAiQuickMessageSlug("review-logs-2"));
    QVERIFY(!validAiQuickMessageSlug("Review Logs"));
    QVERIFY(!validAiQuickMessageSlug("double--dash"));
}

void AiQuickMessageStoreTests::roundTripsQuickMessages()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AiQuickMessageStore store(directory.filePath(QStringLiteral("ai_quick_messages.json")));
    const std::vector messages{AiQuickMessage{.id = "status",
                                              .name = "Service status",
                                              .slug = "service-status",
                                              .content = "Inspect failed services and summarize likely causes.",
                                              .description = "Diagnose service failures",
                                              .createdUtcMs = 100,
                                              .modifiedUtcMs = 100},
                               AiQuickMessage{.id = "logs",
                                              .name = "Recent logs",
                                              .slug = "recent-logs",
                                              .content = "Review the attached logs.\nPrioritize actionable errors.",
                                              .createdUtcMs = 200,
                                              .modifiedUtcMs = 250}};
    QVERIFY(store.save(messages).has_value());
    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(*loaded, messages);
}

void AiQuickMessageStoreTests::rejectsDuplicateSlugsAndMalformedFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ai_quick_messages.json"));
    AiQuickMessage first{.id = "first",
                         .name = "First",
                         .slug = "same",
                         .content = "First prompt",
                         .createdUtcMs = 1,
                         .modifiedUtcMs = 1};
    auto second = first;
    second.id = "second";
    const std::vector duplicateMessages{first, second};
    QVERIFY(!AiQuickMessageStore(path).save(duplicateMessages).has_value());

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(R"({"version":1,"messages":[{"id":"x"}]})") > 0);
    file.close();
    QVERIFY(!AiQuickMessageStore(path).load().has_value());
}

} // namespace

QTEST_GUILESS_MAIN(AiQuickMessageStoreTests)

#include "ai_quick_message_store_tests.moc"
