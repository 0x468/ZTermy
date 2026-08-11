#include "domain/terminal/ShellIntegrationDecoder.h"

#include <QTest>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using ztermy::terminal::ShellIntegrationDecoder;
using ztermy::terminal::ShellIntegrationDecoderLimits;
using ztermy::terminal::ShellIntegrationEvent;
using ztermy::terminal::ShellIntegrationEventType;
using ztermy::terminal::ShellIntegrationProtocol;

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return std::as_bytes(std::span(value));
}

[[nodiscard]] std::string text(const std::vector<std::byte> &value)
{
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

class ShellIntegrationDecoderTests final : public QObject
{
    Q_OBJECT

private slots:
    void preservesOrdinaryOutput();
    void decodesSplitOsc133Lifecycle();
    void supportsBellAndStringTerminators();
    void decodesVerifiedExplicitCommand();
    void doesNotTrustMismatchedNonce();
    void reportsWorkingDirectoryAndCapability();
    void boundsMalformedSequences();
};

void ShellIntegrationDecoderTests::preservesOrdinaryOutput()
{
    ShellIntegrationDecoder decoder;
    const auto events = decoder.append(bytes("hello\x1b[31m red"));
    QCOMPARE(events.size(), std::size_t{1});
    QCOMPARE(events.front().type, ShellIntegrationEventType::output);
    QCOMPARE(text(events.front().output), std::string("hello\x1b[31m red"));
    QCOMPARE(decoder.outputStreamOffset(), std::uint64_t{14});
}

void ShellIntegrationDecoderTests::decodesSplitOsc133Lifecycle()
{
    ShellIntegrationDecoder decoder;
    auto events = decoder.append(bytes("\x1b]13"));
    QVERIFY(events.empty());
    events = decoder.append(bytes("3;C\x07result"));
    QCOMPARE(events.size(), std::size_t{2});
    QCOMPARE(events[0].type, ShellIntegrationEventType::commandExecuted);
    QCOMPARE(events[0].protocol, ShellIntegrationProtocol::osc133);
    QCOMPARE(events[0].outputStreamOffset, std::uint64_t{0});
    QCOMPARE(events[1].type, ShellIntegrationEventType::output);
    QCOMPARE(text(events[1].output), std::string("result"));
    QCOMPARE(events[1].outputStreamOffset, std::uint64_t{0});
}

void ShellIntegrationDecoderTests::supportsBellAndStringTerminators()
{
    ShellIntegrationDecoder decoder;
    const auto events = decoder.append(bytes("\x1b]133;A\x07\x1b]633;B\x1b\\\x1b]633;D;7\x07"));
    QCOMPARE(events.size(), std::size_t{3});
    QCOMPARE(events[0].type, ShellIntegrationEventType::promptStart);
    QCOMPARE(events[1].type, ShellIntegrationEventType::commandStart);
    QCOMPARE(events[2].type, ShellIntegrationEventType::commandFinished);
    QCOMPARE(events[2].exitStatus, std::optional<int>{7});
}

void ShellIntegrationDecoderTests::decodesVerifiedExplicitCommand()
{
    ShellIntegrationDecoder decoder("session-nonce");
    const auto events = decoder.append(bytes("\x1b]633;E;printf\\x20'a\\x3bb'\\x0a;session-nonce\x07"));
    QCOMPARE(events.size(), std::size_t{1});
    QCOMPARE(events.front().type, ShellIntegrationEventType::explicitCommand);
    QCOMPARE(events.front().value, std::string("printf 'a;b'\n"));
    QVERIFY(events.front().nonceVerified);
}

void ShellIntegrationDecoderTests::doesNotTrustMismatchedNonce()
{
    ShellIntegrationDecoder decoder("expected");
    const auto events = decoder.append(bytes("\x1b]633;E;whoami;spoofed\x07"));
    QCOMPARE(events.size(), std::size_t{1});
    QCOMPARE(events.front().type, ShellIntegrationEventType::explicitCommand);
    QVERIFY(!events.front().nonceVerified);
}

void ShellIntegrationDecoderTests::reportsWorkingDirectoryAndCapability()
{
    ShellIntegrationDecoder decoder;
    const auto events = decoder.append(bytes("\x1b]633;P;Cwd=/srv/app\x07"
                                             "\x1b]633;P;HasRichCommandDetection=True\x07"
                                             "\x1b]1337;CurrentDir=/tmp\x07"));
    QCOMPARE(events.size(), std::size_t{3});
    QCOMPARE(events[0].type, ShellIntegrationEventType::workingDirectory);
    QCOMPARE(events[0].value, std::string("/srv/app"));
    QCOMPARE(events[1].type, ShellIntegrationEventType::richCapabilityClaim);
    QCOMPARE(events[2].protocol, ShellIntegrationProtocol::osc1337);
    QCOMPARE(events[2].value, std::string("/tmp"));
}

void ShellIntegrationDecoderTests::boundsMalformedSequences()
{
    ShellIntegrationDecoder decoder({}, ShellIntegrationDecoderLimits{.maxOscPayloadBytes = 8});
    auto events = decoder.append(bytes("\x1b]633;E;0123456789\x07"));
    QCOMPARE(events.size(), std::size_t{1});
    QCOMPARE(events.front().type, ShellIntegrationEventType::decoderError);
    QCOMPARE(events.front().value, std::string("osc-payload-limit"));

    events = decoder.append(bytes("\x1b]633;D;oops\x07"));
    QCOMPARE(events.size(), std::size_t{1});
    QCOMPARE(events.front().type, ShellIntegrationEventType::decoderError);

    events = decoder.append(bytes("\x1b]633;A"));
    QVERIFY(events.empty());
    events = decoder.finish();
    QCOMPARE(events.size(), std::size_t{1});
    QCOMPARE(events.front().value, std::string("incomplete-osc"));
}

} // namespace

QTEST_GUILESS_MAIN(ShellIntegrationDecoderTests)

#include "shell_integration_decoder_tests.moc"
