#include "application/ai/AcpAgentTurnRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr std::size_t maximumCommandBytes = std::size_t{16} * 1024;
constexpr std::size_t maximumOutputByteLimit = std::size_t{4} * 1024 * 1024;
constexpr qsizetype maximumArguments = 256;
constexpr qsizetype maximumEnvironmentVariables = 128;

[[nodiscard]] AiProviderError protocolError(const QString &message)
{
    const QByteArray encoded = message.toUtf8();
    return {.code = AiProviderErrorCode::protocol,
            .message = std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())),
            .retryable = false};
}

[[nodiscard]] AiProviderError cancellationError()
{
    return {.code = AiProviderErrorCode::cancelled, .message = "The ACP Agent turn was cancelled.", .retryable = false};
}

[[nodiscard]] std::string utf8(const QString &value)
{
    const QByteArray encoded = value.toUtf8();
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

[[nodiscard]] QString compactJson(const QJsonValue &value)
{
    if (value.isObject())
    {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray())
    {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    return {};
}

[[nodiscard]] bool validSession(const AcpMessage &message, const QString &sessionId)
{
    return message.params.value(QStringLiteral("sessionId")).toString() == sessionId;
}

[[nodiscard]] std::optional<std::size_t> outputLimit(const QJsonValue &value)
{
    if (value.isUndefined())
    {
        return std::size_t{1024} * 1024;
    }
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 1.0 || std::floor(number) != number
        || number > static_cast<double>(maximumOutputByteLimit))
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(number);
}

[[nodiscard]] bool validText(const QString &value, const bool allowEmpty = false)
{
    return (allowEmpty || !value.isEmpty()) && !value.contains(QChar::Null)
           && std::cmp_less_equal(value.toUtf8().size(), maximumCommandBytes);
}

[[nodiscard]] QString providerMessage(const AiProviderError &error)
{
    return QString::fromUtf8(error.message.data(), static_cast<qsizetype>(error.message.size()));
}

[[nodiscard]] QString posixQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

[[nodiscard]] QString powerShellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

[[nodiscard]] std::expected<QString, QString> terminalCommand(const QJsonObject &params, const AcpTerminalShell shell)
{
    const QString command = params.value(QStringLiteral("command")).toString();
    const QJsonArray arguments = params.value(QStringLiteral("args")).toArray();
    const QJsonArray environment = params.value(QStringLiteral("env")).toArray();
    const QString directory = params.value(QStringLiteral("cwd")).toString();
    if (!validText(command) || arguments.size() > maximumArguments || environment.size() > maximumEnvironmentVariables
        || (!params.value(QStringLiteral("args")).isUndefined() && !params.value(QStringLiteral("args")).isArray())
        || (!params.value(QStringLiteral("env")).isUndefined() && !params.value(QStringLiteral("env")).isArray())
        || (!params.value(QStringLiteral("cwd")).isUndefined()
            && (!params.value(QStringLiteral("cwd")).isString() || !validText(directory))))
    {
        return std::unexpected(QStringLiteral("The ACP terminal command is invalid or exceeds ztermy limits."));
    }

    QStringList tokens{command};
    for (const auto &argument : arguments)
    {
        if (!argument.isString() || !validText(argument.toString(), true))
        {
            return std::unexpected(QStringLiteral("The ACP terminal argument is invalid or too long."));
        }
        tokens.push_back(argument.toString());
    }

    struct Environment final
    {
        QString name;
        QString value;
    };
    std::vector<Environment> variables;
    variables.reserve(static_cast<std::size_t>(environment.size()));
    static const QRegularExpression environmentName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (const auto &entry : environment)
    {
        const QJsonObject variable = entry.toObject();
        const QString name = variable.value(QStringLiteral("name")).toString();
        const QString value = variable.value(QStringLiteral("value")).toString();
        if (!entry.isObject() || variable.size() != 2 || !environmentName.match(name).hasMatch()
            || !variable.value(QStringLiteral("value")).isString() || !validText(value, true))
        {
            return std::unexpected(QStringLiteral("The ACP terminal environment is invalid or too large."));
        }
        variables.push_back({.name = name, .value = value});
    }

    QString built;
    if (shell == AcpTerminalShell::posix)
    {
        QStringList quoted;
        quoted.reserve(tokens.size());
        std::ranges::transform(tokens, std::back_inserter(quoted), posixQuote);
        QString invocation = quoted.join(QLatin1Char(' '));
        if (!variables.empty())
        {
            QStringList values;
            values.reserve(static_cast<qsizetype>(variables.size()));
            for (const auto &variable : variables)
            {
                values.push_back(posixQuote(variable.name + QLatin1Char('=') + variable.value));
            }
            invocation = QStringLiteral("env ") + values.join(QLatin1Char(' ')) + QLatin1Char(' ') + invocation;
        }
        if (!directory.isEmpty())
        {
            invocation = QStringLiteral("cd -- %1 && %2").arg(posixQuote(directory), invocation);
        }
        built =
            variables.empty() && directory.isEmpty() ? invocation : QLatin1Char('(') + invocation + QLatin1Char(')');
    }
    else
    {
        QStringList quoted;
        quoted.reserve(tokens.size());
        std::ranges::transform(tokens, std::back_inserter(quoted), powerShellQuote);
        QString invocation = QStringLiteral("& ") + quoted.join(QLatin1Char(' '));
        if (!directory.isEmpty() || !variables.empty())
        {
            QString setup;
            QString restore;
            if (!variables.empty())
            {
                setup += QStringLiteral("$ztermyEnv=@{}; ");
                for (const auto &variable : variables)
                {
                    const QString name = powerShellQuote(variable.name);
                    setup += QStringLiteral("$ztermyEnv[%1]=[Environment]::GetEnvironmentVariable(%1,'Process'); "
                                            "[Environment]::SetEnvironmentVariable(%1,%2,'Process'); ")
                                 .arg(name, powerShellQuote(variable.value));
                }
                restore = QStringLiteral(
                    " foreach($ztermyEntry in $ztermyEnv.GetEnumerator())"
                    "{[Environment]::SetEnvironmentVariable($ztermyEntry.Key,$ztermyEntry.Value,'Process')}");
            }
            if (!directory.isEmpty())
            {
                setup += QStringLiteral("Push-Location -LiteralPath %1; ").arg(powerShellQuote(directory));
                restore = QStringLiteral(" Pop-Location;") + restore;
            }
            built = QStringLiteral("& { %1try { %2 } finally {%3 } }").arg(setup, invocation, restore);
        }
        else
        {
            built = invocation;
        }
    }
    if (std::cmp_greater(built.toUtf8().size(), maximumCommandBytes))
    {
        return std::unexpected(QStringLiteral("The encoded ACP terminal command exceeds 16 KiB."));
    }
    return built;
}

[[nodiscard]] std::expected<QJsonObject, QString> toolResult(const std::string_view json)
{
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())), &error);
    if (!document.isObject() || error.error != QJsonParseError::NoError)
    {
        return std::unexpected(QStringLiteral("The terminal dispatcher returned invalid JSON."));
    }
    QJsonObject object = document.object();
    if (!object.value(QStringLiteral("ok")).toBool())
    {
        const QString message = object.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString(QStringLiteral("The terminal action failed."));
        return std::unexpected(message);
    }
    return object;
}

[[nodiscard]] std::string truncateFromBeginning(std::string value, const std::size_t limit, bool &truncated)
{
    if (value.size() <= limit)
    {
        return value;
    }
    std::size_t first = value.size() - limit;
    while (first < value.size() && (static_cast<unsigned char>(value[first]) & 0xC0U) == 0x80U)
    {
        ++first;
    }
    value.erase(0, first);
    truncated = true;
    return value;
}

[[nodiscard]] QJsonObject terminalSnapshotResult(AcpTerminalSnapshot snapshot, const std::size_t limit)
{
    bool truncated = snapshot.truncated;
    snapshot.output = truncateFromBeginning(std::move(snapshot.output), limit, truncated);
    QJsonObject result{{QStringLiteral("output"), QString::fromUtf8(snapshot.output)},
                       {QStringLiteral("truncated"), truncated}};
    if (snapshot.exited)
    {
        result.insert(QStringLiteral("exitStatus"),
                      QJsonObject{{QStringLiteral("exitCode"),
                                   snapshot.exitCode.has_value() ? QJsonValue{*snapshot.exitCode} : QJsonValue::Null},
                                  {QStringLiteral("signal"),
                                   snapshot.signal.isEmpty() ? QJsonValue::Null : QJsonValue{snapshot.signal}}});
    }
    return result;
}

[[nodiscard]] QJsonObject terminalExitResult(const AcpTerminalSnapshot &snapshot)
{
    return {
        {QStringLiteral("exitCode"), snapshot.exitCode.has_value() ? QJsonValue{*snapshot.exitCode} : QJsonValue::Null},
        {QStringLiteral("signal"), snapshot.signal.isEmpty() ? QJsonValue::Null : QJsonValue{snapshot.signal}}};
}

[[nodiscard]] std::uint64_t elapsedMilliseconds(const std::chrono::steady_clock::time_point start,
                                                const std::chrono::steady_clock::time_point end)
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
}

} // namespace

AcpAgentTurnRunner::AcpAgentTurnRunner(QObject *parent) : QObject(parent)
{
    m_terminalWaitTimer.setInterval(50);
    QObject::connect(&m_terminalWaitTimer, &QTimer::timeout, this, [this] {
        pollTerminalWait();
    });
}

AcpAgentTurnRunner::~AcpAgentTurnRunner()
{
    stop();
}

std::expected<AcpAgentTurnRunner::TurnId, AiProviderError> AcpAgentTurnRunner::startConfigured(
    AcpClientConfiguration configuration, std::string prompt, const AcpTerminalShell shell, EventHandler eventHandler,
    FinishedHandler finishedHandler, ToolHandler toolHandler, ToolOutputHandler toolOutputHandler,
    TerminalObserver terminalObserver, PermissionHandler permissionHandler,
    PermissionPendingHandler permissionPendingHandler, ActivityHandler activityHandler, UsageHandler usageHandler)
{
    if (active() || prompt.empty() || !toolHandler || !terminalObserver)
    {
        return std::unexpected(protocolError(QStringLiteral("The ACP Agent cannot start this turn.")));
    }
    stop();
    m_mapper.reset();
    m_eventHandler = std::move(eventHandler);
    m_finishedHandler = std::move(finishedHandler);
    m_toolHandler = std::move(toolHandler);
    m_toolOutputHandler = std::move(toolOutputHandler);
    m_terminalObserver = std::move(terminalObserver);
    m_permissionHandler = std::move(permissionHandler);
    m_permissionPendingHandler = std::move(permissionPendingHandler);
    m_activityHandler = std::move(activityHandler);
    m_usageHandler = std::move(usageHandler);
    m_pendingPrompt = std::move(prompt);
    m_shell = shell;
    m_startedAt = std::chrono::steady_clock::now();
    m_firstTokenAt.reset();
    m_turnId = m_nextTurnId++;
    m_starting = true;

    auto started = m_client.start(
        std::move(configuration),
        [this](auto ready) {
            if (!active())
            {
                return;
            }
            if (!ready.has_value())
            {
                finishWithError(protocolError(ready.error()));
                return;
            }
            m_starting = false;
            if (m_eventHandler)
            {
                m_eventHandler(m_turnId, AiStreamEvent{.type = AiStreamEventType::responseStarted});
            }
            auto prompt = m_client.startPrompt(m_pendingPrompt, [this](auto completion) {
                if (!active())
                {
                    return;
                }
                if (!completion.has_value())
                {
                    finishWithError(protocolError(completion.error()));
                    return;
                }
                if (m_eventHandler)
                {
                    m_eventHandler(m_turnId, AiStreamEvent{.type = AiStreamEventType::responseCompleted});
                }
                finishTurn();
            });
            m_pendingPrompt.clear();
            if (!prompt.has_value())
            {
                finishWithError(protocolError(prompt.error()));
            }
        },
        [this](auto message) {
            handleUpdate(std::move(message));
        },
        [this](const AcpMessage &message) {
            handleRequest(message);
        });
    if (!started.has_value())
    {
        const auto error = protocolError(started.error());
        clearTurn();
        return std::unexpected(error);
    }
    return m_turnId;
}

bool AcpAgentTurnRunner::cancel()
{
    if (!active())
    {
        return false;
    }
    if (m_pendingTool.has_value())
    {
        if (m_pendingTool->cancel)
        {
            m_pendingTool->cancel();
        }
        failRequest(m_pendingTool->requestId, QStringLiteral("The ACP terminal request was cancelled."));
        m_pendingTool.reset();
    }
    if (m_pendingPermission.has_value())
    {
        const auto completed = m_client.completeRequest(
            m_pendingPermission->requestId,
            QJsonObject{
                {QStringLiteral("outcome"), QJsonObject{{QStringLiteral("outcome"), QStringLiteral("cancelled")}}}});
        m_pendingPermission.reset();
        if (!completed.has_value())
        {
            finishWithError(protocolError(completed.error()));
            return true;
        }
    }
    if (m_pendingWait.has_value())
    {
        failRequest(m_pendingWait->requestId, QStringLiteral("The ACP terminal wait was cancelled."));
        m_pendingWait.reset();
        m_terminalWaitTimer.stop();
    }
    if (m_starting)
    {
        m_client.stop();
        finishWithError(cancellationError());
        return true;
    }
    const auto cancelled = m_client.cancelPrompt();
    if (!cancelled.has_value())
    {
        finishWithError(protocolError(cancelled.error()));
    }
    return true;
}

bool AcpAgentTurnRunner::completePendingTool(const AiToolOutput &output)
{
    if (!active() || !m_pendingTool.has_value() || output.callId != m_pendingTool->call.id
        || output.name != m_pendingTool->call.name)
    {
        return false;
    }
    auto pending = std::move(*m_pendingTool);
    m_pendingTool.reset();
    finishTool(pending, output);
    return true;
}

bool AcpAgentTurnRunner::completePendingPermission(const QString &optionId)
{
    if (!active() || !m_pendingPermission.has_value())
    {
        return false;
    }
    const auto &options = m_pendingPermission->request.options;
    if (std::ranges::none_of(options, [&optionId](const auto &option) {
            return option.id == optionId;
        }))
    {
        return false;
    }
    const auto selected = std::ranges::find(options, optionId, &AcpPermissionOption::id);
    if (selected->kind == QStringLiteral("allow_once"))
    {
        m_allowNextTerminalCreate = true;
    }
    else if (selected->kind == QStringLiteral("allow_always"))
    {
        m_alwaysAllowTerminalCreate = true;
    }
    const QJsonValue requestId = m_pendingPermission->requestId;
    m_pendingPermission.reset();
    const auto completed = m_client.completeRequest(
        requestId,
        QJsonObject{{QStringLiteral("outcome"), QJsonObject{{QStringLiteral("outcome"), QStringLiteral("selected")},
                                                            {QStringLiteral("optionId"), optionId}}}});
    if (!completed.has_value())
    {
        finishWithError(protocolError(completed.error()));
        return false;
    }
    return true;
}

std::optional<AiToolCall> AcpAgentTurnRunner::pendingToolCall() const
{
    return m_pendingTool.has_value() ? std::optional<AiToolCall>{m_pendingTool->call} : std::nullopt;
}

std::optional<AcpPermissionRequest> AcpAgentTurnRunner::pendingPermission() const
{
    return m_pendingPermission.has_value() ? std::optional<AcpPermissionRequest>{m_pendingPermission->request}
                                           : std::nullopt;
}

bool AcpAgentTurnRunner::active() const noexcept
{
    return m_turnId != 0;
}

AcpAgentTurnRunner::TurnId AcpAgentTurnRunner::activeTurnId() const noexcept
{
    return m_turnId;
}

QString AcpAgentTurnRunner::sessionId() const
{
    return m_client.sessionId();
}

void AcpAgentTurnRunner::stop()
{
    if (m_pendingTool.has_value() && m_pendingTool->cancel)
    {
        m_pendingTool->cancel();
    }
    m_terminalWaitTimer.stop();
    m_client.stop();
    m_mapper.reset();
    m_terminals.clear();
    m_allowNextTerminalCreate = false;
    m_alwaysAllowTerminalCreate = false;
    clearTurn();
}

void AcpAgentTurnRunner::handleUpdate(std::expected<AcpMessage, QString> message)
{
    if (!message.has_value())
    {
        finishWithError(protocolError(message.error()));
        return;
    }
    auto mapped = m_mapper.map(*message);
    if (!mapped.has_value())
    {
        finishWithError(protocolError(mapped.error()));
        return;
    }
    for (const AiStreamEvent &event : mapped->streamEvents)
    {
        if (!m_firstTokenAt.has_value()
            && (event.type == AiStreamEventType::textDelta || event.type == AiStreamEventType::reasoningDelta))
        {
            m_firstTokenAt = std::chrono::steady_clock::now();
        }
        if (m_eventHandler)
        {
            m_eventHandler(m_turnId, event);
        }
    }
    if (mapped->toolActivity.has_value() && m_activityHandler)
    {
        m_activityHandler(*mapped->toolActivity);
    }
    if (mapped->usage.has_value() && m_usageHandler)
    {
        m_usageHandler(*mapped->usage);
    }
}

void AcpAgentTurnRunner::handleRequest(const AcpMessage &message)
{
    if (!active() || !message.hasId)
    {
        return;
    }
    if (!validSession(message, m_client.sessionId()))
    {
        failRequest(message.id, QStringLiteral("The ACP request targets a different session."));
        return;
    }
    if (m_pendingTool.has_value() || m_pendingPermission.has_value() || m_pendingWait.has_value())
    {
        failRequest(message.id, QStringLiteral("Another ACP client request is still pending."));
        return;
    }
    if (message.method == QStringLiteral("terminal/create"))
    {
        handleTerminalCreate(message);
    }
    else if (message.method == QStringLiteral("terminal/output"))
    {
        handleTerminalOutput(message);
    }
    else if (message.method == QStringLiteral("terminal/wait_for_exit"))
    {
        handleTerminalWait(message);
    }
    else if (message.method == QStringLiteral("terminal/kill"))
    {
        handleTerminalKill(message, false);
    }
    else if (message.method == QStringLiteral("terminal/release"))
    {
        handleTerminalKill(message, true);
    }
    else if (message.method == QStringLiteral("session/request_permission"))
    {
        handlePermissionRequest(message);
    }
    else
    {
        failRequest(message.id, QStringLiteral("The ACP client method is not supported by ztermy."));
    }
}

void AcpAgentTurnRunner::handleTerminalCreate(const AcpMessage &message)
{
    const auto limit = outputLimit(message.params.value(QStringLiteral("outputByteLimit")));
    const auto command = terminalCommand(message.params, m_shell);
    if (!limit.has_value() || !command.has_value())
    {
        failRequest(message.id,
                    command.has_value() ? QStringLiteral("The ACP output byte limit is invalid.") : command.error());
        return;
    }
    const bool authorized = m_alwaysAllowTerminalCreate || m_allowNextTerminalCreate;
    m_allowNextTerminalCreate = false;
    const QString callId = QStringLiteral("acp-terminal-create-%1%2")
                               .arg(authorized ? QStringLiteral("authorized-") : QString{})
                               .arg(m_nextToolCallId++);
    const QJsonObject arguments{{QStringLiteral("command"), *command}};
    dispatchTool(PendingTool{
        .purpose = ToolPurpose::create,
        .requestId = message.id,
        .call = AiToolCall{.id = utf8(callId), .name = "run_command", .argumentsJson = utf8(compactJson(arguments))},
        .outputByteLimit = *limit});
}

void AcpAgentTurnRunner::handleTerminalOutput(const AcpMessage &message)
{
    const QString terminalId = message.params.value(QStringLiteral("terminalId")).toString();
    const auto terminal = m_terminals.constFind(terminalId);
    if (terminal == m_terminals.cend())
    {
        failRequest(message.id, QStringLiteral("The ACP terminal id is unknown or released."));
        return;
    }
    auto snapshot = m_terminalObserver(terminal->commandId);
    if (!snapshot.has_value())
    {
        failRequest(message.id, providerMessage(snapshot.error()));
        return;
    }
    const auto completed =
        m_client.completeRequest(message.id, terminalSnapshotResult(std::move(*snapshot), terminal->outputByteLimit));
    if (!completed.has_value())
    {
        finishWithError(protocolError(completed.error()));
    }
}

void AcpAgentTurnRunner::handleTerminalWait(const AcpMessage &message)
{
    const QString terminalId = message.params.value(QStringLiteral("terminalId")).toString();
    const auto terminal = m_terminals.constFind(terminalId);
    if (terminal == m_terminals.cend())
    {
        failRequest(message.id, QStringLiteral("The ACP terminal id is unknown or released."));
        return;
    }
    auto snapshot = m_terminalObserver(terminal->commandId);
    if (!snapshot.has_value())
    {
        failRequest(message.id, providerMessage(snapshot.error()));
        return;
    }
    if (snapshot->exited)
    {
        const auto completed = m_client.completeRequest(message.id, terminalExitResult(*snapshot));
        if (!completed.has_value())
        {
            finishWithError(protocolError(completed.error()));
        }
        return;
    }
    m_pendingWait = PendingWait{.requestId = message.id, .terminalId = terminalId};
    m_terminalWaitTimer.start();
}

void AcpAgentTurnRunner::handleTerminalKill(const AcpMessage &message, const bool release)
{
    const QString terminalId = message.params.value(QStringLiteral("terminalId")).toString();
    const auto terminal = m_terminals.constFind(terminalId);
    if (terminal == m_terminals.cend())
    {
        failRequest(message.id, QStringLiteral("The ACP terminal id is unknown or released."));
        return;
    }
    auto snapshot = m_terminalObserver(terminal->commandId);
    if (!snapshot.has_value())
    {
        failRequest(message.id, providerMessage(snapshot.error()));
        return;
    }
    if (snapshot->exited)
    {
        if (release)
        {
            m_terminals.remove(terminalId);
        }
        const auto completed = m_client.completeRequest(message.id, QJsonObject{});
        if (!completed.has_value())
        {
            finishWithError(protocolError(completed.error()));
        }
        return;
    }
    const QString callId = QStringLiteral("acp-terminal-%1-authorized-%2")
                               .arg(release ? QStringLiteral("release") : QStringLiteral("kill"))
                               .arg(m_nextToolCallId++);
    const QJsonObject arguments{{QStringLiteral("command_id"), QString::fromUtf8(terminal->commandId)},
                                {QStringLiteral("mode"), QStringLiteral("soft")}};
    dispatchTool(PendingTool{
        .purpose = release ? ToolPurpose::release : ToolPurpose::kill,
        .requestId = message.id,
        .call =
            AiToolCall{.id = utf8(callId), .name = "interrupt_command", .argumentsJson = utf8(compactJson(arguments))},
        .terminalId = terminalId});
}

void AcpAgentTurnRunner::handlePermissionRequest(const AcpMessage &message)
{
    const QJsonObject toolCall = message.params.value(QStringLiteral("toolCall")).toObject();
    const QJsonArray values = message.params.value(QStringLiteral("options")).toArray();
    AcpPermissionRequest request{.toolCallId = toolCall.value(QStringLiteral("toolCallId")).toString(),
                                 .title = toolCall.value(QStringLiteral("title")).toString(),
                                 .kind = toolCall.value(QStringLiteral("kind")).toString(),
                                 .detailsJson = compactJson(toolCall)};
    if (request.toolCallId.isEmpty() || values.isEmpty() || values.size() > 16)
    {
        failRequest(message.id, QStringLiteral("The ACP permission request is invalid."));
        return;
    }
    for (const auto &value : values)
    {
        const QJsonObject option = value.toObject();
        AcpPermissionOption parsed{.id = option.value(QStringLiteral("optionId")).toString(),
                                   .name = option.value(QStringLiteral("name")).toString(),
                                   .kind = option.value(QStringLiteral("kind")).toString()};
        if (!value.isObject() || parsed.id.isEmpty() || parsed.name.isEmpty()
            || (parsed.kind != QStringLiteral("allow_once") && parsed.kind != QStringLiteral("allow_always")
                && parsed.kind != QStringLiteral("reject_once") && parsed.kind != QStringLiteral("reject_always")))
        {
            failRequest(message.id, QStringLiteral("The ACP permission options are invalid."));
            return;
        }
        request.options.push_back(std::move(parsed));
    }
    if (!m_permissionHandler)
    {
        failRequest(message.id, QStringLiteral("ACP permission handling is unavailable."));
        return;
    }
    auto decision = m_permissionHandler(request);
    if (!decision.has_value())
    {
        failRequest(message.id, providerMessage(decision.error()));
        return;
    }
    if (decision->has_value())
    {
        m_pendingPermission = PendingPermission{.requestId = message.id, .request = request};
        if (!completePendingPermission(**decision))
        {
            finishWithError(protocolError(QStringLiteral("The ACP permission decision was invalid.")));
        }
        return;
    }
    m_pendingPermission = PendingPermission{.requestId = message.id, .request = std::move(request)};
    if (m_permissionPendingHandler)
    {
        m_permissionPendingHandler(m_pendingPermission->request);
    }
}

void AcpAgentTurnRunner::dispatchTool(PendingTool pending)
{
    if (!m_toolHandler)
    {
        failRequest(pending.requestId, QStringLiteral("The terminal dispatcher is unavailable."));
        return;
    }
    auto handled = m_toolHandler(pending.call);
    if (!handled.has_value())
    {
        failRequest(pending.requestId, providerMessage(handled.error()));
        return;
    }
    if (handled->output.has_value())
    {
        const AiToolOutput output = std::move(*handled->output);
        finishTool(pending, output);
        return;
    }
    pending.cancel = std::move(handled->cancel);
    m_pendingTool = std::move(pending);
}

void AcpAgentTurnRunner::finishTool(const PendingTool &pending, const AiToolOutput &output)
{
    auto result = toolResult(output.outputJson);
    if (!result.has_value())
    {
        failRequest(pending.requestId, result.error());
        return;
    }
    if (m_toolOutputHandler)
    {
        m_toolOutputHandler(pending.call, output);
    }
    QJsonObject response;
    if (pending.purpose == ToolPurpose::create)
    {
        const QString commandId = result->value(QStringLiteral("command_id")).toString();
        if (commandId.isEmpty())
        {
            failRequest(pending.requestId, QStringLiteral("The terminal dispatcher returned no command id."));
            return;
        }
        const QString terminalId = QStringLiteral("ztermy-terminal-%1").arg(m_nextTerminalId++);
        m_terminals.insert(terminalId,
                           TerminalHandle{.commandId = utf8(commandId), .outputByteLimit = pending.outputByteLimit});
        response.insert(QStringLiteral("terminalId"), terminalId);
    }
    else if (pending.purpose == ToolPurpose::release)
    {
        m_terminals.remove(pending.terminalId);
    }
    const auto completed = m_client.completeRequest(pending.requestId, response);
    if (!completed.has_value())
    {
        finishWithError(protocolError(completed.error()));
    }
}

void AcpAgentTurnRunner::pollTerminalWait()
{
    if (!active() || !m_pendingWait.has_value())
    {
        m_terminalWaitTimer.stop();
        return;
    }
    const auto terminal = m_terminals.constFind(m_pendingWait->terminalId);
    if (terminal == m_terminals.cend())
    {
        const QJsonValue requestId = m_pendingWait->requestId;
        m_pendingWait.reset();
        m_terminalWaitTimer.stop();
        failRequest(requestId, QStringLiteral("The ACP terminal was released while waiting."));
        return;
    }
    auto snapshot = m_terminalObserver(terminal->commandId);
    if (!snapshot.has_value())
    {
        const QJsonValue requestId = m_pendingWait->requestId;
        m_pendingWait.reset();
        m_terminalWaitTimer.stop();
        failRequest(requestId, providerMessage(snapshot.error()));
        return;
    }
    if (!snapshot->exited)
    {
        return;
    }
    const QJsonValue requestId = m_pendingWait->requestId;
    m_pendingWait.reset();
    m_terminalWaitTimer.stop();
    const auto completed = m_client.completeRequest(requestId, terminalExitResult(*snapshot));
    if (!completed.has_value())
    {
        finishWithError(protocolError(completed.error()));
    }
}

void AcpAgentTurnRunner::failRequest(const QJsonValue &id, const QString &message)
{
    const auto failed = m_client.failRequest(id, -32000, utf8(message));
    if (!failed.has_value() && active())
    {
        finishWithError(protocolError(failed.error()));
    }
}

void AcpAgentTurnRunner::finishWithError(AiProviderError error)
{
    if (!active())
    {
        return;
    }
    if (m_eventHandler)
    {
        m_eventHandler(m_turnId, AiStreamEvent{.type = AiStreamEventType::responseFailed, .error = std::move(error)});
    }
    finishTurn();
}

void AcpAgentTurnRunner::finishTurn()
{
    const TurnId finishedTurn = m_turnId;
    const AiTurnMetrics finishedMetrics = metrics();
    auto handler = std::move(m_finishedHandler);
    m_terminalWaitTimer.stop();
    clearTurn();
    if (handler)
    {
        handler(finishedTurn, finishedMetrics);
    }
}

void AcpAgentTurnRunner::clearTurn()
{
    m_eventHandler = {};
    m_finishedHandler = {};
    m_toolHandler = {};
    m_toolOutputHandler = {};
    m_terminalObserver = {};
    m_permissionHandler = {};
    m_permissionPendingHandler = {};
    m_activityHandler = {};
    m_usageHandler = {};
    m_pendingTool.reset();
    m_pendingPermission.reset();
    m_pendingWait.reset();
    m_pendingPrompt.clear();
    m_firstTokenAt.reset();
    m_turnId = 0;
    m_starting = false;
}

AiTurnMetrics AcpAgentTurnRunner::metrics() const
{
    const auto finishedAt = std::chrono::steady_clock::now();
    return {.wallTimeMilliseconds = elapsedMilliseconds(m_startedAt, finishedAt),
            .firstTokenMilliseconds =
                m_firstTokenAt.has_value()
                    ? std::optional<std::uint64_t>{elapsedMilliseconds(m_startedAt, *m_firstTokenAt)}
                    : std::nullopt,
            .retryCount = 0};
}

} // namespace ztermy::ai
