#include "domain/terminal/CommandBlockAssembler.h"

#include <QTest>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using ztermy::terminal::CommandBlockAssembler;
using ztermy::terminal::CommandBlockSessionContext;
using ztermy::terminal::CommandBlockState;
using ztermy::terminal::CommandBlockStore;
using ztermy::terminal::CommandCompletionReason;
using ztermy::terminal::CommandOutputCoverage;
using ztermy::terminal::CommandProvenance;
using ztermy::terminal::ShellIntegrationDecoder;
using ztermy::terminal::TerminalSemanticCapability;

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return std::as_bytes(std::span(value));
}

[[nodiscard]] std::string text(const std::vector<std::byte> &value)
{
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

class CommandBlockAssemblerTests final : public QObject
{
    Q_OBJECT

private slots:
    void assemblesVerifiedRichCommand();
    void usesFallbackCommandForBasicLifecycle();
    void recoversMissingFinishAtNextPrompt();
    void marksActiveOutputUnknownAfterDecoderError();
};

void CommandBlockAssemblerTests::assemblesVerifiedRichCommand()
{
    CommandBlockStore store;
    CommandBlockAssembler assembler(store, CommandBlockSessionContext{
                                               .sessionId = "ssh-1",
                                               .host = "example.test",
                                               .shell = "bash",
                                               .sessionGeneration = 9,
                                           });
    ShellIntegrationDecoder decoder("nonce");
    const auto events = decoder.append(bytes("\x1b]633;P;Cwd=/srv/app\x07"
                                             "\x1b]633;B\x07"
                                             "\x1b]633;E;printf\\x20ok;nonce\x07"
                                             "\x1b]633;C\x07"
                                             "ok\r\n"
                                             "\x1b]633;D;0\x07"));
    QVERIFY(assembler.observe(events, 100).has_value());

    QCOMPARE(store.blocks().size(), std::size_t{1});
    const auto &block = store.blocks().front();
    QCOMPARE(block.command, std::string("printf ok"));
    QCOMPARE(block.workingDirectory, std::string("/srv/app"));
    QCOMPARE(block.sessionId, std::string("ssh-1"));
    QCOMPARE(block.sessionGeneration, std::uint64_t{9});
    QCOMPARE(block.capability, TerminalSemanticCapability::rich);
    QCOMPARE(block.commandProvenance, CommandProvenance::verifiedShellIntegration);
    QCOMPARE(block.state, CommandBlockState::finished);
    QCOMPARE(block.exitStatus, std::optional<int>{0});
    QCOMPARE(block.completionReason, std::optional{CommandCompletionReason::shellMarker});
    QCOMPARE(text(block.retainedOutput), std::string("ok\r\n"));
}

void CommandBlockAssemblerTests::usesFallbackCommandForBasicLifecycle()
{
    CommandBlockStore store;
    CommandBlockAssembler assembler(store, {});
    ShellIntegrationDecoder decoder;
    assembler.observeFallbackCommand("git status");
    const auto events = decoder.append(bytes("\x1b]133;C\x07"
                                             "clean"
                                             "\x1b]133;D;0\x07"));
    QVERIFY(assembler.observe(events, 10).has_value());

    const auto &block = store.blocks().front();
    QCOMPARE(block.command, std::string("git status"));
    QCOMPARE(block.capability, TerminalSemanticCapability::basic);
    QCOMPARE(block.commandProvenance, CommandProvenance::heuristicInput);
    QCOMPARE(text(block.retainedOutput), std::string("clean"));
}

void CommandBlockAssemblerTests::recoversMissingFinishAtNextPrompt()
{
    CommandBlockStore store;
    CommandBlockAssembler assembler(store, {});
    ShellIntegrationDecoder decoder;
    const auto events = decoder.append(bytes("\x1b]133;C\x07partial\x1b]133;A\x07"));
    QVERIFY(assembler.observe(events, 20).has_value());

    const auto &block = store.blocks().front();
    QCOMPARE(block.state, CommandBlockState::finished);
    QVERIFY(!block.exitStatus.has_value());
    QCOMPARE(block.completionReason, std::optional{CommandCompletionReason::promptRecovery});
    QVERIFY(!assembler.activeBlockId().has_value());
}

void CommandBlockAssemblerTests::marksActiveOutputUnknownAfterDecoderError()
{
    CommandBlockStore store;
    CommandBlockAssembler assembler(store, {});
    const std::vector events{
        ztermy::terminal::ShellIntegrationEvent{
            .type = ztermy::terminal::ShellIntegrationEventType::commandExecuted,
            .protocol = ztermy::terminal::ShellIntegrationProtocol::osc633,
        },
        ztermy::terminal::ShellIntegrationEvent{
            .type = ztermy::terminal::ShellIntegrationEventType::decoderError,
            .value = "osc-payload-limit",
        },
    };
    QVERIFY(assembler.observe(events, 30).has_value());

    const auto &block = store.blocks().front();
    QCOMPARE(block.outputCoverage, CommandOutputCoverage::unknown);
    QVERIFY(assembler.finishActive(CommandCompletionReason::disconnect, 40).has_value());
    QCOMPARE(block.completionReason, std::optional{CommandCompletionReason::disconnect});
}

} // namespace

QTEST_GUILESS_MAIN(CommandBlockAssemblerTests)

#include "command_block_assembler_tests.moc"
