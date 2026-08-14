#include "application/ai/AiActionToolDispatcher.h"
#include "application/ssh/SshTerminalSession.h"
#include "domain/terminal/TerminalOutputSink.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>

namespace
{

using ztermy::ai::AiActionToolContext;
using ztermy::ai::AiActionToolDispatcher;
using ztermy::ai::AiActionToolDisposition;
using ztermy::ai::AiAgentTurnBudget;
using ztermy::ai::AiPermissionMode;
using ztermy::ai::AiTerminalAction;
using ztermy::ai::AiToolCall;

constexpr std::string_view sessionId = "ssh-agent-real-host";
constexpr std::uint64_t sessionGeneration = 1;

class CapturingOutputSink final : public ztermy::terminal::TerminalOutputSink
{
public:
    void append(const std::span<const std::byte> bytes) noexcept override
    {
        try
        {
            std::scoped_lock lock(m_mutex);
            m_output.append(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
        }
        catch (const std::bad_alloc &)
        {
            m_captureFailed = true;
        }
    }

    [[nodiscard]] QByteArray snapshot() const
    {
        std::scoped_lock lock(m_mutex);
        return m_output;
    }

    [[nodiscard]] bool failed() const
    {
        std::scoped_lock lock(m_mutex);
        return m_captureFailed;
    }

private:
    mutable std::mutex m_mutex;
    QByteArray m_output;
    bool m_captureFailed = false;
};

[[nodiscard]] AiActionToolContext context()
{
    return {.conversationId = "ssh-agent-real-host-conversation",
            .turnId = 1,
            .target = {.sessionId = std::string(sessionId), .sessionGeneration = sessionGeneration},
            .permissionMode = AiPermissionMode::automatic,
            .profileId = "ssh-agent-real-host-profile"};
}

[[nodiscard]] AiToolCall commandCall(const std::string &id, const std::string &command)
{
    const QJsonObject arguments{{QStringLiteral("command"), QString::fromUtf8(command)}};
    return {.id = id,
            .name = "run_command",
            .argumentsJson = QJsonDocument(arguments).toJson(QJsonDocument::Compact).toStdString()};
}

[[nodiscard]] AiToolCall ptyCall(const std::string &id, const std::string &data)
{
    const QJsonObject arguments{{QStringLiteral("data"), QString::fromUtf8(data)},
                                {QStringLiteral("append_enter"), true}};
    return {.id = id,
            .name = "write_to_pty",
            .argumentsJson = QJsonDocument(arguments).toJson(QJsonDocument::Compact).toStdString()};
}

class AiAgentRealHostTests final : public QObject
{
    Q_OBJECT

private slots:
    void executesAgentAndUserInputThroughRealSshPty();
};

void AiAgentRealHostTests::executesAgentAndUserInputThroughRealSshPty()
{
    const QByteArray host = qgetenv("ZTERMY_TEST_SSH_HOST");
    const QByteArray username = qgetenv("ZTERMY_TEST_SSH_USERNAME");
    const QByteArray privateKey = qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY");
    const QByteArray expectedFingerprint = qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT");
    if (host.isEmpty() || username.isEmpty() || privateKey.isEmpty() || expectedFingerprint.isEmpty())
    {
        QSKIP("Set the SSH real-host private-key variables to run the Agent-over-SSH gate");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto output = std::make_shared<CapturingOutputSink>();
    ztermy::ssh::SshTerminalSession session;
    session.setOutputSink(output);
    QSignalSpy confirmationSpy(&session, &ztermy::ssh::SshTerminalSession::hostKeyConfirmationRequired);
    QSignalSpy runningSpy(&session, &ztermy::ssh::SshTerminalSession::runningChanged);

    ztermy::ssh::SshConnectionRequest request{
        .host = QString::fromUtf8(host),
        .port = 22,
        .username = QString::fromUtf8(username),
        .authentication = ztermy::ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = QString::fromUtf8(privateKey),
        .knownHostsPath = directory.filePath(QStringLiteral("known_hosts.json")),
    };
    QVERIFY(!session.start(std::move(request), {.columns = 100, .rows = 30}));
    QTRY_COMPARE_WITH_TIMEOUT(confirmationSpy.count(), 1, 10'000);
    QCOMPARE(confirmationSpy.constFirst().at(2).toString(), QString::fromLatin1(expectedFingerprint));
    session.confirmHostKey(false);
    QTRY_VERIFY_WITH_TIMEOUT(
        [&runningSpy] {
            for (const auto &arguments : runningSpy)
            {
                if (arguments.at(0).toBool())
                {
                    return true;
                }
            }
            return false;
        }(),
        15'000);

    AiActionToolDispatcher dispatcher;
    AiAgentTurnBudget budget;
    const std::string firstCommand = "printf 'ZTERMY_SSH_%s\\n' 'AGENT_DONE'";
    const auto first = dispatcher.prepare(commandCall("ssh-agent-first", firstCommand), context(), budget);
    QCOMPARE(first.disposition, AiActionToolDisposition::execute);
    QVERIFY(first.action.has_value());
    session.queueInput(QByteArray::fromStdString(first.action.value_or(AiTerminalAction{}).command + '\r'));
    QTRY_VERIFY_WITH_TIMEOUT(output->snapshot().contains("ZTERMY_SSH_AGENT_DONE"), 10'000);

    const std::string longCommand = "sleep 1; printf 'ZTERMY_SSH_%s\\n' 'AGENT_LONG_DONE'";
    const auto longPlan = dispatcher.prepare(commandCall("ssh-agent-long", longCommand), context(), budget);
    QCOMPARE(longPlan.disposition, AiActionToolDisposition::execute);
    QVERIFY(longPlan.action.has_value());
    session.queueInput(QByteArray::fromStdString(longPlan.action.value_or(AiTerminalAction{}).command + '\r'));
    session.queueInput(QByteArrayLiteral("printf 'ZTERMY_SSH_%s\\n' 'USER_DONE'\r"));
    QTRY_VERIFY_WITH_TIMEOUT(output->snapshot().contains("ZTERMY_SSH_USER_DONE"), 10'000);
    const QByteArray orderedOutput = output->snapshot();
    const auto agentPosition = orderedOutput.indexOf("ZTERMY_SSH_AGENT_LONG_DONE");
    const auto userPosition = orderedOutput.indexOf("ZTERMY_SSH_USER_DONE");
    QVERIFY(agentPosition >= 0);
    QVERIFY(userPosition > agentPosition);

    const std::string interactiveCommand =
        R"(printf 'AgentPrompt: '; IFS= read -r answer; printf 'ZTERMY_SSH_INTERACTIVE_%s\n' "$answer")";
    const auto interactive =
        dispatcher.prepare(commandCall("ssh-agent-interactive", interactiveCommand), context(), budget);
    QCOMPARE(interactive.disposition, AiActionToolDisposition::execute);
    QVERIFY(interactive.action.has_value());
    session.queueInput(QByteArray::fromStdString(interactive.action.value_or(AiTerminalAction{}).command + '\r'));
    QTRY_VERIFY_WITH_TIMEOUT(output->snapshot().contains("AgentPrompt:"), 10'000);

    const auto answer = dispatcher.prepare(ptyCall("ssh-agent-answer", "agent-answer"), context(), budget);
    QCOMPARE(answer.disposition, AiActionToolDisposition::execute);
    QVERIFY(answer.action.has_value());
    const auto answerAction = answer.action.value_or(AiTerminalAction{});
    session.queueInput(QByteArray::fromStdString(answerAction.ptyData + (answerAction.appendEnter ? "\r" : "")));
    QTRY_VERIFY_WITH_TIMEOUT(output->snapshot().contains("ZTERMY_SSH_INTERACTIVE_agent-answer"), 10'000);
    QVERIFY(!output->failed());

    session.stop();
}

} // namespace

QTEST_GUILESS_MAIN(AiAgentRealHostTests)

#include "ai_agent_real_host_tests.moc"
