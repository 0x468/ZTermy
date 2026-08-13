#include "domain/terminal/SemanticTerminalObserver.h"

#include <QTest>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace
{

using ztermy::terminal::CommandBlockSessionContext;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::CommandCompletionReason;
using ztermy::terminal::SemanticTerminalObserver;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return std::as_bytes(std::span(value));
}

class SemanticTerminalObserverTests final : public QObject
{
    Q_OBJECT

private slots:
    void capturesOutputBeforeQueuedUiConsumers();
    void finalizesOnceOnDisconnect();
};

void SemanticTerminalObserverTests::capturesOutputBeforeQueuedUiConsumers()
{
    SemanticTerminalObserver observer(
        CommandBlockSessionContext{
            .sessionId = "local-1",
            .host = "localhost",
            .shell = "pwsh",
            .sessionGeneration = 2,
        },
        "nonce", {}, {}, [] {
            return std::int64_t{123};
        });
    observer.append(bytes("\x1b]633;P;HasRichCommandDetection=True\x07"
                          "\x1b]633;B\x07"
                          "\x1b]633;E;Get-Date;nonce\x07"
                          "\x1b]633;C\x07"
                          "2026-08-12\r\n"
                          "\x1b]633;D;0\x07"));

    const auto snapshot = observer.snapshot();
    QVERIFY(!snapshot.internalFailure);
    QVERIFY(!snapshot.lastStoreError.has_value());
    QVERIFY(snapshot.richCapabilityClaimed);
    QCOMPARE(snapshot.commandBlocks.size(), std::size_t{1});
    QCOMPARE(snapshot.commandBlocks.front().command, std::string("Get-Date"));
    QCOMPARE(snapshot.commandBlocks.front().capability, TerminalSemanticCapability::rich);
    QCOMPARE(snapshot.commandBlocks.front().state, CommandBlockState::finished);
}

void SemanticTerminalObserverTests::finalizesOnceOnDisconnect()
{
    std::int64_t now = 10;
    SemanticTerminalObserver observer({}, {}, {}, {}, [&now] {
        return now++;
    });
    observer.observeFallbackCommand("long-running");
    observer.append(bytes("\x1b]133;C\x07partial"));
    observer.finish(CommandCompletionReason::disconnect);
    observer.finish(CommandCompletionReason::decoderReset);

    const auto snapshot = observer.snapshot();
    QVERIFY(snapshot.finished);
    QVERIFY(!snapshot.richCapabilityClaimed);
    QCOMPARE(snapshot.commandBlocks.size(), std::size_t{1});
    QCOMPARE(snapshot.commandBlocks.front().completionReason, std::optional{CommandCompletionReason::disconnect});
}

} // namespace

QTEST_GUILESS_MAIN(SemanticTerminalObserverTests)

#include "semantic_terminal_observer_tests.moc"
