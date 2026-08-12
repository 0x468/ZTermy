#include "application/ai/AiActivityModel.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace ztermy::ai;

class AiActivityModelTests final : public QObject
{
    Q_OBJECT

private slots:
    void recordsAndRestoresOnlyBoundedMetadata();
    void updatesAStableToolCardAcrossItsLifecycle();
    void rejectsUnsafeMetadataTokens();
    void clearsAndExportsTheAuditTrail();
};

void AiActivityModelTests::recordsAndRestoresOnlyBoundedMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ai-audit.json"));
    {
        AiActivityModel model(path);
        model.record({.conversationId = QStringLiteral("conversation-secret"),
                      .toolCallId = QStringLiteral("tool-call-secret"),
                      .toolName = QStringLiteral("run_command"),
                      .state = QStringLiteral("succeeded"),
                      .resultCode = QStringLiteral("ok"),
                      .permissionMode = QStringLiteral("ask_each_write"),
                      .sessionGeneration = 7,
                      .sideEffecting = true,
                      .highRisk = false});
        QCOMPARE(model.rowCount(), 1);
    }

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray payload = file.readAll();
    QVERIFY(!payload.contains("conversation-secret"));
    QVERIFY(!payload.contains("tool-call-secret"));
    QVERIFY(!payload.contains("rm -rf"));

    AiActivityModel restored(path);
    QCOMPARE(restored.rowCount(), 1);
    const QModelIndex row = restored.index(0);
    QCOMPARE(restored.data(row, AiActivityModel::ToolNameRole).toString(), QStringLiteral("run_command"));
    QCOMPARE(restored.data(row, AiActivityModel::StateRole).toString(), QStringLiteral("succeeded"));
    QCOMPARE(restored.data(row, AiActivityModel::SessionGenerationRole).toULongLong(), 7ULL);
    QVERIFY(restored.data(row, AiActivityModel::SideEffectingRole).toBool());
}

void AiActivityModelTests::updatesAStableToolCardAcrossItsLifecycle()
{
    QTemporaryDir directory;
    AiActivityModel model(directory.filePath(QStringLiteral("ai-audit.json")));
    AiActivityEvent event{.conversationId = QStringLiteral("conversation"),
                          .toolCallId = QStringLiteral("call"),
                          .toolName = QStringLiteral("run_command"),
                          .state = QStringLiteral("queued"),
                          .resultCode = QStringLiteral("pending"),
                          .permissionMode = QStringLiteral("ask_each_write"),
                          .sessionGeneration = 2,
                          .sideEffecting = true,
                          .highRisk = true};
    model.record(event);
    event.state = QStringLiteral("awaiting_approval");
    model.record(event);
    event.state = QStringLiteral("succeeded");
    event.resultCode = QStringLiteral("ok");
    model.record(event);

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex row = model.index(0);
    QCOMPARE(model.data(row, AiActivityModel::StateRole).toString(), QStringLiteral("succeeded"));
    QCOMPARE(model.data(row, AiActivityModel::ResultCodeRole).toString(), QStringLiteral("ok"));
    QVERIFY(model.data(row, AiActivityModel::HighRiskRole).toBool());
}

void AiActivityModelTests::rejectsUnsafeMetadataTokens()
{
    QTemporaryDir directory;
    AiActivityModel model(directory.filePath(QStringLiteral("ai-audit.json")));
    model.record({.conversationId = QStringLiteral("conversation"),
                  .toolCallId = QStringLiteral("call"),
                  .toolName = QStringLiteral("run_command\nsecret"),
                  .state = QStringLiteral("succeeded"),
                  .resultCode = QStringLiteral("ok"),
                  .permissionMode = QStringLiteral("ask_each_write")});
    QCOMPARE(model.rowCount(), 0);
}

void AiActivityModelTests::clearsAndExportsTheAuditTrail()
{
    QTemporaryDir directory;
    AiActivityModel model(directory.filePath(QStringLiteral("ai-audit.json")));
    model.record({.conversationId = QStringLiteral("conversation"),
                  .toolCallId = QStringLiteral("call"),
                  .toolName = QStringLiteral("get_terminal_state"),
                  .state = QStringLiteral("succeeded"),
                  .resultCode = QStringLiteral("ok"),
                  .permissionMode = QStringLiteral("observer")});
    const QString exported = directory.filePath(QStringLiteral("exported.json"));
    QVERIFY(model.exportTo(exported));
    QVERIFY(QFileInfo::exists(exported));
    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

QTEST_GUILESS_MAIN(AiActivityModelTests)

#include "ai_activity_model_tests.moc"
