#include "domain/ai/AiTerminalCapabilityAdapter.h"

#include <QtTest/QTest>

namespace
{
using ztermy::ai::AiTerminalCapabilityAdapter;
using ztermy::terminal::TerminalSemanticCapability;

class AiTerminalCapabilityAdapterTests final : public QObject
{
    Q_OBJECT

private slots:
    void mapsSupportedShellFixtures();
    void exposesNestedAndAlternateScreenDegradation();
};

void AiTerminalCapabilityAdapterTests::mapsSupportedShellFixtures()
{
    const auto powershell = AiTerminalCapabilityAdapter::describe("pwsh", TerminalSemanticCapability::rich, false);
    QCOMPARE(powershell.shellFamily, std::string("powershell"));
    QCOMPARE(powershell.semanticQuality, std::string("rich_verified"));
    QVERIFY(powershell.exactCommandBoundaries);
    QVERIFY(powershell.reliableExitStatus);

    for (const std::string shell : {"bash", "zsh", "fish"})
    {
        const auto capability = AiTerminalCapabilityAdapter::describe(shell, TerminalSemanticCapability::basic, false);
        QCOMPARE(capability.shellFamily, shell);
        QCOMPARE(capability.semanticQuality, std::string("basic_unverified"));
        QVERIFY(!capability.exactCommandBoundaries);
        QVERIFY(!capability.degradedReason.empty());
    }
}

void AiTerminalCapabilityAdapterTests::exposesNestedAndAlternateScreenDegradation()
{
    // Nested SSH, sudo prompts, and tmux without verified passthrough all arrive
    // as unavailable semantics; the adapter must not infer exact boundaries.
    const auto nested = AiTerminalCapabilityAdapter::describe("bash", TerminalSemanticCapability::none, false);
    QCOMPARE(nested.semanticQuality, std::string("unavailable"));
    QVERIFY(!nested.exactCommandBoundaries);

    const auto tmuxAlternate = AiTerminalCapabilityAdapter::describe("zsh", TerminalSemanticCapability::none, true);
    QCOMPARE(tmuxAlternate.observationMode, std::string("alternate_screen_frame"));
    QCOMPARE(tmuxAlternate.degradedReason, std::string("command_semantics_unavailable_in_alternate_screen"));
    QVERIFY(tmuxAlternate.frameDeltas);
}
} // namespace

QTEST_GUILESS_MAIN(AiTerminalCapabilityAdapterTests)

#include "ai_terminal_capability_adapter_tests.moc"
