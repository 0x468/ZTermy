#include "domain/ai/AiContextBroker.h"

#include <QTest>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace
{

using ztermy::ai::AiContextBroker;
using ztermy::ai::AiContextItemKind;
using ztermy::ai::AiContextLimits;
using ztermy::ai::AiContextRequest;
using ztermy::ai::AiExplicitContext;
using ztermy::ai::AiTerminalFrameContext;
using ztermy::terminal::CommandBlockStart;
using ztermy::terminal::CommandBlockStore;
using ztermy::terminal::CommandOutputObservation;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return std::as_bytes(std::span(value));
}

[[nodiscard]] ztermy::terminal::CommandBlockId addBlock(CommandBlockStore &store,
                                                        const std::string &command,
                                                        const std::string_view output,
                                                        const int exitStatus)
{
    const auto id = store.begin(CommandBlockStart{.command = command,
                                                  .workingDirectory = "/srv",
                                                  .sessionId = "session",
                                                  .host = "host",
                                                  .shell = "bash",
                                                  .sessionGeneration = 7,
                                                  .capability = TerminalSemanticCapability::rich,
                                                  .outputStreamOffset = 0})
                        .value();
    store.append(id, CommandOutputObservation{.bytes = bytes(output), .streamOffset = 0}).value();
    store.finish(id, exitStatus, 10).value();
    return id;
}

class AiContextBrokerTests final : public QObject
{
    Q_OBJECT

private slots:
    void selectsFailureAndFivePrecedingBlocks();
    void exposesEvidenceQualityAndNormalizesOutput();
    void appliesExclusionAndPinPriority();
    void enforcesItemAndAggregateBounds();
    void redactsBeforePublishingPreview();
};

void AiContextBrokerTests::selectsFailureAndFivePrecedingBlocks()
{
    CommandBlockStore store;
    for (int index = 0; index < 7; ++index)
    {
        static_cast<void>(addBlock(store, "echo " + std::to_string(index), "ok\n", 0));
    }
    const auto failedId = addBlock(store, "false", "failed\n", 1);
    AiContextBroker broker;
    const auto bundle = broker.build(store, AiContextRequest{.preferLastFailure = true});
    QCOMPARE(bundle.items.size(), std::size_t{6});
    QCOMPARE(bundle.items.front().command, std::string("false"));
    QCOMPARE(bundle.items.front().exitStatus, std::optional<int>{1});
    QCOMPARE(bundle.items.front().id.ends_with(':' + std::to_string(failedId)), true);
    QCOMPARE(bundle.items.at(1).command, std::string("echo 6"));
    QVERIFY(bundle.items.at(1).automatic);
}

void AiContextBrokerTests::exposesEvidenceQualityAndNormalizesOutput()
{
    CommandBlockStore store;
    const auto id = addBlock(store, "printf", "\x1b[31mred\x1b[0m\r\nline\rnext", 0);
    AiContextBroker broker;
    const auto bundle = broker.build(store, AiContextRequest{.primaryBlockId = id,
                                                             .automaticContextEnabled = false,
                                                             .currentFrame = AiTerminalFrameContext{
                                                                 .id = "frame",
                                                                 .content = "\x1b]0;title\x07screen",
                                                                 .sessionId = "session",
                                                                 .capability = TerminalSemanticCapability::basic}});
    QCOMPARE(bundle.items.size(), std::size_t{2});
    QCOMPARE(bundle.items.front().content, std::string("red\nline\nnext"));
    QCOMPARE(bundle.items.front().capability, TerminalSemanticCapability::rich);
    QCOMPARE(bundle.items.at(1).content, std::string("screen"));
    QVERIFY(bundle.items.front().untrustedEvidence);
}

void AiContextBrokerTests::appliesExclusionAndPinPriority()
{
    CommandBlockStore store;
    const auto first = addBlock(store, "first", "1", 0);
    const auto second = addBlock(store, "second", "2", 0);
    const auto firstId = "command-block:session:7:" + std::to_string(first);
    const auto secondId = "command-block:session:7:" + std::to_string(second);
    AiContextBroker broker;
    const auto bundle = broker.build(store,
                                     AiContextRequest{.primaryBlockId = second,
                                                      .explicitItems = {AiExplicitContext{.id = "note",
                                                                                         .content = "note"}},
                                                      .excludedItemIds = {secondId},
                                                      .pinnedItemIds = {firstId}});
    QCOMPARE(bundle.items.size(), std::size_t{2});
    QCOMPARE(bundle.items.front().kind, AiContextItemKind::explicitAttachment);
    QCOMPARE(bundle.items.at(1).command, std::string("first"));
    QVERIFY(bundle.items.at(1).pinned);
}

void AiContextBrokerTests::enforcesItemAndAggregateBounds()
{
    CommandBlockStore store;
    AiContextBroker broker(AiContextLimits{.maxTotalBytes = 900,
                                           .maxTotalLines = 20,
                                           .maxEstimatedTokens = 225,
                                           .maxItemBytes = 400,
                                           .maxItemLines = 8,
                                           .maxPrecedingBlocks = 5});
    std::string large;
    for (int index = 0; index < 40; ++index)
    {
        large += "line-" + std::to_string(index) + "-你好\n";
    }
    const auto bundle = broker.build(store,
                                     AiContextRequest{.explicitItems = {
                                                          AiExplicitContext{.id = "a", .content = large},
                                                          AiExplicitContext{.id = "b", .content = large},
                                                          AiExplicitContext{.id = "c", .content = large}}});
    QVERIFY(bundle.totalBytes <= std::size_t{900});
    QVERIFY(bundle.totalLines <= std::size_t{20});
    QVERIFY(bundle.estimatedTokens <= std::size_t{225});
    QVERIFY(bundle.items.front().accountedBytes <= std::size_t{400});
    QVERIFY(bundle.items.front().lineCount <= std::size_t{8});
    QVERIFY(bundle.items.front().truncated);
    QVERIFY(bundle.aggregateTruncated);
    QVERIFY(bundle.droppedItems > 0);
}

void AiContextBrokerTests::redactsBeforePublishingPreview()
{
    CommandBlockStore store;
    const auto id = addBlock(store,
                             "curl -H 'Authorization: Bearer command-secret-12345'",
                             "OPENAI_API_KEY=sk-abcdefghijklmnopqrstuvwxyz123456\n",
                             1);
    AiContextBroker broker;
    const auto bundle = broker.build(
        store,
        AiContextRequest{.primaryBlockId = id,
                         .automaticContextEnabled = false,
                         .redactionRules = {ztermy::ai::AiUserRedactionRule{
                             .id = "host-rule",
                             .pattern = "host"}}});
    QCOMPARE(bundle.items.size(), std::size_t{1});
    const auto &item = bundle.items.front();
    QVERIFY(!item.command.contains("command-secret"));
    QVERIFY(!item.content.contains("sk-abcdefghijklmnopqrstuvwxyz"));
    QVERIFY(!item.host.contains("host"));
    QVERIFY(item.redacted);
    QVERIFY(item.redactionCount >= std::size_t{3});
    QCOMPARE(bundle.totalRedactions, item.redactionCount);
}

} // namespace

QTEST_GUILESS_MAIN(AiContextBrokerTests)

#include "ai_context_broker_tests.moc"
