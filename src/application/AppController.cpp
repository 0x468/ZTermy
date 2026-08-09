#include "application/AppController.h"

#include "domain/ssh/SshTarget.h"
#include "infrastructure/security/InMemoryCredentialVault.h"
#include "infrastructure/workbench/QuickCommandStore.h"
#include "ui/terminal/TerminalItem.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

Q_LOGGING_CATEGORY(appControllerLog, "ztermy.application.controller")

namespace
{

constexpr std::size_t maximumRecentHostProfiles = 6;

class TerminalOutputFanout final : public ztermy::terminal::TerminalOutputSink
{
public:
    using Observer = std::function<void(std::span<const std::byte>)>;

    TerminalOutputFanout(std::shared_ptr<ztermy::terminal::TerminalOutputSink> primary, Observer observer)
        : m_primary(std::move(primary)), m_observer(std::move(observer))
    {
    }

    void append(const std::span<const std::byte> bytes) noexcept override
    {
        try
        {
            if (m_primary)
            {
                m_primary->append(bytes);
            }
            if (m_observer)
            {
                m_observer(bytes);
            }
        }
        catch (...)
        {
            // Terminal output must never be allowed to unwind into a session worker.
        }
    }

private:
    std::shared_ptr<ztermy::terminal::TerminalOutputSink> m_primary;
    Observer m_observer;
};

[[nodiscard]] ztermy::workbench::ScriptRecorder::TimePoint recorderNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

[[nodiscard]] ztermy::workbench::ScriptExecution::TimePoint scriptExecutionNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

[[nodiscard]] QString scriptRecorderStateToken(const ztermy::workbench::ScriptRecorderState state)
{
    using ztermy::workbench::ScriptRecorderState;
    switch (state)
    {
        case ScriptRecorderState::Idle:
            return QStringLiteral("idle");
        case ScriptRecorderState::Recording:
            return QStringLiteral("recording");
        case ScriptRecorderState::Paused:
            return QStringLiteral("paused");
        case ScriptRecorderState::Review:
            return QStringLiteral("review");
    }
    return QStringLiteral("idle");
}

[[nodiscard]] QString applicationDataFile(const QString &fileName)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(fileName);
}

[[nodiscard]] QString siblingKnownHostsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("known_hosts.json"));
}

[[nodiscard]] QString siblingSettingsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("settings.json"));
}

[[nodiscard]] QString siblingCredentialsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("credentials.zvlt"));
}

[[nodiscard]] QString siblingPortForwardingFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("port_forwarding.json"));
}

[[nodiscard]] QString siblingQuickCommandsFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("quick_commands.json"));
}

[[nodiscard]] QString siblingScriptsFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("scripts.json"));
}

[[nodiscard]] QString siblingWorkspaceStateFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("workspace_state.json"));
}

[[nodiscard]] QString siblingTransferRecoveryFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("transfer_recovery.json"));
}

[[nodiscard]] QString siblingTransferBatchRecoveryFile(const QString &settingsPath)
{
    return QFileInfo(settingsPath).dir().filePath(QStringLiteral("transfer_batch_recovery.json"));
}

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString authenticationToken(const ztermy::ssh::SshAuthenticationMethod authentication)
{
    switch (authentication)
    {
        case ztermy::ssh::SshAuthenticationMethod::PrivateKey:
            return QStringLiteral("private-key");
        case ztermy::ssh::SshAuthenticationMethod::Password:
            return QStringLiteral("password");
        case ztermy::ssh::SshAuthenticationMethod::Agent:
            return QStringLiteral("agent");
    }
    return {};
}

[[nodiscard]] QString forwardingTypeToken(const ztermy::forwarding::PortForwardingType type)
{
    switch (type)
    {
        case ztermy::forwarding::PortForwardingType::Local:
            return QStringLiteral("local");
        case ztermy::forwarding::PortForwardingType::Remote:
            return QStringLiteral("remote");
        case ztermy::forwarding::PortForwardingType::Dynamic:
            return QStringLiteral("dynamic");
    }
    return {};
}

[[nodiscard]] std::optional<ztermy::forwarding::PortForwardingType> parseForwardingType(const QString &value)
{
    if (value == QStringLiteral("local"))
    {
        return ztermy::forwarding::PortForwardingType::Local;
    }
    if (value == QStringLiteral("remote"))
    {
        return ztermy::forwarding::PortForwardingType::Remote;
    }
    if (value == QStringLiteral("dynamic"))
    {
        return ztermy::forwarding::PortForwardingType::Dynamic;
    }
    return std::nullopt;
}

[[nodiscard]] QString forwardingStateToken(const ztermy::forwarding::PortForwardingJobState state)
{
    switch (state)
    {
        case ztermy::forwarding::PortForwardingJobState::Stopped:
            return QStringLiteral("stopped");
        case ztermy::forwarding::PortForwardingJobState::Starting:
            return QStringLiteral("starting");
        case ztermy::forwarding::PortForwardingJobState::Running:
            return QStringLiteral("running");
        case ztermy::forwarding::PortForwardingJobState::Failed:
            return QStringLiteral("failed");
    }
    return QStringLiteral("stopped");
}

[[nodiscard]] QString forwardingFailureToken(const ztermy::forwarding::PortForwardingJobFailure failure)
{
    using ztermy::forwarding::PortForwardingJobFailure;
    switch (failure)
    {
        case PortForwardingJobFailure::None:
            return {};
        case PortForwardingJobFailure::Connection:
            return QStringLiteral("connection");
        case PortForwardingJobFailure::Listener:
            return QStringLiteral("listener");
        case PortForwardingJobFailure::RemoteListener:
            return QStringLiteral("remote-listener");
        case PortForwardingJobFailure::Transport:
            return QStringLiteral("transport");
        case PortForwardingJobFailure::ResourceLimit:
            return QStringLiteral("resource-limit");
    }
    return QStringLiteral("transport");
}

[[nodiscard]] std::optional<ztermy::ssh::SshAuthenticationMethod> parseAuthenticationToken(const QString &value)
{
    if (value == QStringLiteral("private-key"))
    {
        return ztermy::ssh::SshAuthenticationMethod::PrivateKey;
    }
    if (value == QStringLiteral("password"))
    {
        return ztermy::ssh::SshAuthenticationMethod::Password;
    }
    if (value == QStringLiteral("agent"))
    {
        return ztermy::ssh::SshAuthenticationMethod::Agent;
    }
    return std::nullopt;
}

[[nodiscard]] QString proxyTypeToken(const ztermy::ssh::SshProxyType type)
{
    switch (type)
    {
        case ztermy::ssh::SshProxyType::None:
            return QStringLiteral("none");
        case ztermy::ssh::SshProxyType::Socks5:
            return QStringLiteral("socks5");
        case ztermy::ssh::SshProxyType::HttpConnect:
            return QStringLiteral("http-connect");
    }
    return {};
}

[[nodiscard]] std::optional<ztermy::ssh::SshProxyType> parseProxyTypeToken(const QString &value)
{
    if (value == QStringLiteral("none"))
    {
        return ztermy::ssh::SshProxyType::None;
    }
    if (value == QStringLiteral("socks5"))
    {
        return ztermy::ssh::SshProxyType::Socks5;
    }
    if (value == QStringLiteral("http-connect"))
    {
        return ztermy::ssh::SshProxyType::HttpConnect;
    }
    return std::nullopt;
}

[[nodiscard]] QString utf8QString(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString normalizedTerminalWorkingDirectory(const std::string &value)
{
    if (value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos)
    {
        return {};
    }
    const QByteArray encoded(value.data(), static_cast<qsizetype>(value.size()));
    QString path;
    if (encoded.startsWith("file://"))
    {
        const QUrl url = QUrl::fromEncoded(encoded, QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != QStringLiteral("file"))
        {
            return {};
        }
        path = url.path(QUrl::FullyDecoded);
    }
    else
    {
        path = QString::fromUtf8(encoded);
    }
    const auto normalized = ztermy::sftp::normalizeRemotePath(utf8String(path));
    return normalized ? utf8QString(*normalized) : QString{};
}

[[nodiscard]] std::optional<ztermy::workbench::ShellScope> quickCommandShellScope(const QString &value)
{
    if (value == QStringLiteral("any"))
    {
        return ztermy::workbench::ShellScope::any;
    }
    if (value == QStringLiteral("posix"))
    {
        return ztermy::workbench::ShellScope::posix;
    }
    if (value == QStringLiteral("powershell"))
    {
        return ztermy::workbench::ShellScope::powershell;
    }
    return std::nullopt;
}

[[nodiscard]] QString quickCommandShellScopeToken(const ztermy::workbench::ShellScope value)
{
    switch (value)
    {
        case ztermy::workbench::ShellScope::posix:
            return QStringLiteral("posix");
        case ztermy::workbench::ShellScope::powershell:
            return QStringLiteral("powershell");
        case ztermy::workbench::ShellScope::any:
        default:
            return QStringLiteral("any");
    }
}

[[nodiscard]] QString scriptVariableTypeToken(const ztermy::workbench::ScriptVariableType type)
{
    switch (type)
    {
        case ztermy::workbench::ScriptVariableType::integer:
            return QStringLiteral("integer");
        case ztermy::workbench::ScriptVariableType::boolean:
            return QStringLiteral("boolean");
        case ztermy::workbench::ScriptVariableType::choice:
            return QStringLiteral("choice");
        case ztermy::workbench::ScriptVariableType::text:
        default:
            return QStringLiteral("text");
    }
}

[[nodiscard]] QString scriptContinuationToken(const ztermy::workbench::ScriptContinuation continuation)
{
    return continuation == ztermy::workbench::ScriptContinuation::literalOutput ? QStringLiteral("literal-output")
                                                                                : QStringLiteral("immediate");
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptVariableType> parseScriptVariableType(const QString &value)
{
    using ztermy::workbench::ScriptVariableType;
    if (value == QStringLiteral("text"))
    {
        return ScriptVariableType::text;
    }
    if (value == QStringLiteral("integer"))
    {
        return ScriptVariableType::integer;
    }
    if (value == QStringLiteral("boolean"))
    {
        return ScriptVariableType::boolean;
    }
    if (value == QStringLiteral("choice"))
    {
        return ScriptVariableType::choice;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ztermy::workbench::ScriptContinuation> parseScriptContinuation(const QString &value)
{
    using ztermy::workbench::ScriptContinuation;
    if (value == QStringLiteral("immediate"))
    {
        return ScriptContinuation::immediate;
    }
    if (value == QStringLiteral("literal-output"))
    {
        return ScriptContinuation::literalOutput;
    }
    return std::nullopt;
}

[[nodiscard]] QString scriptExecutionStateToken(const ztermy::workbench::ScriptExecutionState state)
{
    using ztermy::workbench::ScriptExecutionState;
    switch (state)
    {
        case ScriptExecutionState::running:
            return QStringLiteral("running");
        case ScriptExecutionState::waitingForOutput:
            return QStringLiteral("waiting-output");
        case ScriptExecutionState::completed:
            return QStringLiteral("completed");
        case ScriptExecutionState::cancelled:
            return QStringLiteral("cancelled");
        case ScriptExecutionState::timedOut:
            return QStringLiteral("timed-out");
        case ScriptExecutionState::idle:
        default:
            return QStringLiteral("idle");
    }
}

[[nodiscard]] QString scriptRenderErrorToken(const ztermy::workbench::ScriptRenderError error)
{
    using ztermy::workbench::ScriptRenderError;
    switch (error)
    {
        case ScriptRenderError::missingVariable:
            return QStringLiteral("missing-variable");
        case ScriptRenderError::invalidVariableValue:
            return QStringLiteral("invalid-variable-value");
        case ScriptRenderError::invalidTemplate:
            return QStringLiteral("invalid-template");
        case ScriptRenderError::unknownTemplateVariable:
            return QStringLiteral("unknown-template-variable");
        case ScriptRenderError::renderedTooLarge:
            return QStringLiteral("rendered-too-large");
        case ScriptRenderError::invalidDefinition:
        default:
            return QStringLiteral("invalid-definition");
    }
}

[[nodiscard]] QString shellKindToken(const ztermy::workbench::ShellKind value)
{
    switch (value)
    {
        case ztermy::workbench::ShellKind::bash:
            return QStringLiteral("bash");
        case ztermy::workbench::ShellKind::zsh:
            return QStringLiteral("zsh");
        case ztermy::workbench::ShellKind::fish:
            return QStringLiteral("fish");
        case ztermy::workbench::ShellKind::powershell:
            return QStringLiteral("powershell");
        case ztermy::workbench::ShellKind::unknown:
        default:
            return QStringLiteral("unknown");
    }
}

[[nodiscard]] QString normalizedQuickCommandText(QString value)
{
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return value;
}

[[nodiscard]] bool validTerminalCommand(const QString &command)
{
    constexpr qsizetype maximumCommandCharacters = qsizetype{64} * 1024;
    if (command.trimmed().isEmpty() || command.size() > maximumCommandCharacters)
    {
        return false;
    }
    return std::ranges::none_of(command, [](const QChar character) {
        const ushort value = character.unicode();
        return (value < 0x20U && value != '\t' && value != '\n' && value != '\r') || value == 0x7FU;
    });
}

constexpr std::size_t maximumHistoryEntries = 1000;
constexpr qsizetype maximumPendingHistoryBytes = qsizetype{64} * 1024;

void removeLastUtf8CodePoint(QByteArray &value)
{
    if (value.isEmpty())
    {
        return;
    }
    qsizetype index = value.size() - 1;
    while (index > 0 && (static_cast<unsigned char>(value.at(index)) & 0xC0U) == 0x80U)
    {
        --index;
    }
    value.truncate(index);
}

[[nodiscard]] QVariantMap terminalHistoryValue(const ztermy::workbench::ShellHistoryEntry &entry,
                                               const QString &sourceLabel = {}, const QString &sourceId = {})
{
    QVariantMap value{
        {QStringLiteral("command"), utf8QString(entry.command)},
        {QStringLiteral("shell"), shellKindToken(entry.shell)},
    };
    if (entry.timestampUtcSeconds)
    {
        value.insert(QStringLiteral("timestampUtcMs"), *entry.timestampUtcSeconds * 1000);
    }
    if (!sourceLabel.isEmpty())
    {
        value.insert(QStringLiteral("sourceLabel"), sourceLabel);
    }
    if (!sourceId.isEmpty())
    {
        value.insert(QStringLiteral("sourceId"), sourceId);
    }
    return value;
}

[[nodiscard]] bool profileHasStoredCredential(const ztermy::ssh::SshProfile &profile,
                                              const std::vector<ztermy::security::CredentialKey> *availableKeys)
{
    if (profile.authentication == ztermy::ssh::SshAuthenticationMethod::Agent)
    {
        return false;
    }
    if (!profile.credentialReference)
    {
        return false;
    }
    if (availableKeys == nullptr)
    {
        // A locked or temporarily unavailable persistent backend cannot be
        // enumerated. Preserve the non-secret profile metadata so the UI can
        // offer the appropriate unlock/recovery flow.
        return true;
    }
    const ztermy::security::CredentialKey key{
        .profileId = *profile.credentialReference,
        .kind = profile.authentication == ztermy::ssh::SshAuthenticationMethod::PrivateKey
                    ? ztermy::security::CredentialKind::PrivateKeyPassphrase
                    : ztermy::security::CredentialKind::Password,
    };
    return std::ranges::find(*availableKeys, key) != availableKeys->end();
}

[[nodiscard]] bool profileHasStoredProxyCredential(const ztermy::ssh::SshProfile &profile,
                                                   const std::vector<ztermy::security::CredentialKey> *availableKeys)
{
    if (!profile.proxy.credentialReference)
    {
        return false;
    }
    if (availableKeys == nullptr)
    {
        return true;
    }
    const ztermy::security::CredentialKey key{
        .profileId = *profile.proxy.credentialReference,
        .kind = ztermy::security::CredentialKind::ProxyPassword,
    };
    return std::ranges::find(*availableKeys, key) != availableKeys->end();
}

[[nodiscard]] QVariantMap sessionOptionsVariantMap(const ztermy::ssh::SshSessionOptions &options)
{
    QVariantList environment;
    environment.reserve(static_cast<qsizetype>(options.environment.size()));
    for (const ztermy::ssh::SshEnvironmentVariable &variable : options.environment)
    {
        environment.push_back(QVariantMap{{QStringLiteral("name"), utf8QString(variable.name)},
                                          {QStringLiteral("value"), utf8QString(variable.value)}});
    }
    return {
        {QStringLiteral("terminalType"), utf8QString(options.terminalType)},
        {QStringLiteral("keepaliveIntervalSeconds"), options.keepaliveIntervalSeconds},
        {QStringLiteral("keepaliveFailureThreshold"), options.keepaliveFailureThreshold},
        {QStringLiteral("startupCommand"), utf8QString(options.startupCommand)},
        {QStringLiteral("startupCommandMode"),
         options.startupCommandMode == ztermy::ssh::SshStartupCommandMode::LineDelay ? QStringLiteral("line-delay")
                                                                                     : QStringLiteral("paste")},
        {QStringLiteral("startupLineDelayMilliseconds"), options.startupLineDelayMilliseconds},
        {QStringLiteral("environment"), environment},
        {QStringLiteral("reconnectPolicy"),
         options.reconnectPolicy == ztermy::ssh::SshReconnectPolicy::OnTransportFailure
             ? QStringLiteral("transport-failure")
             : QStringLiteral("never")},
        {QStringLiteral("reconnectMaximumAttempts"), options.reconnectMaximumAttempts},
        {QStringLiteral("reconnectInitialBackoffMilliseconds"), options.reconnectInitialBackoffMilliseconds},
    };
}

[[nodiscard]] std::optional<ztermy::ssh::SshSessionOptions> mergeSessionOptions(const QVariantMap &overrides,
                                                                                ztermy::ssh::SshSessionOptions options)
{
    const auto integerOverride = [&overrides](const QString &key, auto &target) {
        using Target = std::remove_cvref_t<decltype(target)>;
        if (!overrides.contains(key))
        {
            return true;
        }
        bool valid = false;
        const int value = overrides.value(key).toInt(&valid);
        if (!valid || value < 0 || std::cmp_greater(value, (std::numeric_limits<Target>::max)()))
        {
            return false;
        }
        target = static_cast<Target>(value);
        return true;
    };

    if (overrides.contains(QStringLiteral("terminalType")))
    {
        options.terminalType = utf8String(overrides.value(QStringLiteral("terminalType")).toString().trimmed());
    }
    if (overrides.contains(QStringLiteral("startupCommand")))
    {
        options.startupCommand = utf8String(overrides.value(QStringLiteral("startupCommand")).toString());
    }
    if (overrides.contains(QStringLiteral("startupCommandMode")))
    {
        const QString mode = overrides.value(QStringLiteral("startupCommandMode")).toString();
        if (mode == QStringLiteral("paste"))
        {
            options.startupCommandMode = ztermy::ssh::SshStartupCommandMode::Paste;
        }
        else if (mode == QStringLiteral("line-delay"))
        {
            options.startupCommandMode = ztermy::ssh::SshStartupCommandMode::LineDelay;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (overrides.contains(QStringLiteral("reconnectPolicy")))
    {
        const QString policy = overrides.value(QStringLiteral("reconnectPolicy")).toString();
        if (policy == QStringLiteral("never"))
        {
            options.reconnectPolicy = ztermy::ssh::SshReconnectPolicy::Never;
        }
        else if (policy == QStringLiteral("transport-failure"))
        {
            options.reconnectPolicy = ztermy::ssh::SshReconnectPolicy::OnTransportFailure;
        }
        else
        {
            return std::nullopt;
        }
    }
    if (!integerOverride(QStringLiteral("keepaliveIntervalSeconds"), options.keepaliveIntervalSeconds)
        || !integerOverride(QStringLiteral("keepaliveFailureThreshold"), options.keepaliveFailureThreshold)
        || !integerOverride(QStringLiteral("startupLineDelayMilliseconds"), options.startupLineDelayMilliseconds)
        || !integerOverride(QStringLiteral("reconnectMaximumAttempts"), options.reconnectMaximumAttempts)
        || !integerOverride(QStringLiteral("reconnectInitialBackoffMilliseconds"),
                            options.reconnectInitialBackoffMilliseconds))
    {
        return std::nullopt;
    }
    if (overrides.contains(QStringLiteral("environment")))
    {
        const QVariantList values = overrides.value(QStringLiteral("environment")).toList();
        options.environment.clear();
        options.environment.reserve(static_cast<std::size_t>(values.size()));
        for (const QVariant &value : values)
        {
            const QVariantMap variable = value.toMap();
            if (!variable.contains(QStringLiteral("name")) || !variable.contains(QStringLiteral("value")))
            {
                return std::nullopt;
            }
            options.environment.push_back(
                {.name = utf8String(variable.value(QStringLiteral("name")).toString().trimmed()),
                 .value = utf8String(variable.value(QStringLiteral("value")).toString())});
        }
    }
    return ztermy::ssh::validSshSessionOptions(options) ? std::optional{std::move(options)} : std::nullopt;
}

[[nodiscard]] QVariantMap proxyOptionsVariantMap(const ztermy::ssh::SshProxyOptions &options,
                                                 const bool credentialStored)
{
    return {
        {QStringLiteral("type"), proxyTypeToken(options.type)},
        {QStringLiteral("host"), utf8QString(options.host)},
        {QStringLiteral("port"), options.port},
        {QStringLiteral("username"), utf8QString(options.username)},
        {QStringLiteral("credentialStored"), credentialStored},
    };
}

[[nodiscard]] std::optional<ztermy::ssh::SshProxyOptions> mergeProxyOptions(const QVariantMap &overrides,
                                                                            ztermy::ssh::SshProxyOptions options)
{
    if (overrides.contains(QStringLiteral("type")))
    {
        const auto type = parseProxyTypeToken(overrides.value(QStringLiteral("type")).toString());
        if (!type)
        {
            return std::nullopt;
        }
        options.type = *type;
    }
    if (options.type == ztermy::ssh::SshProxyType::None)
    {
        return ztermy::ssh::SshProxyOptions{};
    }
    if (overrides.contains(QStringLiteral("host")))
    {
        options.host = utf8String(overrides.value(QStringLiteral("host")).toString().trimmed());
    }
    if (overrides.contains(QStringLiteral("port")))
    {
        bool valid = false;
        const int port = overrides.value(QStringLiteral("port")).toInt(&valid);
        if (!valid || port <= 0 || port > 65535)
        {
            return std::nullopt;
        }
        options.port = static_cast<std::uint16_t>(port);
    }
    if (overrides.contains(QStringLiteral("username")))
    {
        options.username = utf8String(overrides.value(QStringLiteral("username")).toString().trimmed());
        if (options.username.empty())
        {
            options.credentialReference.reset();
        }
    }
    return ztermy::ssh::validSshProxyOptions(options) ? std::optional{std::move(options)} : std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>>
mergeJumpProfileIds(const QVariantMap &routeOptions, std::vector<std::string> jumpProfileIds,
                    const std::string &profileId, const std::vector<ztermy::ssh::SshProfile> &profiles)
{
    const QString key = QStringLiteral("jumpProfileIds");
    if (!routeOptions.contains(key))
    {
        return jumpProfileIds;
    }
    const QVariant value = routeOptions.value(key);
    if (!value.canConvert<QVariantList>())
    {
        return std::nullopt;
    }
    const QVariantList values = value.toList();
    if (values.size() > static_cast<qsizetype>(ztermy::ssh::maximumSshJumpHostCount))
    {
        return std::nullopt;
    }
    jumpProfileIds.clear();
    jumpProfileIds.reserve(static_cast<std::size_t>(values.size()));
    for (const QVariant &entry : values)
    {
        const std::string jumpId = utf8String(entry.toString().trimmed());
        if (jumpId.empty() || jumpId == profileId || std::ranges::find(jumpProfileIds, jumpId) != jumpProfileIds.end()
            || std::ranges::find(profiles, jumpId, &ztermy::ssh::SshProfile::id) == profiles.end())
        {
            return std::nullopt;
        }
        jumpProfileIds.push_back(jumpId);
    }
    return jumpProfileIds;
}

[[nodiscard]] bool profileRequiresCredential(const ztermy::ssh::SshProfile &profile) noexcept
{
    return profile.authentication == ztermy::ssh::SshAuthenticationMethod::Password
           || (profile.authentication == ztermy::ssh::SshAuthenticationMethod::PrivateKey
               && profile.privateKeyPassphraseRequired);
}

[[nodiscard]] bool proxyRequiresCredential(const ztermy::ssh::SshProfile &profile) noexcept
{
    return profile.proxy.type != ztermy::ssh::SshProxyType::None && !profile.proxy.username.empty();
}

[[nodiscard]] ztermy::security::CredentialKind credentialKind(const ztermy::ssh::SshProfile &profile) noexcept;

[[nodiscard]] std::expected<std::vector<ztermy::ssh::SshJumpHostRequest>, ztermy::sftp::TransferCredentialError>
storedJumpHostRequests(const ztermy::ssh::SshProfile &profile, const std::vector<ztermy::ssh::SshProfile> &profiles,
                       ztermy::security::CredentialVault &vault) noexcept
{
    using ztermy::sftp::TransferCredentialError;
    try
    {
        std::vector<ztermy::ssh::SshJumpHostRequest> requests;
        requests.reserve(profile.jumpProfileIds.size());
        for (const std::string &jumpProfileId : profile.jumpProfileIds)
        {
            const auto jump = std::ranges::find(profiles, jumpProfileId, &ztermy::ssh::SshProfile::id);
            if (jump == profiles.end())
            {
                return std::unexpected(TransferCredentialError::Unavailable);
            }
            ztermy::security::SensitiveByteArray secret;
            if (jump->credentialReference)
            {
                auto stored = vault.read({.profileId = *jump->credentialReference, .kind = credentialKind(*jump)});
                if (!stored)
                {
                    return std::unexpected(stored.error() == ztermy::security::CredentialVaultError::Locked
                                               ? TransferCredentialError::Locked
                                               : TransferCredentialError::Unavailable);
                }
                secret = std::move(*stored);
            }
            if (profileRequiresCredential(*jump) && secret.empty())
            {
                return std::unexpected(TransferCredentialError::Unavailable);
            }
            ztermy::security::SensitiveByteArray proxySecret;
            if (jump->proxy.credentialReference)
            {
                auto stored = vault.read({.profileId = *jump->proxy.credentialReference,
                                          .kind = ztermy::security::CredentialKind::ProxyPassword});
                if (!stored)
                {
                    return std::unexpected(stored.error() == ztermy::security::CredentialVaultError::Locked
                                               ? TransferCredentialError::Locked
                                               : TransferCredentialError::Unavailable);
                }
                proxySecret = std::move(*stored);
            }
            if (proxyRequiresCredential(*jump) && proxySecret.empty())
            {
                return std::unexpected(TransferCredentialError::Unavailable);
            }
            requests.push_back({
                .profileId = utf8QString(jump->id),
                .displayName = utf8QString(jump->name),
                .host = utf8QString(jump->host),
                .port = jump->port,
                .username = utf8QString(jump->username),
                .authentication = jump->authentication,
                .privateKeyPath = utf8QString(jump->privateKeyPath),
                .secret = std::move(secret),
                .proxy = jump->proxy,
                .proxySecret = std::move(proxySecret),
            });
        }
        return requests;
    }
    catch (...)
    {
        return std::unexpected(TransferCredentialError::Unavailable);
    }
}

[[nodiscard]] QVariantMap profileVariantMap(const ztermy::ssh::SshProfile &profile,
                                            const std::vector<ztermy::ssh::SshProfile> &profiles,
                                            const std::vector<ztermy::security::CredentialKey> *availableKeys)
{
    const bool credentialStored = profileHasStoredCredential(profile, availableKeys);
    const bool proxyCredentialStored = profileHasStoredProxyCredential(profile, availableKeys);
    QVariantList jumpProfileIds;
    QVariantList jumpProfiles;
    bool jumpProfilesReady = true;
    bool connectionCredentialStored = credentialStored || proxyCredentialStored;
    jumpProfileIds.reserve(static_cast<qsizetype>(profile.jumpProfileIds.size()));
    jumpProfiles.reserve(static_cast<qsizetype>(profile.jumpProfileIds.size()));
    for (const std::string &jumpId : profile.jumpProfileIds)
    {
        jumpProfileIds.append(utf8QString(jumpId));
        const auto jump = std::ranges::find(profiles, jumpId, &ztermy::ssh::SshProfile::id);
        if (jump == profiles.end())
        {
            jumpProfilesReady = false;
            continue;
        }
        const bool jumpCredentialStored = profileHasStoredCredential(*jump, availableKeys);
        const bool jumpProxyCredentialStored = profileHasStoredProxyCredential(*jump, availableKeys);
        const bool ready = (!profileRequiresCredential(*jump) || jumpCredentialStored)
                           && (!proxyRequiresCredential(*jump) || jumpProxyCredentialStored);
        jumpProfilesReady = jumpProfilesReady && ready;
        connectionCredentialStored = connectionCredentialStored || jumpCredentialStored || jumpProxyCredentialStored;
        jumpProfiles.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(jump->id)},
            {QStringLiteral("name"), utf8QString(jump->name)},
            {QStringLiteral("endpoint"),
             QStringLiteral("%1@%2:%3").arg(utf8QString(jump->username), utf8QString(jump->host)).arg(jump->port)},
            {QStringLiteral("ready"), ready},
        });
    }
    QVariantMap result{
        {QStringLiteral("id"), utf8QString(profile.id)},
        {QStringLiteral("name"), utf8QString(profile.name)},
        {QStringLiteral("group"), utf8QString(profile.group)},
        {QStringLiteral("host"), utf8QString(profile.host)},
        {QStringLiteral("port"), profile.port},
        {QStringLiteral("username"), utf8QString(profile.username)},
        {QStringLiteral("authentication"), authenticationToken(profile.authentication)},
        {QStringLiteral("privateKeyPath"), utf8QString(profile.privateKeyPath)},
        {QStringLiteral("privateKeyPassphraseRequired"), profile.privateKeyPassphraseRequired},
        {QStringLiteral("credentialStored"), credentialStored},
        {QStringLiteral("sessionOptions"), sessionOptionsVariantMap(profile.sessionOptions)},
        {QStringLiteral("proxy"), proxyOptionsVariantMap(profile.proxy, proxyCredentialStored)},
        {QStringLiteral("jumpProfileIds"), jumpProfileIds},
        {QStringLiteral("jumpProfiles"), jumpProfiles},
        {QStringLiteral("jumpProfilesReady"), jumpProfilesReady},
        {QStringLiteral("connectionCredentialStored"), connectionCredentialStored},
    };
    if (profile.lastConnectedUtcMs)
    {
        result.insert(QStringLiteral("lastConnectedUtcMs"), *profile.lastConnectedUtcMs);
    }
    return result;
}

[[nodiscard]] ztermy::security::CredentialStorage
defaultCredentialStorage(const ztermy::config::StorageMode mode) noexcept
{
    return mode == ztermy::config::StorageMode::installed ? ztermy::security::CredentialStorage::System
                                                          : ztermy::security::CredentialStorage::Portable;
}

[[nodiscard]] std::optional<ztermy::security::CredentialStorage> parseCredentialStorage(const QString &value)
{
    if (value == QStringLiteral("system"))
    {
        return ztermy::security::CredentialStorage::System;
    }
    if (value == QStringLiteral("portable"))
    {
        return ztermy::security::CredentialStorage::Portable;
    }
    if (value == QStringLiteral("session"))
    {
        return ztermy::security::CredentialStorage::Session;
    }
    return std::nullopt;
}

[[nodiscard]] ztermy::config::CredentialStoragePreference
credentialPreferenceForStorage(const ztermy::security::CredentialStorage storage) noexcept
{
    switch (storage)
    {
        case ztermy::security::CredentialStorage::System:
            return ztermy::config::CredentialStoragePreference::system;
        case ztermy::security::CredentialStorage::Portable:
            return ztermy::config::CredentialStoragePreference::portable;
        case ztermy::security::CredentialStorage::Session:
            return ztermy::config::CredentialStoragePreference::session;
    }
    return ztermy::config::CredentialStoragePreference::automatic;
}

[[nodiscard]] QString credentialVaultErrorMessage(const ztermy::security::CredentialVaultError error)
{
    using enum ztermy::security::CredentialVaultError;
    switch (error)
    {
        case Locked:
            return QCoreApplication::translate("AppController", "Unlock the portable credential vault first.");
        case Unavailable:
            return QCoreApplication::translate("AppController",
                                               "The selected credential store is unavailable in this Windows session.");
        case AccessDenied:
            return QCoreApplication::translate("AppController",
                                               "Windows denied access to the selected credential store.");
        case AuthenticationFailed:
            return QCoreApplication::translate("AppController",
                                               "The vault password is incorrect, or the vault was modified.");
        case WeakMasterPassword:
            return QCoreApplication::translate("AppController", "Use a vault password with at least 8 UTF-8 bytes.");
        case AlreadyInitialized:
            return QCoreApplication::translate("AppController",
                                               "The portable credential vault is already initialized.");
        case CorruptData:
            return QCoreApplication::translate("AppController", "The portable credential vault is damaged or invalid.");
        case UnsupportedVersion:
            return QCoreApplication::translate("AppController",
                                               "This credential vault was created by an unsupported ztermy version.");
        case EmptySecret:
            return QCoreApplication::translate("AppController", "Enter a password or key passphrase before saving it.");
        case SecretTooLarge:
            return QCoreApplication::translate("AppController", "The credential is larger than the supported limit.");
        case NotFound:
            return QCoreApplication::translate("AppController", "No saved credential was found for this host.");
        case InvalidKey:
            return QCoreApplication::translate("AppController", "The credential reference is invalid.");
        case IoError:
            return QCoreApplication::translate("AppController", "The credential store could not be read or written.");
        case CryptoError:
            return QCoreApplication::translate("AppController",
                                               "Windows could not complete the credential encryption operation.");
        case MigrationFailed:
            return QCoreApplication::translate("AppController",
                                               "Credential migration was rolled back because verification failed.");
    }
    return QCoreApplication::translate("AppController", "The credential operation failed.");
}

void logCredentialRollbackResult(std::expected<void, ztermy::security::CredentialVaultError> result,
                                 const char *operation)
{
    if (!result)
    {
        qCWarning(appControllerLog) << "Credential rollback failed during" << operation
                                    << "error=" << static_cast<int>(result.error());
    }
}

class CredentialMutation final
{
public:
    explicit CredentialMutation(ztermy::security::CredentialVault &vault) noexcept : m_vault(vault) {}

    ~CredentialMutation()
    {
        if (m_active)
        {
            rollback();
        }
    }

    CredentialMutation(const CredentialMutation &) = delete;
    CredentialMutation &operator=(const CredentialMutation &) = delete;

    [[nodiscard]] std::expected<void, ztermy::security::CredentialVaultError>
    apply(const ztermy::security::CredentialKey &desiredKey,
          const std::optional<ztermy::security::CredentialKey> &previousKey, const bool shouldStore,
          const bool shouldRemovePrevious, ztermy::security::SensitiveByteArray secret)
    {
        if (shouldStore)
        {
            auto previous = m_vault.read(desiredKey);
            if (previous)
            {
                m_previousDesiredSecret = std::move(*previous);
            }
            else if (previous.error() != ztermy::security::CredentialVaultError::NotFound)
            {
                return std::unexpected(previous.error());
            }
            auto stored = m_vault.store(desiredKey, std::move(secret));
            if (!stored)
            {
                return stored;
            }
            m_desiredKey = desiredKey;
        }

        if (shouldRemovePrevious && previousKey && (!m_desiredKey || *previousKey != *m_desiredKey))
        {
            auto previous = m_vault.read(*previousKey);
            if (previous)
            {
                m_removedPreviousSecret = std::move(*previous);
            }
            else if (previous.error() != ztermy::security::CredentialVaultError::NotFound)
            {
                rollback();
                return std::unexpected(previous.error());
            }
            auto removed = m_vault.remove(*previousKey);
            if (!removed && removed.error() != ztermy::security::CredentialVaultError::NotFound)
            {
                rollback();
                return removed;
            }
            if (m_removedPreviousSecret)
            {
                m_removedKey = *previousKey;
            }
        }
        return {};
    }

    void commit() noexcept
    {
        m_active = false;
        m_previousDesiredSecret.reset();
        m_removedPreviousSecret.reset();
    }

private:
    void rollback() noexcept
    {
        m_active = false;
        try
        {
            if (m_removedKey && m_removedPreviousSecret)
            {
                const std::string_view bytes = m_removedPreviousSecret->view();
                logCredentialRollbackResult(
                    m_vault.store(*m_removedKey, ztermy::security::SensitiveByteArray(
                                                     QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                    "previous credential restore");
            }
            if (m_desiredKey)
            {
                if (m_previousDesiredSecret)
                {
                    const std::string_view bytes = m_previousDesiredSecret->view();
                    logCredentialRollbackResult(
                        m_vault.store(*m_desiredKey, ztermy::security::SensitiveByteArray(QByteArray(
                                                         bytes.data(), static_cast<qsizetype>(bytes.size())))),
                        "profile-save restore");
                }
                else
                {
                    logCredentialRollbackResult(m_vault.remove(*m_desiredKey), "profile-save removal");
                }
            }
        }
        catch (...)
        {
            qCWarning(appControllerLog) << "Credential rollback raised an unexpected exception";
        }
    }

    ztermy::security::CredentialVault &m_vault;
    std::optional<ztermy::security::CredentialKey> m_desiredKey;
    std::optional<ztermy::security::CredentialKey> m_removedKey;
    std::optional<ztermy::security::SensitiveByteArray> m_previousDesiredSecret;
    std::optional<ztermy::security::SensitiveByteArray> m_removedPreviousSecret;
    bool m_active = true;
};

[[nodiscard]] ztermy::security::CredentialKind credentialKind(const ztermy::ssh::SshProfile &profile) noexcept
{
    return profile.authentication == ztermy::ssh::SshAuthenticationMethod::Password
               ? ztermy::security::CredentialKind::Password
               : ztermy::security::CredentialKind::PrivateKeyPassphrase;
}

[[nodiscard]] QString transferStatusToken(const ztermy::sftp::TransferStatus status)
{
    using ztermy::sftp::TransferStatus;
    switch (status)
    {
        case TransferStatus::Queued:
            return QStringLiteral("queued");
        case TransferStatus::Running:
            return QStringLiteral("running");
        case TransferStatus::Pausing:
            return QStringLiteral("pausing");
        case TransferStatus::Paused:
            return QStringLiteral("paused");
        case TransferStatus::Cancelling:
            return QStringLiteral("cancelling");
        case TransferStatus::NeedsAttention:
            return QStringLiteral("needs-attention");
        case TransferStatus::Completed:
            return QStringLiteral("completed");
        case TransferStatus::Failed:
            return QStringLiteral("failed");
        case TransferStatus::Cancelled:
            return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString transferBatchStatusToken(const ztermy::sftp::TransferBatchStatus status)
{
    using ztermy::sftp::TransferBatchStatus;
    switch (status)
    {
        case TransferBatchStatus::Discovering:
            return QStringLiteral("discovering");
        case TransferBatchStatus::Ready:
            return QStringLiteral("ready");
        case TransferBatchStatus::Running:
            return QStringLiteral("running");
        case TransferBatchStatus::Paused:
            return QStringLiteral("paused");
        case TransferBatchStatus::NeedsAttention:
            return QStringLiteral("needs-attention");
        case TransferBatchStatus::Completed:
            return QStringLiteral("completed");
        case TransferBatchStatus::Failed:
            return QStringLiteral("failed");
        case TransferBatchStatus::Cancelled:
            return QStringLiteral("cancelled");
        case TransferBatchStatus::Interrupted:
            return QStringLiteral("interrupted");
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString sessionLogStateToken(const ztermy::logging::SessionLogState state)
{
    using ztermy::logging::SessionLogState;
    switch (state)
    {
        case SessionLogState::Idle:
            return QStringLiteral("idle");
        case SessionLogState::Starting:
            return QStringLiteral("starting");
        case SessionLogState::Active:
            return QStringLiteral("active");
        case SessionLogState::Failed:
            return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

[[nodiscard]] QString transferErrorMessage(const std::string &errorCode)
{
    const QString code = utf8QString(errorCode);
    if (code == QStringLiteral("interrupted"))
    {
        return QCoreApplication::translate("AppController",
                                           "Interrupted by the previous shutdown. Retry to start the transfer again.");
    }
    if (code == QStringLiteral("credential-locked"))
    {
        return QCoreApplication::translate("AppController", "Unlock the credential vault, then retry the transfer.");
    }
    if (code == QStringLiteral("credential-unavailable") || code == QStringLiteral("authentication-unavailable"))
    {
        return QCoreApplication::translate("AppController", "Review the saved authentication method, then retry.");
    }
    if (code == QStringLiteral("authentication-rejected"))
    {
        return QCoreApplication::translate("AppController",
                                           "Authentication was rejected. Update the credential, then retry.");
    }
    if (code == QStringLiteral("remote-timeout"))
    {
        return QCoreApplication::translate("AppController",
                                           "The remote operation timed out. Check connectivity, then retry.");
    }
    if (code == QStringLiteral("remote-connection-lost"))
    {
        return QCoreApplication::translate("AppController", "The remote connection was lost. Reconnect, then retry.");
    }
    if (code == QStringLiteral("local-io") || code == QStringLiteral("commit-failed"))
    {
        return QCoreApplication::translate(
            "AppController", "The local file could not be written. Check the path, permissions, and free space.");
    }
    if (code == QStringLiteral("remote-io"))
    {
        return QCoreApplication::translate(
            "AppController", "The remote file operation failed. Check the path and permissions, then retry.");
    }
    if (code == QStringLiteral("file-conflict"))
    {
        return QCoreApplication::translate("AppController", "Choose whether to replace, rename, skip, or cancel.");
    }
    if (code == QStringLiteral("incompatible-conflict") || code == QStringLiteral("invalid-conflict-rename"))
    {
        return QCoreApplication::translate("AppController", "Choose a compatible destination name and try again.");
    }
    if (code == QStringLiteral("invalid-task"))
    {
        return QCoreApplication::translate("AppController", "The source or destination is no longer valid.");
    }
    if (code == QStringLiteral("credential-cancelled") || code == QStringLiteral("cancelled"))
    {
        return QCoreApplication::translate("AppController", "Authentication was cancelled.");
    }
    if (code == QStringLiteral("worker-initialization-failed"))
    {
        return QCoreApplication::translate("AppController", "The transfer worker could not start. Retry the transfer.");
    }
    return code.isEmpty() ? QString{}
                          : QCoreApplication::translate(
                                "AppController", "The transfer failed. Review the paths and connection, then retry.");
}

[[nodiscard]] QVariantMap transferTaskValue(const ztermy::sftp::TransferTask &task)
{
    return {
        {QStringLiteral("id"), utf8QString(task.id)},
        {QStringLiteral("endpointId"), utf8QString(task.endpointId)},
        {QStringLiteral("displayName"), utf8QString(task.displayName)},
        {QStringLiteral("sourcePath"), utf8QString(task.sourcePath)},
        {QStringLiteral("destinationPath"), utf8QString(task.destinationPath)},
        {QStringLiteral("filenameEncoding"), utf8QString(task.filenameEncoding)},
        {QStringLiteral("direction"), task.direction == ztermy::sftp::TransferDirection::Download
                                          ? QStringLiteral("download")
                                          : QStringLiteral("upload")},
        {QStringLiteral("status"), transferStatusToken(task.status)},
        {QStringLiteral("totalBytes"), QVariant::fromValue<qulonglong>(task.totalBytes)},
        {QStringLiteral("transferredBytes"), QVariant::fromValue<qulonglong>(task.transferredBytes)},
        {QStringLiteral("bytesPerSecond"), QVariant::fromValue<qulonglong>(task.bytesPerSecond)},
        {QStringLiteral("errorCode"), utf8QString(task.errorCode)},
        {QStringLiteral("errorMessage"), transferErrorMessage(task.errorCode)},
        {QStringLiteral("retryable"), task.retryable},
    };
}

[[nodiscard]] QVariantMap transferBatchValue(const ztermy::sftp::TransferBatch &batch)
{
    const ztermy::sftp::TransferBatchSummary summary = ztermy::sftp::summarizeTransferBatch(batch);
    QStringList roots;
    roots.reserve(static_cast<qsizetype>(batch.sourceRoots.size()));
    for (const std::string &root : batch.sourceRoots)
    {
        roots.push_back(utf8QString(root));
    }
    return {
        {QStringLiteral("id"), utf8QString(batch.id)},
        {QStringLiteral("endpointId"), utf8QString(batch.endpointId)},
        {QStringLiteral("displayName"), utf8QString(batch.displayName)},
        {QStringLiteral("sourceRoots"), roots},
        {QStringLiteral("destinationRoot"), utf8QString(batch.destinationRoot)},
        {QStringLiteral("direction"), batch.direction == ztermy::sftp::TransferBatchDirection::Download
                                          ? QStringLiteral("download")
                                          : QStringLiteral("upload")},
        {QStringLiteral("status"), transferBatchStatusToken(batch.status)},
        {QStringLiteral("entryCount"), QVariant::fromValue<qulonglong>(summary.entryCount)},
        {QStringLiteral("directoryCount"), QVariant::fromValue<qulonglong>(summary.directoryCount)},
        {QStringLiteral("fileCount"), QVariant::fromValue<qulonglong>(summary.regularFileCount)},
        {QStringLiteral("completedCount"), QVariant::fromValue<qulonglong>(summary.completedCount)},
        {QStringLiteral("skippedCount"), QVariant::fromValue<qulonglong>(summary.skippedCount)},
        {QStringLiteral("failedCount"), QVariant::fromValue<qulonglong>(summary.failedCount)},
        {QStringLiteral("totalBytes"), QVariant::fromValue<qulonglong>(summary.totalBytes)},
        {QStringLiteral("transferredBytes"), QVariant::fromValue<qulonglong>(summary.transferredBytes)},
        {QStringLiteral("bytesPerSecond"), QVariant::fromValue<qulonglong>(batch.bytesPerSecond)},
        {QStringLiteral("errorCode"), utf8QString(batch.discoveryErrorCode)},
        {QStringLiteral("errorMessage"), transferErrorMessage(batch.discoveryErrorCode)},
    };
}

[[nodiscard]] QString quickConnectError(const ztermy::ssh::SshTargetError error)
{
    using enum ztermy::ssh::SshTargetError;
    switch (error)
    {
        case MissingUsername:
            return QCoreApplication::translate("AppController", "Enter a username before @.");
        case MissingHost:
            return QCoreApplication::translate("AppController", "Enter a host after @.");
        case InvalidPort:
            return QCoreApplication::translate("AppController", "Port must be a number from 1 to 65535.");
        case BracketsRequired:
            return QCoreApplication::translate("AppController",
                                               "Wrap an IPv6 host in brackets, for example user@[::1]:22.");
        case InvalidFormat:
            return QCoreApplication::translate("AppController", "Use user@host or user@host:port.");
    }
    return QCoreApplication::translate("AppController", "Use user@host or user@host:port.");
}

[[nodiscard]] QString localizedSshFailure(const std::optional<ztermy::ssh::SshFailureKind> failure)
{
    using enum ztermy::ssh::SshFailureKind;
    if (!failure)
    {
        return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
    }
    switch (*failure)
    {
        case NameResolutionFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH host name resolution failed");
        case ConnectionRefused:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection was refused");
        case TimedOut:
            return QCoreApplication::translate("SshTerminalSession", "SSH operation timed out");
        case TransportError:
            return QCoreApplication::translate("SshTerminalSession", "SSH transport failed");
        case HostKeyChanged:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key changed");
        case HostKeyInvalid:
            return QCoreApplication::translate("SshTerminalSession", "SSH host key could not be verified");
        case AuthenticationRejected:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication was rejected");
        case AuthenticationUnavailable:
            return QCoreApplication::translate("SshTerminalSession", "SSH authentication method is unavailable");
        case ChannelOpenFailed:
            return QCoreApplication::translate("SshTerminalSession", "SSH terminal channel could not be opened");
        case RemoteClosed:
            return QCoreApplication::translate("SshTerminalSession", "SSH remote host closed the connection");
        case Cancelled:
            return QCoreApplication::translate("SshTerminalSession", "SSH connection cancelled");
        case ProtocolError:
            return QCoreApplication::translate("SshTerminalSession", "SSH protocol error");
    }
    return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
}

[[nodiscard]] QString localizedSshStatus(const ztermy::ssh::SshConnectionPhase phase,
                                         const std::optional<ztermy::ssh::SshFailureKind> failure)
{
    using enum ztermy::ssh::SshConnectionPhase;
    switch (phase)
    {
        case Resolving:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Resolving SSH host");
        case Connecting:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Connecting to SSH host");
        case Handshaking:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Negotiating SSH connection");
        case VerifyingHostKey:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Verifying SSH host key");
        case AwaitingHostKeyConfirmation:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH host key confirmation required");
        case Authenticating:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Authenticating SSH session");
        case OpeningChannel:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "Opening SSH terminal");
        case Connected:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH terminal connected");
        case Failed:
            return localizedSshFailure(failure);
        case Closing:
        case Disconnected:
            return QCoreApplication::translate("ztermy::ssh::SshTerminalSession", "SSH terminal disconnected");
    }
    return QCoreApplication::translate("SshTerminalSession", "SSH connection failed");
}

} // namespace

namespace ztermy
{

AppController::AppController(QObject *parent)
    : AppController(applicationDataFile(QStringLiteral("profiles.json")),
                    applicationDataFile(QStringLiteral("known_hosts.json")),
                    applicationDataFile(QStringLiteral("settings.json")), parent)
{
}

AppController::AppController(const QString &profileStorePath, QObject *parent)
    : AppController(profileStorePath, siblingKnownHostsFile(profileStorePath), siblingSettingsFile(profileStorePath),
                    parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QObject *parent)
    : AppController(std::move(profileStorePath), std::move(knownHostsPath), QString{}, parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QObject *parent)
    : AppController(
          std::move(profileStorePath), std::move(knownHostsPath), std::move(settingsPath),
          [] {
              return std::make_unique<terminal::LocalTerminalSession>();
          },
          parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : AppController(std::move(profileStorePath), std::move(knownHostsPath), QString{}, std::move(localSessionFactory),
                    parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : QObject(parent),
      m_localSessionFactory(std::move(localSessionFactory)),
      m_profileStore(std::move(profileStorePath)),
      m_portForwardingStore(siblingPortForwardingFile(m_profileStore.filePath())),
      m_settingsStore(settingsPath.isEmpty() ? siblingSettingsFile(m_profileStore.filePath())
                                             : std::move(settingsPath)),
      m_scriptStore(siblingScriptsFile(m_settingsStore.filePath())),
      m_legacyQuickCommandPath(siblingQuickCommandsFile(m_settingsStore.filePath())),
      m_workspaceStateStore(siblingWorkspaceStateFile(m_settingsStore.filePath())),
      m_credentialVaults(std::make_unique<security::CredentialVaultCoordinator>(
          siblingCredentialsFile(m_profileStore.filePath()), security::CredentialStorage::Session)),
      m_knownHostsPath(std::move(knownHostsPath))
{
    if (!m_localSessionFactory)
    {
        m_localSessionFactory = [] {
            return std::make_unique<terminal::LocalTerminalSession>();
        };
    }
    Q_ASSERT(m_localSessionFactory);
    qRegisterMetaType<ShellHistoryEntries>();
    QObject::connect(this, &AppController::terminalTabsChanged, this, &AppController::terminalWorkspaceChanged);
    QObject::connect(this, &AppController::terminalHistoryTaskCompleted, this,
                     &AppController::applyTerminalHistoryTaskResult, Qt::QueuedConnection);
    initializePortForwardingSignalBridges();
    loadHostProfiles();
    loadPortForwardingRules();
    loadApplicationSettings();
    initializeActionRegistry();
    initializeTransferManager();
    initializeScriptExecutionTimer();
    loadQuickCommands();
    loadWorkspaceState();
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             QString credentialsPath, const config::StorageMode storageMode, QObject *parent)
    : AppController(
          std::move(profileStorePath), std::move(knownHostsPath), std::move(settingsPath), std::move(credentialsPath),
          storageMode,
          [] {
              return std::make_unique<terminal::LocalTerminalSession>();
          },
          parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                             QString credentialsPath, const config::StorageMode storageMode,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : QObject(parent),
      m_localSessionFactory(std::move(localSessionFactory)),
      m_profileStore(std::move(profileStorePath)),
      m_portForwardingStore(siblingPortForwardingFile(m_profileStore.filePath())),
      m_settingsStore(settingsPath.isEmpty() ? siblingSettingsFile(m_profileStore.filePath())
                                             : std::move(settingsPath)),
      m_scriptStore(siblingScriptsFile(m_settingsStore.filePath())),
      m_legacyQuickCommandPath(siblingQuickCommandsFile(m_settingsStore.filePath())),
      m_workspaceStateStore(siblingWorkspaceStateFile(m_settingsStore.filePath())),
      m_credentialVaults(std::make_unique<security::CredentialVaultCoordinator>(
          credentialsPath.isEmpty() ? siblingCredentialsFile(m_profileStore.filePath()) : std::move(credentialsPath),
          defaultCredentialStorage(storageMode))),
      m_defaultCredentialStorage(defaultCredentialStorage(storageMode)),
      m_knownHostsPath(std::move(knownHostsPath))
{
    if (!m_localSessionFactory)
    {
        m_localSessionFactory = [] {
            return std::make_unique<terminal::LocalTerminalSession>();
        };
    }
    Q_ASSERT(m_localSessionFactory);
    Q_ASSERT(m_credentialVaults);
    qRegisterMetaType<ShellHistoryEntries>();
    QObject::connect(this, &AppController::terminalTabsChanged, this, &AppController::terminalWorkspaceChanged);
    QObject::connect(this, &AppController::terminalHistoryTaskCompleted, this,
                     &AppController::applyTerminalHistoryTaskResult, Qt::QueuedConnection);
    initializePortForwardingSignalBridges();
    loadHostProfiles();
    loadPortForwardingRules();
    loadApplicationSettings();
    initializeActionRegistry();
    initializeTransferManager();
    initializeScriptExecutionTimer();
    loadQuickCommands();
    loadWorkspaceState();
}

AppController::~AppController()
{
    shutdown();
}

void AppController::initializePortForwardingSignalBridges()
{
    QObject::connect(
        this, &AppController::portForwardingSnapshotReady, this,
        [this](const QString &ruleId, const int state, const int failure, const qulonglong activeClients,
               const qulonglong bytesFromClients, const qulonglong bytesToClients, const qulonglong rejectedClients) {
            applyPortForwardingSnapshot(ruleId.toUtf8().toStdString(),
                                        forwarding::PortForwardingJobSnapshot{
                                            .state = static_cast<forwarding::PortForwardingJobState>(state),
                                            .failure = static_cast<forwarding::PortForwardingJobFailure>(failure),
                                            .activeClients = static_cast<std::size_t>(activeClients),
                                            .bytesFromClients = static_cast<std::uint64_t>(bytesFromClients),
                                            .bytesToClients = static_cast<std::uint64_t>(bytesToClients),
                                            .rejectedClients = static_cast<std::uint64_t>(rejectedClients),
                                        });
        },
        Qt::QueuedConnection);
    QObject::connect(
        this, &AppController::portForwardingHostKeyPromptRequested, this,
        [this](const QString &ruleId, const QString &endpoint, const QString &algorithm, const QString &fingerprint,
               const bool changed) {
            PortForwardingRuntime *runtime = findPortForwardingRuntime(ruleId.toUtf8().toStdString());
            if (runtime == nullptr)
            {
                return;
            }
            if (m_shutdownStarted || m_hostKeyPromptVisible)
            {
                if (!changed)
                {
                    resolvePortForwardingHostKey(*runtime, ssh::UnknownHostKeyDecision::Reject);
                }
                return;
            }
            m_hostKeyForwardingRuleId = ruleId;
            setHostKeyPrompt(endpoint, algorithm, fingerprint, changed);
        },
        Qt::QueuedConnection);
}

void AppController::attachTerminal(ui::TerminalItem *terminal)
{
    m_terminal = terminal;
    const TerminalTab *tab = activeTab();
    if (terminal == nullptr || tab == nullptr)
    {
        return;
    }
    attachTerminalViewport(tab->paneId, terminal);
}

void AppController::attachTerminalViewport(const QString &paneId, QObject *viewport)
{
    auto *terminal = qobject_cast<ui::TerminalItem *>(viewport);
    const TerminalTab *tab = findTabForPane(paneId);
    if (terminal == nullptr || tab == nullptr)
    {
        return;
    }
    if (const auto existing = m_terminalViewports.value(paneId); existing == terminal)
    {
        showTabInViewport(*tab);
        return;
    }
    QObject::disconnect(terminal, nullptr, this, nullptr);
    m_terminalViewports.insert(paneId, terminal);
    if (tab->id == m_focusedTabId)
    {
        m_terminal = terminal;
    }
    QObject::connect(terminal, &QObject::destroyed, this, [this, paneId, terminal] {
        if (m_terminalViewports.value(paneId) == terminal)
        {
            m_terminalViewports.remove(paneId);
        }
        if (m_terminal == terminal)
        {
            m_terminal = nullptr;
        }
    });
    connectTerminalSignals(*terminal, paneId);
    showTabInViewport(*tab);
}

void AppController::detachTerminalViewport(const QString &paneId, QObject *viewport)
{
    auto *terminal = qobject_cast<ui::TerminalItem *>(viewport);
    if (terminal == nullptr || m_terminalViewports.value(paneId) != terminal)
    {
        return;
    }
    QObject::disconnect(terminal, nullptr, this, nullptr);
    m_terminalViewports.remove(paneId);
    if (m_terminal == terminal)
    {
        m_terminal = nullptr;
    }
}

void AppController::shutdown() noexcept
{
    if (m_shutdownStarted)
    {
        return;
    }
    m_shutdownStarted = true;
    m_scriptExecutionTimer.stop();
    stopAllPortForwardingRules();
    clearHostKeyPrompt();

    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->requestStop();
    }
    if (m_transferManager)
    {
        m_transferManager->requestStop();
    }
    for (const auto &tab : m_tabs)
    {
        if (tab->sftpSession)
        {
            tab->sftpSession->requestStop();
        }
    }
    for (const auto &session : m_stoppingSftpSessions)
    {
        session->requestStop();
    }

    for (const auto &tab : m_tabs)
    {
        if (tab->local)
        {
            tab->local->stop();
        }
        if (tab->ssh)
        {
            tab->ssh->stop();
        }
        if (tab->sessionLog)
        {
            tab->sessionLog->stop();
        }
    }
    m_transferBatchCoordinator.reset();
    if (m_transferManager)
    {
        m_transferManager->shutdown();
        m_transferManager.reset();
    }
    m_tabs.clear();
    m_stoppingSftpSessions.clear();
    m_transferTasks.clear();
    m_transferBatches.clear();
    m_activeTabId.clear();
    m_focusedTabId.clear();
    for (ui::TerminalItem *terminal : std::as_const(m_terminalViewports))
    {
        if (terminal != nullptr)
        {
            QObject::disconnect(terminal, nullptr, this, nullptr);
            terminal->setSnapshot({});
        }
    }
    m_terminalViewports.clear();
    m_terminal = nullptr;
}

bool AppController::sshActive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->kind == TerminalTabKind::Ssh;
}

bool AppController::hostKeyPromptVisible() const noexcept
{
    return m_hostKeyPromptVisible;
}

QString AppController::hostKeyEndpoint() const
{
    return m_hostKeyEndpoint;
}

QString AppController::hostKeyAlgorithm() const
{
    return m_hostKeyAlgorithm;
}

QString AppController::hostKeyFingerprint() const
{
    return m_hostKeyFingerprint;
}

bool AppController::hostKeyChangedWarning() const noexcept
{
    return m_hostKeyChangedWarning;
}

QString AppController::defaultPrivateKeyPath() const
{
    return QDir::toNativeSeparators(QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                                        .filePath(QStringLiteral(".ssh/id_ed25519")));
}

QVariantList AppController::hostProfiles() const
{
    const auto keys = m_credentialVaults->active().listKeys();
    const std::vector<security::CredentialKey> *availableKeys = keys ? &*keys : nullptr;
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_profiles.size()));
    for (const ssh::SshProfile &profile : m_profiles)
    {
        result.append(profileVariantMap(profile, m_profiles, availableKeys));
    }
    return result;
}

QVariantList AppController::recentHostProfiles() const
{
    const auto keys = m_credentialVaults->active().listKeys();
    const std::vector<security::CredentialKey> *availableKeys = keys ? &*keys : nullptr;
    std::vector<const ssh::SshProfile *> recent;
    recent.reserve(m_profiles.size());
    for (const ssh::SshProfile &profile : m_profiles)
    {
        if (profile.lastConnectedUtcMs)
        {
            recent.push_back(&profile);
        }
    }
    std::ranges::sort(recent, std::greater{}, [](const ssh::SshProfile *profile) {
        return *profile->lastConnectedUtcMs;
    });
    if (recent.size() > maximumRecentHostProfiles)
    {
        recent.resize(maximumRecentHostProfiles);
    }

    QVariantList result;
    result.reserve(static_cast<qsizetype>(recent.size()));
    for (const ssh::SshProfile *profile : recent)
    {
        result.append(profileVariantMap(*profile, m_profiles, availableKeys));
    }
    return result;
}

QStringList AppController::hostProfileGroups() const
{
    QStringList groups;
    for (const ssh::SshProfile &profile : m_profiles)
    {
        const QString group = utf8QString(profile.group).trimmed();
        if (!group.isEmpty() && !groups.contains(group, Qt::CaseInsensitive))
        {
            groups.push_back(group);
        }
    }
    std::ranges::sort(groups, [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    return groups;
}

QStringList AppController::collapsedHostSections() const
{
    QStringList sections;
    sections.reserve(static_cast<qsizetype>(m_workspaceState.collapsedHostSections.size()));
    for (const std::string &section : m_workspaceState.collapsedHostSections)
    {
        sections.push_back(utf8QString(section));
    }
    return sections;
}

QVariantList AppController::terminalTabs() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_workspaceState.terminalWorkspaces.size()));
    for (const workbench::TerminalWorkspaceLayout &workspace : m_workspaceState.terminalWorkspaces)
    {
        const QString representativeId =
            workspace.id == utf8String(m_activeTabId) ? m_focusedTabId : firstTabIdForWorkspace(workspace);
        const TerminalTab *tab = findTab(representativeId);
        if (tab != nullptr)
        {
            QVariantMap value = terminalTabValue(*tab, utf8QString(workspace.id));
            value.insert(QStringLiteral("title"), utf8QString(workspace.title));
            value.insert(QStringLiteral("paneCount"), static_cast<int>(workspace.restoreIntents.size()));
            result.append(value);
        }
    }
    return result;
}

QVariantMap AppController::terminalTabValue(const TerminalTab &tab, const QString &publicId) const
{
    const workbench::ScriptExecutionSnapshot execution = tab.scriptExecution.snapshot();
    return {
        {QStringLiteral("id"), publicId},
        {QStringLiteral("sessionId"), tab.id},
        {QStringLiteral("paneId"), tab.paneId},
        {QStringLiteral("title"), tab.title},
        {QStringLiteral("kind"), tab.kind == TerminalTabKind::Local ? QStringLiteral("local") : QStringLiteral("ssh")},
        {QStringLiteral("status"), tab.status},
        {QStringLiteral("identity"), tab.identity.isEmpty() ? tab.title : tab.identity},
        {QStringLiteral("address"), tab.address},
        {QStringLiteral("connectedUtcMs"), tab.connectedUtcMs},
        {QStringLiteral("logState"),
         tab.sessionLog ? sessionLogStateToken(tab.sessionLog->state()) : QStringLiteral("idle")},
        {QStringLiteral("logPath"), tab.sessionLog ? tab.sessionLog->path() : QString{}},
        {QStringLiteral("logError"), tab.sessionLog ? tab.sessionLog->errorString() : QString{}},
        {QStringLiteral("logDroppedBytes"),
         QVariant::fromValue<qulonglong>(tab.sessionLog ? tab.sessionLog->droppedBytes() : 0)},
        {QStringLiteral("running"), tab.running},
        {QStringLiteral("reconnecting"), tab.reconnectPending},
        {QStringLiteral("reconnectAttempt"), static_cast<int>(tab.reconnectAttempt)},
        {QStringLiteral("canReconnect"),
         tab.kind == TerminalTabKind::Ssh && !tab.sourceProfileId.isEmpty() && !tab.running},
        {QStringLiteral("connected"),
         tab.kind == TerminalTabKind::Ssh && tab.sshPhase == ssh::SshConnectionPhase::Connected},
        {QStringLiteral("connecting"),
         tab.kind == TerminalTabKind::Ssh && tab.sshPhase != ssh::SshConnectionPhase::Disconnected
             && tab.sshPhase != ssh::SshConnectionPhase::Connected && tab.sshPhase != ssh::SshConnectionPhase::Closing
             && tab.sshPhase != ssh::SshConnectionPhase::Failed},
        {QStringLiteral("failed"), !tab.reconnectPending && tab.kind == TerminalTabKind::Ssh
                                       && tab.sshPhase == ssh::SshConnectionPhase::Failed
                                       && tab.sshFailure != ssh::SshFailureKind::RemoteClosed},
        {QStringLiteral("remoteClosed"), !tab.reconnectPending && tab.kind == TerminalTabKind::Ssh
                                             && tab.sshPhase == ssh::SshConnectionPhase::Failed
                                             && tab.sshFailure == ssh::SshFailureKind::RemoteClosed},
        {QStringLiteral("workbenchOpen"), tab.workbenchOpen},
        {QStringLiteral("workbenchPage"), tab.workbenchPage},
        {QStringLiteral("workbenchSide"), tab.workbenchSide},
        {QStringLiteral("workbenchWidth"), tab.workbenchWidth},
        {QStringLiteral("composerOpen"), tab.composerOpen},
        {QStringLiteral("composerHeight"), tab.composerHeight},
        {QStringLiteral("keywordHighlightEnabled"), tab.keywordHighlightEnabled},
        {QStringLiteral("keywordHighlightRules"), keywordRulesVariant(tab)},
        {QStringLiteral("terminalEncoding"), tab.terminalEncoding},
        {QStringLiteral("sessionFontFamily"), tab.sessionFontFamily},
        {QStringLiteral("sessionFontSize"), tab.sessionFontSize},
        {QStringLiteral("sessionLigatures"), tab.sessionLigatures},
        {QStringLiteral("sessionBackgroundOpacity"), tab.sessionBackgroundOpacity},
        {QStringLiteral("sessionCursor"), tab.sessionCursor},
        {QStringLiteral("sessionForeground"), tab.sessionForeground},
        {QStringLiteral("sessionBackground"), tab.sessionBackground},
        {QStringLiteral("scriptRecordingState"), scriptRecorderStateToken(tab.scriptRecorder.state())},
        {QStringLiteral("scriptRecordingSteps"), recordedScriptStepsVariant(tab)},
        {QStringLiteral("scriptRecordingStartedUtcMs"), tab.recordingStartedUtcMs},
        {QStringLiteral("scriptPlaybackActive"), tab.scriptPlaybackActive},
        {QStringLiteral("scriptExecutionState"), scriptExecutionStateToken(execution.state)},
        {QStringLiteral("scriptExecutionId"), utf8QString(execution.scriptId)},
        {QStringLiteral("scriptExecutionName"), utf8QString(execution.scriptName)},
        {QStringLiteral("scriptExecutionDispatchedSteps"), static_cast<qulonglong>(execution.dispatchedSteps)},
        {QStringLiteral("scriptExecutionTotalSteps"), static_cast<qulonglong>(execution.totalSteps)},
    };
}

QVariantList AppController::quickCommands() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_scripts.size()));
    for (const workbench::ScriptDefinition &script : m_scripts)
    {
        QVariantList variables;
        variables.reserve(static_cast<qsizetype>(script.variables.size()));
        for (const workbench::ScriptVariable &variable : script.variables)
        {
            QStringList choices;
            choices.reserve(static_cast<qsizetype>(variable.choices.size()));
            for (const std::string &choice : variable.choices)
            {
                choices.append(utf8QString(choice));
            }
            variables.append(QVariantMap{{QStringLiteral("name"), utf8QString(variable.name)},
                                         {QStringLiteral("label"), utf8QString(variable.label)},
                                         {QStringLiteral("type"), scriptVariableTypeToken(variable.type)},
                                         {QStringLiteral("defaultValue"), utf8QString(variable.defaultValue)},
                                         {QStringLiteral("choices"), choices},
                                         {QStringLiteral("required"), variable.required}});
        }
        QVariantList steps;
        QStringList commandText;
        steps.reserve(static_cast<qsizetype>(script.steps.size()));
        commandText.reserve(static_cast<qsizetype>(script.steps.size()));
        for (const workbench::ScriptStep &step : script.steps)
        {
            const QString command = utf8QString(step.command);
            commandText.append(command);
            steps.append(QVariantMap{{QStringLiteral("command"), command},
                                     {QStringLiteral("continuation"), scriptContinuationToken(step.continuation)},
                                     {QStringLiteral("outputMarker"), utf8QString(step.outputMarker)},
                                     {QStringLiteral("timeoutMs"), step.timeoutMs}});
        }
        result.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(script.id)},
            {QStringLiteral("name"), utf8QString(script.name)},
            {QStringLiteral("command"), commandText.join(QLatin1Char('\n'))},
            {QStringLiteral("description"), utf8QString(script.description)},
            {QStringLiteral("shell"), quickCommandShellScopeToken(script.shellScope)},
            {QStringLiteral("variables"), variables},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("createdUtcMs"), script.createdUtcMs},
            {QStringLiteral("modifiedUtcMs"), script.modifiedUtcMs},
        });
    }
    return result;
}

QString AppController::quickCommandOperationError() const
{
    return m_quickCommandOperationError;
}

QVariantList AppController::terminalHistory() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return {};
    }

    QVariantList result;
    result.reserve(
        static_cast<qsizetype>(std::min(maximumHistoryEntries, tab->capturedHistory.size() + tab->history.size())));
    QSet<QString> seen;
    const auto appendEntries = [&result, &seen](const std::vector<workbench::ShellHistoryEntry> &entries) {
        for (const workbench::ShellHistoryEntry &entry : entries)
        {
            const QString command = utf8QString(entry.command);
            if (seen.contains(command))
            {
                continue;
            }
            seen.insert(command);
            result.append(terminalHistoryValue(entry));
            if (result.size() >= static_cast<qsizetype>(maximumHistoryEntries))
            {
                break;
            }
        }
    };
    appendEntries(tab->capturedHistory);
    if (result.size() < static_cast<qsizetype>(maximumHistoryEntries))
    {
        appendEntries(tab->history);
    }
    return result;
}

QVariantList AppController::terminalGlobalHistory() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(maximumHistoryEntries));
    QSet<QString> seen;

    const auto appendTab = [&result, &seen](const TerminalTab &tab) {
        const QString sourceId = !tab.sourceProfileId.isEmpty() ? tab.sourceProfileId : tab.id;
        const QString sourceLabel = !tab.title.isEmpty() ? tab.title : tab.identity;
        const auto appendEntries = [&](const std::vector<workbench::ShellHistoryEntry> &entries) {
            for (const workbench::ShellHistoryEntry &entry : entries)
            {
                const QString command = utf8QString(entry.command);
                const QString key = sourceId + QChar{u'\0'} + command;
                if (seen.contains(key))
                {
                    continue;
                }
                seen.insert(key);
                result.append(terminalHistoryValue(entry, sourceLabel, sourceId));
                if (result.size() >= static_cast<qsizetype>(maximumHistoryEntries))
                {
                    break;
                }
            }
        };
        appendEntries(tab.capturedHistory);
        if (result.size() < static_cast<qsizetype>(maximumHistoryEntries))
        {
            appendEntries(tab.history);
        }
    };

    const TerminalTab *active = activeTab();
    if (active != nullptr)
    {
        appendTab(*active);
    }
    for (const std::unique_ptr<TerminalTab> &tab : m_tabs)
    {
        if (result.size() >= static_cast<qsizetype>(maximumHistoryEntries))
        {
            break;
        }
        if (tab && tab.get() != active)
        {
            appendTab(*tab);
        }
    }
    return result;
}

QVariantList AppController::actions() const
{
    return m_actionRegistry.actions(activeTab() != nullptr);
}

QString AppController::terminalHistoryState() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("idle") : tab->historyState;
}

QString AppController::terminalHistoryError() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->historyError;
}

QObject *AppController::activeSftpDirectoryModel() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? nullptr : tab->sftpModel.get();
}

QString AppController::activeSftpPath() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("/") : tab->sftpPath;
}

QString AppController::activeSftpHomePath() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->sftpHomePath;
}

QVariantList AppController::recentSftpPaths() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty())
    {
        return {};
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab->sourceProfileId));
    if (state == nullptr)
    {
        return {};
    }
    QVariantList paths;
    paths.reserve(static_cast<qsizetype>(state->recentRemotePaths.size()));
    for (const std::string &path : state->recentRemotePaths)
    {
        paths.push_back(utf8QString(path));
    }
    return paths;
}

QVariantList AppController::bookmarkedSftpPaths() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty())
    {
        return {};
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab->sourceProfileId));
    if (state == nullptr)
    {
        return {};
    }
    QVariantList paths;
    paths.reserve(static_cast<qsizetype>(state->bookmarkedRemotePaths.size()));
    for (const std::string &path : state->bookmarkedRemotePaths)
    {
        paths.push_back(utf8QString(path));
    }
    return paths;
}

bool AppController::activeSftpPathBookmarked() const
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty())
    {
        return false;
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab->sourceProfileId));
    return state != nullptr && workbench::remotePathBookmarked(*state, utf8String(tab->sftpPath));
}

QString AppController::activeSftpState() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("idle") : tab->sftpState;
}

QString AppController::activeSftpError() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->sftpError;
}

QString AppController::activeSftpViewMode() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("list") : tab->sftpViewMode;
}

QString AppController::activeSftpSortColumn() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("name") : tab->sftpSortColumn;
}

bool AppController::activeSftpSortAscending() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpSortAscending;
}

bool AppController::activeSftpDirectoriesFirst() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpDirectoriesFirst;
}

bool AppController::activeSftpShowModifiedColumn() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpShowModifiedColumn;
}

bool AppController::activeSftpShowSizeColumn() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr || tab->sftpShowSizeColumn;
}

bool AppController::activeSftpShowTypeColumn() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->sftpShowTypeColumn;
}

QString AppController::activeSftpFilenameEncoding() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QStringLiteral("utf-8") : tab->sftpFilenameEncoding;
}

bool AppController::activeSftpFollowTerminalDirectory() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->followTerminalDirectory;
}

QString AppController::activeTerminalWorkingDirectory() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->terminalWorkingDirectory;
}

QVariantList AppController::transferTasks() const
{
    return m_transferTasks;
}

QVariantList AppController::transferBatches() const
{
    return m_transferBatches;
}

int AppController::activeTransferCount() const noexcept
{
    return static_cast<int>(std::ranges::count_if(m_transferTasks, [](const QVariant &value) {
        const QString status = value.toMap().value(QStringLiteral("status")).toString();
        return status == QLatin1StringView("queued") || status == QLatin1StringView("running")
               || status == QLatin1StringView("pausing") || status == QLatin1StringView("paused")
               || status == QLatin1StringView("cancelling") || status == QLatin1StringView("needs-attention");
    }));
}

QString AppController::activeTerminalTabId() const
{
    return m_activeTabId;
}

QVariantMap AppController::activeTerminalWorkspace() const
{
    const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (workspace == nullptr)
    {
        return {};
    }
    return {
        {QStringLiteral("id"), utf8QString(workspace->id)},
        {QStringLiteral("title"), utf8QString(workspace->title)},
        {QStringLiteral("activePaneId"), utf8QString(workspace->activePaneId)},
        {QStringLiteral("paneCount"), static_cast<int>(workspace->restoreIntents.size())},
        {QStringLiteral("root"), terminalLayoutNodeValue(*workspace, workspace->rootNodeId)},
    };
}

QVariantMap AppController::terminalLayoutNodeValue(const workbench::TerminalWorkspaceLayout &workspace,
                                                   const std::string_view nodeId) const
{
    const auto position = std::ranges::find(workspace.nodes, nodeId, &workbench::TerminalLayoutNode::id);
    if (position == workspace.nodes.end())
    {
        return {};
    }
    if (position->kind == workbench::TerminalLayoutNodeKind::Leaf)
    {
        const TerminalTab *tab = findTabForPane(utf8QString(position->id));
        if (tab == nullptr)
        {
            return {};
        }
        return {
            {QStringLiteral("id"), utf8QString(position->id)},
            {QStringLiteral("kind"), QStringLiteral("leaf")},
            {QStringLiteral("active"), position->id == workspace.activePaneId},
            {QStringLiteral("tab"), terminalTabValue(*tab, utf8QString(workspace.id))},
        };
    }
    return {
        {QStringLiteral("id"), utf8QString(position->id)},
        {QStringLiteral("kind"), QStringLiteral("split")},
        {QStringLiteral("orientation"), position->orientation == workbench::TerminalSplitOrientation::Horizontal
                                            ? QStringLiteral("horizontal")
                                            : QStringLiteral("vertical")},
        {QStringLiteral("ratio"), position->ratio},
        {QStringLiteral("first"), terminalLayoutNodeValue(workspace, position->firstChildId)},
        {QStringLiteral("second"), terminalLayoutNodeValue(workspace, position->secondChildId)},
    };
}

QVariantMap AppController::activeRemoteTelemetry() const
{
    const TerminalTab *tab = activeTab();
    QVariantMap result{{QStringLiteral("state"), tab ? tab->telemetryState : QStringLiteral("paused")},
                       {QStringLiteral("available"), tab != nullptr && tab->telemetrySample.has_value()}};
    if (tab == nullptr || !tab->telemetrySample)
    {
        return result;
    }

    const telemetry::Sample &sample = *tab->telemetrySample;
    result.insert(QStringLiteral("os"), QString::fromStdString(sample.osName));
    result.insert(QStringLiteral("cpuPercent"), sample.cpuPercent.value_or(-1.0));
    result.insert(QStringLiteral("cpuCores"), static_cast<int>(sample.cpuCoreCount));
    result.insert(QStringLiteral("memoryUsedKiB"), QVariant::fromValue<qulonglong>(sample.memoryUsedKiB));
    result.insert(QStringLiteral("memoryTotalKiB"), QVariant::fromValue<qulonglong>(sample.memory.totalKiB));
    result.insert(QStringLiteral("memoryAvailableKiB"), QVariant::fromValue<qulonglong>(sample.memory.availableKiB));
    result.insert(QStringLiteral("memoryBuffersKiB"), QVariant::fromValue<qulonglong>(sample.memory.buffersKiB));
    result.insert(QStringLiteral("memoryCachedKiB"),
                  QVariant::fromValue<qulonglong>(sample.memory.cachedKiB + sample.memory.reclaimableKiB));
    result.insert(QStringLiteral("swapUsedKiB"),
                  QVariant::fromValue<qulonglong>(sample.memory.swapTotalKiB - sample.memory.swapFreeKiB));
    result.insert(QStringLiteral("swapTotalKiB"), QVariant::fromValue<qulonglong>(sample.memory.swapTotalKiB));
    result.insert(QStringLiteral("receivedBytesPerSecond"),
                  QVariant::fromValue<qulonglong>(sample.receivedBytesPerSecond));
    result.insert(QStringLiteral("transmittedBytesPerSecond"),
                  QVariant::fromValue<qulonglong>(sample.transmittedBytesPerSecond));
    result.insert(QStringLiteral("latencyMs"), static_cast<int>(sample.sshProbeLatencyMs));

    QVariantList cores;
    cores.reserve(static_cast<qsizetype>(sample.corePercents.size()));
    for (const double value : sample.corePercents)
    {
        cores.append(value);
    }
    result.insert(QStringLiteral("cores"), cores);

    QVariantList disks;
    disks.reserve(static_cast<qsizetype>(sample.disks.size()));
    for (const telemetry::DiskCounters &disk : sample.disks)
    {
        disks.append(QVariantMap{{QStringLiteral("mountPoint"), QString::fromStdString(disk.mountPoint)},
                                 {QStringLiteral("usedKiB"), QVariant::fromValue<qulonglong>(disk.usedKiB)},
                                 {QStringLiteral("totalKiB"), QVariant::fromValue<qulonglong>(disk.totalKiB)},
                                 {QStringLiteral("percent"), disk.usedPercent}});
    }
    result.insert(QStringLiteral("disks"), disks);

    QVariantList interfaces;
    interfaces.reserve(static_cast<qsizetype>(sample.interfaces.size()));
    for (const telemetry::NetworkRate &interface : sample.interfaces)
    {
        interfaces.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(interface.name)},
                                      {QStringLiteral("receivedBytesPerSecond"),
                                       QVariant::fromValue<qulonglong>(interface.receivedBytesPerSecond)},
                                      {QStringLiteral("transmittedBytesPerSecond"),
                                       QVariant::fromValue<qulonglong>(interface.transmittedBytesPerSecond)}});
    }
    result.insert(QStringLiteral("interfaces"), interfaces);

    QVariantList processes;
    processes.reserve(static_cast<qsizetype>(sample.processes.size()));
    for (const telemetry::ProcessMemory &process : sample.processes)
    {
        processes.append(QVariantMap{{QStringLiteral("pid"), process.pid},
                                     {QStringLiteral("memoryPercent"), process.memoryPercent},
                                     {QStringLiteral("command"), QString::fromStdString(process.command)}});
    }
    result.insert(QStringLiteral("processes"), processes);

    QVariantList history;
    history.reserve(static_cast<qsizetype>(tab->telemetryHistory.size()));
    for (const telemetry::Sample &entry : tab->telemetryHistory)
    {
        const auto rootDisk = std::ranges::find(entry.disks, std::string("/"), &telemetry::DiskCounters::mountPoint);
        history.append(QVariantMap{
            {QStringLiteral("cpu"), entry.cpuPercent.value_or(-1.0)},
            {QStringLiteral("memory"), entry.memory.totalKiB > 0 ? 100.0 * static_cast<double>(entry.memoryUsedKiB)
                                                                       / static_cast<double>(entry.memory.totalKiB)
                                                                 : 0.0},
            {QStringLiteral("disk"), rootDisk != entry.disks.end() ? rootDisk->usedPercent : 0.0},
            {QStringLiteral("received"), QVariant::fromValue<qulonglong>(entry.receivedBytesPerSecond)},
            {QStringLiteral("transmitted"), QVariant::fromValue<qulonglong>(entry.transmittedBytesPerSecond)},
            {QStringLiteral("latency"), static_cast<int>(entry.sshProbeLatencyMs)},
        });
    }
    result.insert(QStringLiteral("history"), history);
    return result;
}

QString AppController::terminalSearchQuery() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->searchQuery;
}

int AppController::terminalSearchCurrent() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchCurrent);
}

int AppController::terminalSearchTotal() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchTotal);
}

bool AppController::terminalSearchCaseSensitive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->searchCaseSensitive;
}

QString AppController::themePreference() const
{
    return config::themePreferenceToken(m_settings.theme);
}

qreal AppController::backdropOpacity() const noexcept
{
    return m_settings.backdropOpacity;
}

QString AppController::backdropPreference() const
{
    return config::backdropPreferenceToken(m_settings.backdrop);
}

QString AppController::accentPreference() const
{
    return config::accentPreferenceToken(m_settings.accent);
}

QString AppController::customAccent() const
{
    return m_settings.customAccent;
}

QString AppController::uiFontFamily() const
{
    return m_settings.uiFontFamily;
}

QString AppController::terminalFontFamily() const
{
    return m_settings.terminalFontFamily;
}

int AppController::terminalFontSize() const noexcept
{
    return m_settings.terminalFontSize;
}

bool AppController::showAllTerminalFonts() const noexcept
{
    return m_settings.showAllTerminalFonts;
}

bool AppController::terminalLigatures() const noexcept
{
    return m_settings.terminalLigatures;
}

qreal AppController::terminalBackgroundOpacity() const noexcept
{
    return m_settings.terminalBackgroundOpacity;
}

QString AppController::cursorPreference() const
{
    return config::cursorPreferenceToken(m_settings.cursor);
}

bool AppController::cursorBlink() const noexcept
{
    return m_settings.cursorBlink;
}

bool AppController::copyOnSelect() const noexcept
{
    return m_settings.copyOnSelect;
}

bool AppController::confirmMultilinePaste() const noexcept
{
    return m_settings.confirmMultilinePaste;
}

bool AppController::sftpShowHiddenFiles() const noexcept
{
    return m_settings.sftpShowHiddenFiles;
}

bool AppController::sftpConfirmDelete() const noexcept
{
    return m_settings.sftpConfirmDelete;
}

QString AppController::languagePreference() const
{
    return config::languagePreferenceToken(m_settings.language);
}

void AppController::retranslateUiState()
{
    for (const auto &tab : m_tabs)
    {
        if (tab->kind == TerminalTabKind::Local)
        {
            tab->status = tab->running ? QCoreApplication::translate("ztermy::terminal::LocalTerminalSession",
                                                                     "Local PowerShell connected")
                                       : QCoreApplication::translate("ztermy::terminal::LocalTerminalSession",
                                                                     "Local terminal stopped");
        }
        else if (tab->reconnectPending)
        {
            tab->status = QCoreApplication::translate("AppController", "Waiting to reconnect to the SSH host...");
        }
        else
        {
            tab->status = localizedSshStatus(tab->sshPhase, tab->sshFailure);
        }
    }
    setCredentialOperationError({});
    showActiveTab();
    emit terminalTabsChanged();
    emit actionRegistryChanged();
}

QString AppController::credentialStoragePreference() const
{
    return config::credentialStoragePreferenceToken(m_settings.credentialStorage);
}

QString AppController::effectiveCredentialStorage() const
{
    return QString::fromLatin1(security::credentialStorageToken(m_credentialVaults->storage()));
}

bool AppController::portableVaultInitialized() const noexcept
{
    return m_credentialVaults->portableInitialized();
}

bool AppController::portableVaultLocked() const noexcept
{
    return m_credentialVaults->portableLocked();
}

QString AppController::credentialOperationError() const
{
    return m_credentialOperationError;
}

QVariantList AppController::portForwardingRules() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_portForwardingRules.size()));
    for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
    {
        const auto profile = std::ranges::find(m_profiles, rule.profileId, &ssh::SshProfile::id);
        const PortForwardingRuntime *runtime = findPortForwardingRuntime(rule.id);
        const forwarding::PortForwardingJobSnapshot snapshot =
            runtime != nullptr && runtime->job ? runtime->job->snapshot() : forwarding::PortForwardingJobSnapshot{};
        const bool waitingForPortableVault = rule.autoStart && runtime == nullptr
                                             && snapshot.state == forwarding::PortForwardingJobState::Stopped
                                             && portableVaultLocked();
        result.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(rule.id)},
            {QStringLiteral("label"), utf8QString(rule.label)},
            {QStringLiteral("profileId"), utf8QString(rule.profileId)},
            {QStringLiteral("profileName"), profile == m_profiles.end() ? QString{} : utf8QString(profile->name)},
            {QStringLiteral("type"), forwardingTypeToken(rule.type)},
            {QStringLiteral("bindHost"), utf8QString(rule.bind.host)},
            {QStringLiteral("bindPort"), rule.bind.port},
            {QStringLiteral("destinationHost"), utf8QString(rule.destination.host)},
            {QStringLiteral("destinationPort"), rule.destination.port},
            {QStringLiteral("autoStart"), rule.autoStart},
            {QStringLiteral("state"),
             waitingForPortableVault ? QStringLiteral("waiting") : forwardingStateToken(snapshot.state)},
            {QStringLiteral("failure"), forwardingFailureToken(snapshot.failure)},
            {QStringLiteral("activeClients"), static_cast<qulonglong>(snapshot.activeClients)},
            {QStringLiteral("bytesFromClients"), static_cast<qulonglong>(snapshot.bytesFromClients)},
            {QStringLiteral("bytesToClients"), static_cast<qulonglong>(snapshot.bytesToClients)},
            {QStringLiteral("rejectedClients"), static_cast<qulonglong>(snapshot.rejectedClients)},
        });
    }
    return result;
}

QString AppController::portForwardingOperationError() const
{
    return m_portForwardingOperationError;
}

QString AppController::startLocalTerminal()
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("The maximum of 32 terminal tabs is already open"));
        }
        return {};
    }

    const std::string previousActiveWorkspaceId = m_workspaceState.activeTerminalWorkspaceId;
    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->workspaceId = tab->id;
    tab->paneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->title = tr("PowerShell %1").arg(m_nextLocalTabNumber++);
    tab->status = tr("Starting local terminal...");
    tab->kind = TerminalTabKind::Local;
    tab->local = m_localSessionFactory();
    if (!tab->local)
    {
        return {};
    }
    initializeSessionLog(*tab);
    initializeTerminalOutputSink(*tab);
    QString tabId = tab->id;
    auto workspace = workbench::makeSinglePaneTerminalWorkspace(
        utf8String(tab->workspaceId), utf8String(tab->paneId),
        {.id = utf8String(tab->id), .title = utf8String(tab->title), .kind = workbench::TerminalRestoreKind::Local});
    workspace.title = utf8String(tab->title);
    connectLocalTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    m_workspaceState.terminalWorkspaces.push_back(std::move(workspace));
    m_workspaceState.activeTerminalWorkspaceId = utf8String(tabId);
    if (!persistTerminalWorkspaces())
    {
        m_workspaceState.terminalWorkspaces.pop_back();
        m_workspaceState.activeTerminalWorkspaceId = previousActiveWorkspaceId;
        m_tabs.pop_back();
        return {};
    }
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->local)
    {
        return {};
    }
    const std::error_code error = created->local->start({.columns = 100, .rows = 30});
    if (error)
    {
        created->status = tr("Unable to start local terminal: %1").arg(QString::fromStdString(error.message()));
        showActiveTab();
        emit terminalTabsChanged();
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return tabId;
}

bool AppController::activateTerminalTab(const QString &id)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(workspaceId);
    if (workspace == nullptr)
    {
        return false;
    }
    const TerminalTab *focused = findTabForPane(utf8QString(workspace->activePaneId));
    if (focused == nullptr)
    {
        return false;
    }
    m_focusedTabId = focused->id;
    if (m_activeTabId == workspaceId)
    {
        updateTelemetryVisibility();
        showActiveTab();
        return true;
    }
    m_activeTabId = workspaceId;
    m_workspaceState.activeTerminalWorkspaceId = utf8String(workspaceId);
    static_cast<void>(persistTerminalWorkspaces());
    emitActiveTerminalContextChanged();
    return true;
}

bool AppController::closeTerminalTab(const QString &id)
{
    QString workspaceId = id;
    if (const TerminalTab *session = findTab(id); session != nullptr)
    {
        workspaceId = session->workspaceId;
    }
    const auto workspacePosition = std::ranges::find(m_workspaceState.terminalWorkspaces, utf8String(workspaceId),
                                                     &workbench::TerminalWorkspaceLayout::id);
    if (workspacePosition == m_workspaceState.terminalWorkspaces.end())
    {
        return false;
    }
    const bool closingActive = workspaceId == m_activeTabId;
    const auto workspaceIndex =
        static_cast<std::size_t>(std::distance(m_workspaceState.terminalWorkspaces.begin(), workspacePosition));
    std::erase_if(m_tabs, [this, &workspaceId](const std::unique_ptr<TerminalTab> &tab) {
        if (tab->workspaceId != workspaceId)
        {
            return false;
        }
        if (tab->id == m_hostKeyTabId)
        {
            clearHostKeyPrompt();
        }
        if (tab->local)
        {
            tab->local->stop();
        }
        if (tab->ssh)
        {
            tab->ssh->stop();
        }
        if (tab->sessionLog)
        {
            tab->sessionLog->stop();
        }
        stopSftpSession(*tab);
        m_terminalViewports.remove(tab->paneId);
        return true;
    });
    m_workspaceState.terminalWorkspaces.erase(workspacePosition);
    emit terminalTabsChanged();

    if (closingActive)
    {
        if (m_workspaceState.terminalWorkspaces.empty())
        {
            m_activeTabId.clear();
            m_focusedTabId.clear();
            m_workspaceState.activeTerminalWorkspaceId.clear();
            emitActiveTerminalContextChanged();
        }
        else
        {
            const std::size_t nextIndex = std::min(workspaceIndex, m_workspaceState.terminalWorkspaces.size() - 1U);
            m_activeTabId.clear();
            activateTerminalTab(utf8QString(m_workspaceState.terminalWorkspaces[nextIndex].id));
        }
    }
    static_cast<void>(persistTerminalWorkspaces());
    return true;
}

bool AppController::activateTerminalPane(const QString &paneId)
{
    TerminalTab *tab = findTabForPane(paneId);
    if (tab == nullptr)
    {
        return false;
    }
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(tab->workspaceId);
    if (workspace == nullptr)
    {
        return false;
    }
    const bool changed =
        m_activeTabId != tab->workspaceId || m_focusedTabId != tab->id || workspace->activePaneId != utf8String(paneId);
    m_activeTabId = tab->workspaceId;
    m_focusedTabId = tab->id;
    workspace->activePaneId = utf8String(paneId);
    m_workspaceState.activeTerminalWorkspaceId = utf8String(tab->workspaceId);
    m_terminal = m_terminalViewports.value(paneId);
    if (changed)
    {
        static_cast<void>(persistTerminalWorkspaces());
        emitActiveTerminalContextChanged();
        emit terminalTabsChanged();
    }
    return true;
}

bool AppController::splitActiveTerminal(const QString &orientation, const bool duplicateActive)
{
    TerminalTab *source = activeTab();
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (source == nullptr || workspace == nullptr || m_tabs.size() >= maximumTerminalTabs
        || workspace->restoreIntents.size() >= workbench::maximumTerminalPanesPerWorkspace)
    {
        return false;
    }
    if (duplicateActive && source->kind == TerminalTabKind::Ssh && source->sourceProfileId.isEmpty())
    {
        return false;
    }
    workbench::TerminalSplitOrientation splitOrientation;
    if (orientation == QStringLiteral("horizontal"))
    {
        splitOrientation = workbench::TerminalSplitOrientation::Horizontal;
    }
    else if (orientation == QStringLiteral("vertical"))
    {
        splitOrientation = workbench::TerminalSplitOrientation::Vertical;
    }
    else
    {
        return false;
    }

    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->workspaceId = source->workspaceId;
    tab->paneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    workbench::TerminalRestoreKind restoreKind = workbench::TerminalRestoreKind::Local;
    if (duplicateActive && source->kind == TerminalTabKind::Ssh)
    {
        tab->kind = TerminalTabKind::Ssh;
        tab->title = source->title;
        tab->identity = source->identity;
        tab->address = source->address;
        tab->sourceProfileId = source->sourceProfileId;
        tab->status = tr("SSH pane duplicated; reconnecting...");
        tab->keywordHighlightRules = source->keywordHighlightRules;
        tab->keywordHighlightEnabled = source->keywordHighlightEnabled;
        applyWorkspaceState(*tab);
        tab->ssh = std::make_unique<ssh::SshTerminalSession>();
        restoreKind = tab->sourceProfileId.isEmpty() ? workbench::TerminalRestoreKind::Transient
                                                     : workbench::TerminalRestoreKind::SshProfile;
    }
    else
    {
        tab->kind = TerminalTabKind::Local;
        tab->title = tr("PowerShell %1").arg(m_nextLocalTabNumber++);
        tab->status = tr("Starting local terminal...");
        tab->local = m_localSessionFactory();
        if (!tab->local)
        {
            return false;
        }
    }

    const QString tabId = tab->id;
    const QString paneId = tab->paneId;
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::splitTerminalPane(*workspace, utf8String(source->paneId),
                                      utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
                                      utf8String(paneId),
                                      {.id = utf8String(tabId),
                                       .profileId = utf8String(tab->sourceProfileId),
                                       .title = utf8String(tab->title),
                                       .kind = restoreKind},
                                      splitOrientation))
    {
        return false;
    }
    workspace->activePaneId = utf8String(paneId);
    initializeSessionLog(*tab);
    initializeTerminalOutputSink(*tab);
    if (tab->local)
    {
        tab->local->setOutputSink(tab->outputSink);
        connectLocalTabSignals(*tab);
    }
    else
    {
        tab->ssh->setOutputSink(tab->outputSink);
        connectSshTabSignals(*tab);
    }
    m_tabs.push_back(std::move(tab));
    m_focusedTabId = tabId;
    if (!persistTerminalWorkspaces())
    {
        *workspace = previous;
        m_tabs.pop_back();
        m_focusedTabId = source->id;
        return false;
    }

    TerminalTab *created = findTab(tabId);
    if (created != nullptr && created->local)
    {
        const std::error_code error = created->local->start({.columns = 100, .rows = 30});
        if (error)
        {
            created->status = tr("Unable to start local terminal: %1").arg(QString::fromStdString(error.message()));
        }
    }
    else if (created != nullptr && created->ssh && !created->sourceProfileId.isEmpty())
    {
        created->reconnectPending = true;
        attemptSshReconnect(created->id, ++created->reconnectGeneration);
    }
    emit terminalTabsChanged();
    emitActiveTerminalContextChanged();
    return true;
}

bool AppController::closeActiveTerminalPane()
{
    TerminalTab *tab = activeTab();
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (tab == nullptr || workspace == nullptr)
    {
        return false;
    }
    if (workspace->restoreIntents.size() == 1)
    {
        return closeTerminalTab(workspace->id.empty() ? QString{} : utf8QString(workspace->id));
    }
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::closeTerminalPane(*workspace, utf8String(tab->paneId)))
    {
        return false;
    }
    TerminalTab *next = findTabForPane(utf8QString(workspace->activePaneId));
    if (next == nullptr || !persistTerminalWorkspaces())
    {
        *workspace = previous;
        return false;
    }
    if (tab->local)
    {
        tab->local->stop();
    }
    if (tab->ssh)
    {
        tab->ssh->stop();
    }
    if (tab->sessionLog)
    {
        tab->sessionLog->stop();
    }
    stopSftpSession(*tab);
    m_terminalViewports.remove(tab->paneId);
    const QString closedId = tab->id;
    std::erase_if(m_tabs, [&closedId](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id == closedId;
    });
    m_focusedTabId = next->id;
    emit terminalTabsChanged();
    emitActiveTerminalContextChanged();
    return true;
}

bool AppController::focusRelativeTerminalPane(const int offset)
{
    const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    const TerminalTab *tab = activeTab();
    if (workspace == nullptr || tab == nullptr || offset == 0)
    {
        return false;
    }
    const std::vector<std::string> panes = workbench::terminalPaneOrder(*workspace);
    const auto current = std::ranges::find(panes, utf8String(tab->paneId));
    if (current == panes.end() || panes.empty())
    {
        return false;
    }
    const auto index = static_cast<std::ptrdiff_t>(std::distance(panes.begin(), current));
    const auto count = static_cast<std::ptrdiff_t>(panes.size());
    const auto next = (index + static_cast<std::ptrdiff_t>(offset % static_cast<int>(count)) + count) % count;
    return activateTerminalPane(utf8QString(panes[static_cast<std::size_t>(next)]));
}

bool AppController::resizeActiveTerminalPane(const qreal delta)
{
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    const TerminalTab *tab = activeTab();
    if (workspace == nullptr || tab == nullptr || !std::isfinite(delta) || qFuzzyIsNull(delta))
    {
        return false;
    }
    const std::string paneId = utf8String(tab->paneId);
    const auto parent = std::ranges::find_if(workspace->nodes, [&paneId](const workbench::TerminalLayoutNode &node) {
        return node.kind == workbench::TerminalLayoutNodeKind::Split
               && (node.firstChildId == paneId || node.secondChildId == paneId);
    });
    if (parent == workspace->nodes.end())
    {
        return false;
    }
    const double adjusted = parent->ratio + (parent->firstChildId == paneId ? delta : -delta);
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::resizeTerminalSplit(*workspace, parent->id, adjusted) || !persistTerminalWorkspaces())
    {
        *workspace = previous;
        return false;
    }
    emit terminalWorkspaceChanged();
    return true;
}

bool AppController::setTerminalSplitRatio(const QString &splitNodeId, const qreal ratio)
{
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    if (workspace == nullptr || !std::isfinite(ratio))
    {
        return false;
    }
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::resizeTerminalSplit(*workspace, utf8String(splitNodeId), ratio) || !persistTerminalWorkspaces())
    {
        *workspace = previous;
        return false;
    }
    emit terminalWorkspaceChanged();
    return true;
}

bool AppController::swapActiveTerminalPane(const int offset)
{
    workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(m_activeTabId);
    TerminalTab *active = activeTab();
    if (workspace == nullptr || active == nullptr || offset == 0)
    {
        return false;
    }
    const std::vector<std::string> panes = workbench::terminalPaneOrder(*workspace);
    const auto current = std::ranges::find(panes, utf8String(active->paneId));
    if (current == panes.end())
    {
        return false;
    }
    const auto index = static_cast<std::ptrdiff_t>(std::distance(panes.begin(), current));
    const auto targetIndex = index + (offset < 0 ? -1 : 1);
    if (targetIndex < 0 || static_cast<std::size_t>(targetIndex) >= panes.size())
    {
        return false;
    }
    const QString otherPaneId = utf8QString(panes[static_cast<std::size_t>(targetIndex)]);
    TerminalTab *other = findTabForPane(otherPaneId);
    if (other == nullptr)
    {
        return false;
    }
    const workbench::TerminalWorkspaceLayout previous = *workspace;
    if (!workbench::swapTerminalPanes(*workspace, utf8String(active->paneId), utf8String(other->paneId)))
    {
        return false;
    }
    std::swap(active->paneId, other->paneId);
    workspace->activePaneId = utf8String(active->paneId);
    if (!persistTerminalWorkspaces())
    {
        std::swap(active->paneId, other->paneId);
        *workspace = previous;
        return false;
    }
    emit terminalWorkspaceChanged();
    showAllTerminalViewports();
    return true;
}

void AppController::searchTerminal(const QString &query, const bool backwards, const bool caseSensitive)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    if (query.isEmpty())
    {
        clearTerminalSearch();
        return;
    }

    tab->searchQuery = query;
    tab->searchCaseSensitive = caseSensitive;
    if (tab->ssh)
    {
        tab->ssh->search(query, backwards, caseSensitive);
    }
    else if (tab->local)
    {
        tab->local->search(query, backwards, caseSensitive);
    }
    emit terminalSearchChanged();
}

void AppController::clearTerminalSearch()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->searchQuery.clear();
    tab->searchCurrent = 0;
    tab->searchTotal = 0;
    if (tab->ssh)
    {
        tab->ssh->clearSearch();
    }
    else if (tab->local)
    {
        tab->local->clearSearch();
    }
    emit terminalSearchChanged();
}

bool AppController::toggleTerminalWorkbench(const QString &page)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr
        || (page != QStringLiteral("history") && page != QStringLiteral("scripts") && page != QStringLiteral("sftp")))
    {
        return false;
    }
    if (page == QStringLiteral("sftp")
        && (tab->kind != TerminalTabKind::Ssh || tab->sshPhase != ssh::SshConnectionPhase::Connected))
    {
        return false;
    }

    if (tab->workbenchOpen && tab->workbenchPage == page)
    {
        tab->workbenchOpen = false;
        if (page == QStringLiteral("sftp"))
        {
            stopSftpSession(*tab);
        }
    }
    else
    {
        if (tab->workbenchOpen && tab->workbenchPage == QStringLiteral("sftp"))
        {
            stopSftpSession(*tab);
        }
        tab->workbenchPage = page;
        tab->workbenchOpen = true;
        if (page == QStringLiteral("sftp"))
        {
            (void)startSftpSession(*tab);
        }
    }
    emit terminalTabsChanged();
    emit sftpChanged();
    persistWorkspaceState(*tab);
    return true;
}

void AppController::closeTerminalWorkbench()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->workbenchOpen)
    {
        return;
    }
    tab->workbenchOpen = false;
    if (tab->workbenchPage == QStringLiteral("sftp"))
    {
        stopSftpSession(*tab);
    }
    emit terminalTabsChanged();
    emit sftpChanged();
}

void AppController::setTerminalWorkbenchWidth(const qreal width)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    const qreal bounded = std::clamp(width, qreal{320.0}, qreal{800.0});
    if (qFuzzyCompare(tab->workbenchWidth, bounded))
    {
        return;
    }
    tab->workbenchWidth = bounded;
    persistWorkspaceState(*tab);
    emit terminalTabsChanged();
}

void AppController::moveTerminalWorkbench()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->workbenchSide =
        tab->workbenchSide == QStringLiteral("left") ? QStringLiteral("right") : QStringLiteral("left");
    persistWorkspaceState(*tab);
    emit terminalTabsChanged();
}

void AppController::toggleTerminalComposer()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->composerOpen = !tab->composerOpen;
    emit terminalTabsChanged();
}

void AppController::setTerminalComposerHeight(const qreal height)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    const qreal bounded = std::clamp(height, qreal{104.0}, qreal{360.0});
    if (qFuzzyCompare(tab->composerHeight, bounded))
    {
        return;
    }
    tab->composerHeight = bounded;
    persistWorkspaceState(*tab);
    emit terminalTabsChanged();
}

bool AppController::copyActiveTerminalAddress()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->address.isEmpty() || m_terminal == nullptr)
    {
        return false;
    }
    m_terminal->setClipboardText(tab->address);
    return true;
}

bool AppController::copyActiveSftpPath()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpPath.isEmpty() || m_terminal == nullptr)
    {
        return false;
    }
    m_terminal->setClipboardText(tab->sftpPath);
    return true;
}

bool AppController::insertTerminalCommand(const QString &command)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || !validTerminalCommand(command))
    {
        return false;
    }
    queuePaste(normalizedQuickCommandText(command).toUtf8());
    return true;
}

bool AppController::runTerminalCommand(const QString &command)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || !validTerminalCommand(command))
    {
        return false;
    }
    QString normalized = normalizedQuickCommandText(command);
    QByteArray bytes = normalized.toUtf8();
    bytes.replace('\n', '\r');
    if (bytes.isEmpty() || bytes.back() != '\r')
    {
        bytes.append('\r');
    }
    appendCapturedHistory(*tab, normalized);
    if (tab->scriptRecorder.state() == workbench::ScriptRecorderState::Recording
        && tab->scriptRecorder.recordCommand(utf8String(normalized), recorderNow()))
    {
        emit terminalTabsChanged();
    }
    tab->inputHistoryBuffer.clear();
    tab->inputHistoryBufferReliable = true;
    dispatchInput(*tab, bytes);
    return true;
}

bool AppController::startTerminalLog(const QString &localFileUrl)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sessionLog == nullptr)
    {
        return false;
    }
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    if (!tab->sessionLog->start(path))
    {
        tab->status = tr("Session log could not be started.");
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tab->status);
        }
        emit terminalTabsChanged();
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

void AppController::stopTerminalLog()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sessionLog == nullptr)
    {
        return;
    }
    tab->sessionLog->stop();
    emit terminalTabsChanged();
}

bool AppController::setActiveKeywordHighlightEnabled(const bool enabled)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->keywordHighlightEnabled == enabled)
    {
        return tab != nullptr && tab->kind == TerminalTabKind::Ssh;
    }
    const bool previous = tab->keywordHighlightEnabled;
    tab->keywordHighlightEnabled = enabled;
    if (!persistKeywordRules(*tab))
    {
        tab->keywordHighlightEnabled = previous;
        return false;
    }
    showActiveTab();
    emit terminalTabsChanged();
    return true;
}

bool AppController::saveActiveKeywordHighlightRule(const QString &id, const QString &pattern, const QString &foreground,
                                                   const QString &background, const bool enabled,
                                                   const bool caseSensitive)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh)
    {
        return false;
    }
    ssh::SshKeywordHighlightRule rule{
        .id = utf8String(id.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed()),
        .pattern = utf8String(pattern),
        .foreground = utf8String(foreground.trimmed()),
        .background = utf8String(background.trimmed()),
        .enabled = enabled,
        .caseSensitive = caseSensitive,
    };
    if (!ssh::validKeywordHighlightRule(rule))
    {
        return false;
    }
    const std::vector<ssh::SshKeywordHighlightRule> previous = tab->keywordHighlightRules;
    const auto existing = std::ranges::find(tab->keywordHighlightRules, rule.id, &ssh::SshKeywordHighlightRule::id);
    if (existing == tab->keywordHighlightRules.end())
    {
        if (tab->keywordHighlightRules.size() >= ui::maximumKeywordRules)
        {
            return false;
        }
        tab->keywordHighlightRules.push_back(std::move(rule));
    }
    else
    {
        *existing = std::move(rule);
    }
    if (!persistKeywordRules(*tab))
    {
        tab->keywordHighlightRules = previous;
        return false;
    }
    showActiveTab();
    emit terminalTabsChanged();
    return true;
}

bool AppController::deleteActiveKeywordHighlightRule(const QString &id)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh)
    {
        return false;
    }
    const std::string normalizedId = utf8String(id.trimmed());
    const auto existing =
        std::ranges::find(tab->keywordHighlightRules, normalizedId, &ssh::SshKeywordHighlightRule::id);
    if (existing == tab->keywordHighlightRules.end())
    {
        return false;
    }
    const std::vector<ssh::SshKeywordHighlightRule> previous = tab->keywordHighlightRules;
    tab->keywordHighlightRules.erase(existing);
    if (!persistKeywordRules(*tab))
    {
        tab->keywordHighlightRules = previous;
        return false;
    }
    showActiveTab();
    emit terminalTabsChanged();
    return true;
}

bool AppController::setActiveTerminalEncoding(const QString &encoding)
{
    TerminalTab *tab = activeTab();
    const auto parsed = terminal::terminalEncodingFromToken(encoding);
    if (tab == nullptr || !tab->ssh || !parsed)
    {
        return false;
    }
    tab->terminalEncoding = terminal::terminalEncodingToken(*parsed);
    tab->ssh->setEncoding(tab->terminalEncoding);
    emit terminalTabsChanged();
    return true;
}

bool AppController::setActiveTerminalAppearance(const QString &fontFamily, const int fontSize, const bool ligatures,
                                                const qreal backgroundOpacity, const QString &cursor,
                                                const QString &foreground, const QString &background)
{
    TerminalTab *tab = activeTab();
    const QString normalizedCursor = cursor.trimmed().toLower();
    const QColor foregroundColor(foreground);
    const QColor backgroundColor(background);
    if (tab == nullptr || fontFamily.trimmed().isEmpty() || fontSize < 8 || fontSize > 32 || backgroundOpacity < 0.0
        || backgroundOpacity > 1.0
        || (normalizedCursor != QStringLiteral("terminal") && normalizedCursor != QStringLiteral("block")
            && normalizedCursor != QStringLiteral("bar") && normalizedCursor != QStringLiteral("underline"))
        || !foregroundColor.isValid() || !backgroundColor.isValid())
    {
        return false;
    }
    tab->sessionFontFamily = fontFamily.trimmed();
    tab->sessionFontSize = fontSize;
    tab->sessionLigatures = ligatures;
    tab->sessionBackgroundOpacity = backgroundOpacity;
    tab->sessionCursor = normalizedCursor;
    tab->sessionForeground = foregroundColor.name(QColor::HexRgb);
    tab->sessionBackground = backgroundColor.name(QColor::HexRgb);
    emit terminalTabsChanged();
    return true;
}

bool AppController::resetActiveTerminalAppearance()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return false;
    }
    tab->sessionFontFamily.clear();
    tab->sessionFontSize = 0;
    tab->sessionLigatures = m_settings.terminalLigatures;
    tab->sessionBackgroundOpacity = -1.0;
    tab->sessionCursor.clear();
    tab->sessionForeground.clear();
    tab->sessionBackground.clear();
    emit terminalTabsChanged();
    return true;
}

bool AppController::startTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || !tab->scriptRecorder.start(recorderNow()))
    {
        return false;
    }
    ++tab->scriptPlaybackGeneration;
    tab->scriptPlaybackActive = false;
    tab->recordingStartedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    emit terminalTabsChanged();
    return true;
}

bool AppController::pauseTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->scriptRecorder.pause())
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::resumeTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->scriptRecorder.resume(recorderNow()))
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::stopTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->scriptRecorder.stop())
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::replayTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || !tab->running || tab->scriptRecorder.state() != workbench::ScriptRecorderState::Review
        || tab->scriptRecorder.steps().empty() || tab->scriptPlaybackActive)
    {
        return false;
    }
    tab->scriptPlaybackActive = true;
    const std::uint64_t generation = ++tab->scriptPlaybackGeneration;
    const QString tabId = tab->id;
    emit terminalTabsChanged();
    replayRecordedScriptStep(tabId, 0, generation);
    return true;
}

bool AppController::copyTerminalScriptRecording()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->scriptRecorder.steps().empty())
    {
        return false;
    }
    QJsonArray steps;
    for (const workbench::RecordedScriptStep &step : tab->scriptRecorder.steps())
    {
        if (step.kind == workbench::RecordedScriptStepKind::Send)
        {
            steps.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("send")},
                                     {QStringLiteral("value"), utf8QString(step.command)}});
        }
        else
        {
            steps.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("delay")},
                                     {QStringLiteral("milliseconds"), static_cast<qint64>(step.delayMilliseconds)}});
        }
    }
    const QJsonDocument document(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("steps"), steps}});
    QGuiApplication::clipboard()->setText(QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
    return true;
}

void AppController::clearTerminalScriptRecording()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    ++tab->scriptPlaybackGeneration;
    tab->scriptPlaybackActive = false;
    tab->recordingStartedUtcMs = 0;
    tab->scriptRecorder.clear();
    emit terminalTabsChanged();
}

bool AppController::saveScript(const QVariantMap &value)
{
    const auto shellScope = quickCommandShellScope(value.value(QStringLiteral("shell")).toString());
    const QVariantList variableValues = value.value(QStringLiteral("variables")).toList();
    const QVariantList stepValues = value.value(QStringLiteral("steps")).toList();
    if (!shellScope || variableValues.size() > static_cast<qsizetype>(workbench::maximumScriptVariableCount)
        || stepValues.size() > static_cast<qsizetype>(workbench::maximumScriptStepCount))
    {
        setQuickCommandOperationError(tr("The script definition is invalid."));
        return false;
    }

    const QString suppliedId = value.value(QStringLiteral("id")).toString().trimmed();
    const std::int64_t now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    workbench::ScriptDefinition script{
        .id = suppliedId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()
                                   : utf8String(suppliedId),
        .name = utf8String(value.value(QStringLiteral("name")).toString().trimmed()),
        .description = utf8String(value.value(QStringLiteral("description")).toString().trimmed()),
        .shellScope = *shellScope,
        .createdUtcMs = now,
        .modifiedUtcMs = now,
    };
    script.variables.reserve(static_cast<std::size_t>(variableValues.size()));
    for (const QVariant &entry : variableValues)
    {
        const QVariantMap variableValue = entry.toMap();
        const auto type = parseScriptVariableType(variableValue.value(QStringLiteral("type")).toString());
        if (!type)
        {
            setQuickCommandOperationError(tr("The script contains an invalid variable."));
            return false;
        }
        std::vector<std::string> choices;
        const QVariantList choiceValues = variableValue.value(QStringLiteral("choices")).toList();
        choices.reserve(static_cast<std::size_t>(choiceValues.size()));
        for (const QVariant &choice : choiceValues)
        {
            choices.push_back(utf8String(choice.toString()));
        }
        script.variables.push_back({
            .name = utf8String(variableValue.value(QStringLiteral("name")).toString().trimmed()),
            .label = utf8String(variableValue.value(QStringLiteral("label")).toString().trimmed()),
            .type = *type,
            .defaultValue = utf8String(variableValue.value(QStringLiteral("defaultValue")).toString()),
            .choices = std::move(choices),
            .required = variableValue.value(QStringLiteral("required")).toBool(),
        });
    }
    script.steps.reserve(static_cast<std::size_t>(stepValues.size()));
    for (const QVariant &entry : stepValues)
    {
        const QVariantMap stepValue = entry.toMap();
        const auto continuation = parseScriptContinuation(stepValue.value(QStringLiteral("continuation")).toString());
        bool validTimeout = false;
        const qlonglong timeout = stepValue.value(QStringLiteral("timeoutMs")).toLongLong(&validTimeout);
        if (!continuation || !validTimeout || timeout < 0
            || timeout > static_cast<qlonglong>(std::numeric_limits<std::uint32_t>::max()))
        {
            setQuickCommandOperationError(tr("The script contains an invalid step."));
            return false;
        }
        script.steps.push_back({
            .command = utf8String(normalizedQuickCommandText(stepValue.value(QStringLiteral("command")).toString())),
            .continuation = *continuation,
            .outputMarker = utf8String(stepValue.value(QStringLiteral("outputMarker")).toString()),
            .timeoutMs = static_cast<std::uint32_t>(timeout),
        });
    }
    if (!workbench::validScriptDefinition(script))
    {
        setQuickCommandOperationError(tr("The script definition is invalid."));
        return false;
    }

    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    if (suppliedId.isEmpty())
    {
        if (candidate.size() >= workbench::maximumScriptCount)
        {
            setQuickCommandOperationError(tr("The script library has reached its 256 item limit."));
            return false;
        }
        candidate.push_back(std::move(script));
    }
    else
    {
        const auto existing = std::ranges::find(candidate, script.id, &workbench::ScriptDefinition::id);
        if (existing == candidate.end())
        {
            setQuickCommandOperationError(tr("The script no longer exists."));
            return false;
        }
        script.createdUtcMs = existing->createdUtcMs;
        *existing = std::move(script);
    }
    if (!m_scriptStore.save(candidate))
    {
        setQuickCommandOperationError(tr("The script could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

QVariantMap AppController::renderScript(const QString &id, const QVariantMap &values) const
{
    const auto existing = std::ranges::find(m_scripts, utf8String(id), &workbench::ScriptDefinition::id);
    if (existing == m_scripts.end())
    {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("not-found")}};
    }
    workbench::ScriptVariableValues variables;
    variables.reserve(static_cast<std::size_t>(values.size()));
    for (auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator)
    {
        variables.emplace(utf8String(iterator.key()), utf8String(iterator.value().toString()));
    }
    const auto rendered = workbench::renderScript(*existing, variables);
    if (!rendered)
    {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), scriptRenderErrorToken(rendered.error())}};
    }
    QVariantList steps;
    steps.reserve(static_cast<qsizetype>(rendered->steps.size()));
    for (const workbench::RenderedScriptStep &step : rendered->steps)
    {
        steps.append(QVariantMap{{QStringLiteral("command"), utf8QString(step.command)},
                                 {QStringLiteral("continuation"), scriptContinuationToken(step.continuation)},
                                 {QStringLiteral("outputMarker"), utf8QString(step.outputMarker)},
                                 {QStringLiteral("timeoutMs"), step.timeoutMs}});
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("id"), utf8QString(rendered->id)},
            {QStringLiteral("name"), utf8QString(rendered->name)},
            {QStringLiteral("steps"), steps}};
}

bool AppController::runScript(const QString &id, const QVariantMap &values, const QString &targetSessionId)
{
    TerminalTab *tab = findTab(targetSessionId);
    const auto existing = std::ranges::find(m_scripts, utf8String(id), &workbench::ScriptDefinition::id);
    if (tab == nullptr || !tab->running || existing == m_scripts.end())
    {
        setQuickCommandOperationError(tr("Choose a running target terminal for the script."));
        return false;
    }
    workbench::ScriptVariableValues variables;
    variables.reserve(static_cast<std::size_t>(values.size()));
    for (auto iterator = values.constBegin(); iterator != values.constEnd(); ++iterator)
    {
        variables.emplace(utf8String(iterator.key()), utf8String(iterator.value().toString()));
    }
    auto rendered = workbench::renderScript(*existing, variables);
    if (!rendered)
    {
        setQuickCommandOperationError(tr("Fill every required script variable with a valid value."));
        return false;
    }
    auto commands = tab->scriptExecution.start(std::move(*rendered), utf8String(tab->id), scriptExecutionNow());
    if (!commands)
    {
        setQuickCommandOperationError(tab->scriptExecution.active()
                                          ? tr("A script is already running in this terminal.")
                                          : tr("The script could not be started."));
        return false;
    }
    m_quickCommandOperationError.clear();
    dispatchScriptCommands(*tab, *commands);
    emit quickCommandsChanged();
    emit terminalTabsChanged();
    return true;
}

bool AppController::cancelScript(const QString &targetSessionId)
{
    TerminalTab *tab = findTab(targetSessionId);
    if (tab == nullptr || !tab->scriptExecution.cancel())
    {
        return false;
    }
    emit terminalTabsChanged();
    return true;
}

bool AppController::saveQuickCommand(const QString &id, const QString &name, const QString &command,
                                     const QString &description, const QString &shellScope)
{
    const QString normalizedName = name.trimmed();
    const QString normalizedCommand = normalizedQuickCommandText(command);
    const QString normalizedDescription = description.trimmed();
    const auto parsedShellScope = quickCommandShellScope(shellScope);
    if (normalizedName.isEmpty() || normalizedCommand.trimmed().isEmpty() || !parsedShellScope)
    {
        setQuickCommandOperationError(tr("Enter a name and command, then choose a valid shell scope."));
        return false;
    }

    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    const std::int64_t now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    workbench::ScriptDefinition script{
        .id = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() : utf8String(id),
        .name = utf8String(normalizedName),
        .description = utf8String(normalizedDescription),
        .shellScope = *parsedShellScope,
        .variables = {},
        .steps = {{.command = utf8String(normalizedCommand)}},
        .createdUtcMs = now,
        .modifiedUtcMs = now,
    };

    if (id.isEmpty())
    {
        candidate.push_back(std::move(script));
    }
    else
    {
        const auto existing = std::ranges::find(candidate, script.id, &workbench::ScriptDefinition::id);
        if (existing == candidate.end())
        {
            setQuickCommandOperationError(tr("The script no longer exists."));
            return false;
        }
        script.createdUtcMs = existing->createdUtcMs;
        *existing = std::move(script);
    }

    if (!m_scriptStore.save(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist scripts";
        setQuickCommandOperationError(tr("The script could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

bool AppController::deleteQuickCommand(const QString &id)
{
    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    const std::string commandId = utf8String(id);
    const auto existing = std::ranges::find(candidate, commandId, &workbench::ScriptDefinition::id);
    if (existing == candidate.end())
    {
        setQuickCommandOperationError(tr("The script no longer exists."));
        return false;
    }
    candidate.erase(existing);
    if (!m_scriptStore.save(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist script deletion";
        setQuickCommandOperationError(tr("The script could not be deleted."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

bool AppController::moveQuickCommand(const QString &id, const int targetIndex)
{
    if (m_scripts.empty())
    {
        return false;
    }
    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    const std::string commandId = utf8String(id);
    const auto existing = std::ranges::find(candidate, commandId, &workbench::ScriptDefinition::id);
    if (existing == candidate.end())
    {
        setQuickCommandOperationError(tr("The script no longer exists."));
        return false;
    }

    const auto sourceIndex = static_cast<std::size_t>(std::distance(candidate.begin(), existing));
    const auto boundedTarget =
        static_cast<std::size_t>(std::clamp(targetIndex, 0, static_cast<int>(candidate.size() - 1U)));
    if (sourceIndex == boundedTarget)
    {
        return true;
    }
    workbench::ScriptDefinition moved = std::move(candidate[sourceIndex]);
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
    candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(boundedTarget), std::move(moved));
    if (!m_scriptStore.save(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist script order";
        setQuickCommandOperationError(tr("The script order could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    m_quickCommandOperationError.clear();
    emit quickCommandsChanged();
    return true;
}

bool AppController::importQuickCommands(const QString &localFileUrl)
{
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    if (!QFileInfo(path).isFile())
    {
        setQuickCommandOperationError(tr("The script library could not be imported."));
        return false;
    }
    std::vector<workbench::ScriptDefinition> imported;
    const workbench::ScriptStore source(path);
    if (auto scripts = source.load())
    {
        imported = std::move(*scripts);
    }
    else if (scripts.error() == workbench::ScriptStoreError::unsupportedVersion)
    {
        const auto legacy = workbench::QuickCommandStore(path).load();
        if (!legacy)
        {
            setQuickCommandOperationError(tr("The script library could not be imported."));
            return false;
        }
        imported.reserve(legacy->size());
        std::ranges::transform(*legacy, std::back_inserter(imported), workbench::scriptFromQuickCommand);
    }
    else
    {
        setQuickCommandOperationError(tr("The script library could not be imported."));
        return false;
    }
    if (m_scripts.size() + imported.size() > workbench::maximumScriptCount)
    {
        setQuickCommandOperationError(tr("The imported script library exceeds the 256 item limit."));
        return false;
    }

    std::vector<workbench::ScriptDefinition> candidate = m_scripts;
    QSet<QString> ids;
    ids.reserve(static_cast<qsizetype>(candidate.size() + imported.size()));
    for (const workbench::ScriptDefinition &script : candidate)
    {
        ids.insert(utf8QString(script.id));
    }
    for (workbench::ScriptDefinition &script : imported)
    {
        QString id = utf8QString(script.id);
        if (ids.contains(id))
        {
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            script.id = utf8String(id);
        }
        ids.insert(id);
        candidate.push_back(std::move(script));
    }
    if (!m_scriptStore.save(candidate))
    {
        setQuickCommandOperationError(tr("The imported script library could not be saved."));
        return false;
    }
    m_scripts = std::move(candidate);
    setQuickCommandOperationError({});
    return true;
}

bool AppController::exportQuickCommands(const QString &localFileUrl)
{
    const QUrl url(localFileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : localFileUrl;
    const workbench::ScriptStore destination(path);
    if (!destination.save(m_scripts))
    {
        setQuickCommandOperationError(tr("The script library could not be exported."));
        return false;
    }
    setQuickCommandOperationError({});
    return true;
}

void AppController::refreshTerminalHistory()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    if (tab->kind == TerminalTabKind::Ssh)
    {
        if (tab->ssh == nullptr || !tab->running)
        {
            tab->historyState = QStringLiteral("error");
            tab->historyError = tr("Connect the SSH terminal before reading remote history.");
            emit terminalHistoryChanged();
            return;
        }
        tab->historyState = QStringLiteral("loading");
        tab->historyError.clear();
        const std::uint64_t requestId = ++tab->historyRequestId;
        emit terminalHistoryChanged();
        tab->ssh->requestShellHistory(requestId);
        return;
    }

    const QString historyPath = workbench::defaultPowerShellHistoryPath();
    if (historyPath.isEmpty())
    {
        tab->historyState = QStringLiteral("error");
        tab->historyError = tr("The PowerShell history location is unavailable.");
        emit terminalHistoryChanged();
        return;
    }

    tab->historyState = QStringLiteral("loading");
    tab->historyError.clear();
    const QString tabId = tab->id;
    const std::uint64_t requestId = ++tab->historyRequestId;
    const QString historyReadError = tr("PowerShell history could not be read.");
    emit terminalHistoryChanged();

    const QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, historyPath, tabId, requestId, historyReadError] {
        std::vector<workbench::ShellHistoryEntry> entries;
        bool readFailed = true;
        try
        {
            auto result = workbench::readPowerShellHistory(historyPath);
            if (result)
            {
                entries = std::move(*result);
                readFailed = false;
            }
        }
        catch (const std::bad_alloc &)
        {
            readFailed = true;
        }
        if (self)
        {
            emit self->terminalHistoryTaskCompleted(tabId, requestId, std::move(entries),
                                                    readFailed ? historyReadError : QString{});
        }
    });
}

void AppController::setTerminalTelemetryVisible(const bool visible)
{
    if (m_terminalTelemetryVisible == visible)
    {
        return;
    }
    m_terminalTelemetryVisible = visible;
    updateTelemetryVisibility();
}

void AppController::refreshRemoteTelemetry()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->ssh == nullptr || !tab->running)
    {
        return;
    }
    tab->telemetryState = QStringLiteral("loading");
    emit remoteTelemetryChanged();
    tab->ssh->refreshRemoteTelemetry();
}

void AppController::refreshSftpDirectory()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->sftpSession == nullptr && tab->workbenchPage == QStringLiteral("sftp"))
    {
        (void)startSftpSession(*tab);
        return;
    }
    if (tab != nullptr && tab->sftpSession != nullptr)
    {
        requestSftpDirectory(*tab, tab->sftpPath);
    }
}

bool AppController::navigateSftpDirectory(const QString &remotePath)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr)
    {
        return false;
    }
    const auto normalized = sftp::normalizeRemotePath(utf8String(remotePath));
    if (!normalized)
    {
        return false;
    }
    requestSftpDirectory(*tab, utf8QString(*normalized));
    return true;
}

bool AppController::navigateSftpHome()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr || tab->sftpHomePath.isEmpty())
    {
        return false;
    }
    requestSftpDirectory(*tab, tab->sftpHomePath);
    return true;
}

bool AppController::navigateSftpParent()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr)
    {
        return false;
    }
    requestSftpDirectory(*tab, utf8QString(sftp::parentRemotePath(utf8String(tab->sftpPath))));
    return true;
}

bool AppController::toggleActiveSftpBookmark()
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sourceProfileId.isEmpty() || tab->sftpPath.isEmpty())
    {
        return false;
    }
    workbench::WorkspaceState candidate = m_workspaceState;
    workbench::ProfileWorkspaceState &state =
        workbench::ensureProfileWorkspaceState(candidate, utf8String(tab->sourceProfileId));
    (void)workbench::toggleBookmarkedRemotePath(state, utf8String(tab->sftpPath));
    if (!saveWorkspaceStateCandidate(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist SFTP path bookmark";
        return false;
    }
    m_workspaceState = std::move(candidate);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpViewMode(const QString &mode)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || (mode != QStringLiteral("list") && mode != QStringLiteral("tree")))
    {
        return false;
    }
    if (tab->sftpViewMode == mode)
    {
        return true;
    }
    tab->sftpViewMode = mode;
    if (tab->sftpModel != nullptr)
    {
        tab->sftpModel->setViewMode(mode);
    }
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpSort(const QString &column, const bool ascending)
{
    TerminalTab *tab = activeTab();
    const QString normalized = column.trimmed().toLower();
    if (tab == nullptr
        || (normalized != QStringLiteral("name") && normalized != QStringLiteral("modified")
            && normalized != QStringLiteral("size") && normalized != QStringLiteral("type")))
    {
        return false;
    }
    tab->sftpSortColumn = normalized;
    tab->sftpSortAscending = ascending;
    if (tab->sftpModel != nullptr)
    {
        tab->sftpModel->setSortColumn(normalized);
        tab->sftpModel->setSortAscending(ascending);
    }
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpDirectoriesFirst(const bool enabled)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return false;
    }
    tab->sftpDirectoriesFirst = enabled;
    if (tab->sftpModel != nullptr)
    {
        tab->sftpModel->setDirectoriesFirst(enabled);
    }
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpVisibleColumns(const bool modified, const bool size, const bool type)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return false;
    }
    tab->sftpShowModifiedColumn = modified;
    tab->sftpShowSizeColumn = size;
    tab->sftpShowTypeColumn = type;
    persistWorkspaceState(*tab);
    emit sftpChanged();
    return true;
}

bool AppController::setSftpFilenameEncoding(const QString &encoding)
{
    TerminalTab *tab = activeTab();
    const QString normalized = encoding.trimmed().toLower();
    if (tab == nullptr || (normalized != QStringLiteral("utf-8") && normalized != QStringLiteral("gb18030")))
    {
        return false;
    }
    if (tab->sftpFilenameEncoding == normalized)
    {
        return true;
    }
    tab->sftpFilenameEncoding = normalized;
    persistWorkspaceState(*tab);
    stopSftpSession(*tab);
    const bool started = startSftpSession(*tab);
    emit sftpChanged();
    return started;
}

bool AppController::navigateSftpToTerminalDirectory()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->sftpSession == nullptr || tab->terminalWorkingDirectory.isEmpty())
    {
        return false;
    }
    requestSftpDirectory(*tab, tab->terminalWorkingDirectory);
    return true;
}

bool AppController::setSftpFollowTerminalDirectory(const bool enabled)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || (enabled && tab->kind != TerminalTabKind::Ssh))
    {
        return false;
    }
    if (tab->followTerminalDirectory == enabled)
    {
        return true;
    }
    tab->followTerminalDirectory = enabled;
    persistWorkspaceState(*tab);
    emit sftpChanged();
    if (enabled && tab->sftpSession != nullptr && !tab->terminalWorkingDirectory.isEmpty()
        && tab->terminalWorkingDirectory != tab->sftpPath)
    {
        requestSftpDirectory(*tab, tab->terminalWorkingDirectory);
    }
    return true;
}

bool AppController::createSftpDirectory(const QString &name)
{
    TerminalTab *tab = activeTab();
    const std::string remoteName = utf8String(name.trimmed());
    if (tab == nullptr || tab->sftpSession == nullptr || !sftp::validRemoteName(remoteName))
    {
        return false;
    }
    const auto path = sftp::joinRemotePath(utf8String(tab->sftpPath), remoteName);
    if (!path)
    {
        return false;
    }
    tab->sftpSession->requestCreateDirectory(++tab->sftpRequestId, utf8QString(*path));
    return true;
}

bool AppController::createSftpFile(const QString &name)
{
    TerminalTab *tab = activeTab();
    const std::string remoteName = utf8String(name.trimmed());
    if (tab == nullptr || tab->sftpSession == nullptr || !sftp::validRemoteName(remoteName))
    {
        return false;
    }
    const auto path = sftp::joinRemotePath(utf8String(tab->sftpPath), remoteName);
    if (!path)
    {
        return false;
    }
    tab->sftpSession->requestCreateFile(++tab->sftpRequestId, utf8QString(*path));
    return true;
}

bool AppController::renameSftpEntry(const QString &remotePath, const QString &newName)
{
    TerminalTab *tab = activeTab();
    const std::string source = utf8String(remotePath);
    const std::string replacement = utf8String(newName.trimmed());
    if (tab == nullptr || tab->sftpSession == nullptr || !sftp::validRemoteName(replacement))
    {
        return false;
    }
    const auto destination = sftp::joinRemotePath(sftp::parentRemotePath(source), replacement);
    if (!destination)
    {
        return false;
    }
    tab->sftpSession->requestRenameEntry(++tab->sftpRequestId, remotePath, utf8QString(*destination));
    return true;
}

bool AppController::removeSftpEntry(const QString &remotePath, const bool directory)
{
    TerminalTab *tab = activeTab();
    const auto normalized = sftp::normalizeRemotePath(utf8String(remotePath));
    if (tab == nullptr || tab->sftpSession == nullptr || !normalized || *normalized == "/")
    {
        return false;
    }
    tab->sftpSession->requestRemoveEntry(++tab->sftpRequestId, utf8QString(*normalized), directory);
    return true;
}

bool AppController::enqueueSftpDownload(const QString &remotePath, const QString &localFileUrl,
                                        const qulonglong totalBytes, const qlonglong modifiedUtcSeconds)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->sourceProfileId.isEmpty()
        || m_transferManager == nullptr)
    {
        return false;
    }
    const auto normalized = sftp::normalizeRemotePath(utf8String(remotePath));
    const QString localPath = QUrl(localFileUrl).isLocalFile() ? QUrl(localFileUrl).toLocalFile() : localFileUrl;
    if (!normalized || localPath.trimmed().isEmpty())
    {
        return false;
    }
    const QFileInfo destination(localPath);
    sftp::TransferTask task{
        .id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab->sourceProfileId),
        .displayName = utf8String(QFileInfo(utf8QString(*normalized)).fileName()),
        .sourcePath = *normalized,
        .destinationPath = utf8String(destination.absoluteFilePath()),
        .filenameEncoding = utf8String(tab->sftpFilenameEncoding),
        .direction = sftp::TransferDirection::Download,
        .totalBytes = totalBytes,
        .sourceModifiedUtcSeconds =
            modifiedUtcSeconds >= 0 ? std::optional<std::int64_t>{modifiedUtcSeconds} : std::nullopt,
    };
    const auto queued = m_transferManager->enqueue(std::move(task), transferRequestProvider(tab->sourceProfileId), {});
    return queued.has_value();
}

bool AppController::enqueueSftpUpload(const QString &localFileUrl)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->sourceProfileId.isEmpty()
        || m_transferManager == nullptr)
    {
        return false;
    }
    const QUrl localUrl(localFileUrl);
    const QString localPath = localUrl.isLocalFile() ? localUrl.toLocalFile() : localFileUrl;
    const QFileInfo source(localPath);
    if (source.isDir())
    {
        return enqueueSftpUploadBatch({localFileUrl});
    }
    if (!source.exists() || !source.isFile())
    {
        return false;
    }
    const auto remotePath = sftp::joinRemotePath(utf8String(tab->sftpPath), utf8String(source.fileName()));
    if (!remotePath)
    {
        return false;
    }
    sftp::TransferTask task{
        .id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab->sourceProfileId),
        .displayName = utf8String(source.fileName()),
        .sourcePath = utf8String(source.absoluteFilePath()),
        .destinationPath = *remotePath,
        .filenameEncoding = utf8String(tab->sftpFilenameEncoding),
        .direction = sftp::TransferDirection::Upload,
        .totalBytes = static_cast<std::uint64_t>(source.size()),
        .sourceModifiedUtcSeconds = source.lastModified().toSecsSinceEpoch(),
    };
    const auto queued = m_transferManager->enqueue(std::move(task), transferRequestProvider(tab->sourceProfileId), {});
    return queued.has_value();
}

bool AppController::enqueueSftpUploadBatch(const QStringList &localFileUrls)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->sourceProfileId.isEmpty()
        || m_transferBatchCoordinator == nullptr || localFileUrls.isEmpty()
        || localFileUrls.size() > static_cast<qsizetype>(sftp::maximumTransferSourceRoots))
    {
        return false;
    }
    std::vector<std::string> roots;
    roots.reserve(static_cast<std::size_t>(localFileUrls.size()));
    for (const QString &value : localFileUrls)
    {
        const QUrl url(value);
        const QString path = url.isLocalFile() ? url.toLocalFile() : value;
        const QFileInfo source(path);
        if (!source.exists() || (!source.isFile() && !source.isDir()))
        {
            return false;
        }
        roots.push_back(utf8String(source.absoluteFilePath()));
    }
    const QString displayName = roots.size() == 1 ? QFileInfo(utf8QString(roots.front())).fileName()
                                                  : tr("Upload %1 items").arg(static_cast<qulonglong>(roots.size()));
    sftp::TransferPlanRequest request{
        .batchId = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab->sourceProfileId),
        .displayName = utf8String(displayName),
        .destinationRoot = utf8String(tab->sftpPath),
        .sourceRoots = std::move(roots),
        .direction = sftp::TransferBatchDirection::Upload,
    };
    return m_transferBatchCoordinator
        ->enqueue(std::move(request), transferRequestProvider(tab->sourceProfileId),
                  utf8String(tab->sftpFilenameEncoding))
        .has_value();
}

bool AppController::enqueueSftpDownloadBatch(const QStringList &remotePaths, const QString &localDirectoryUrl)
{
    const TerminalTab *tab = activeTab();
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->sourceProfileId.isEmpty()
        || m_transferBatchCoordinator == nullptr || remotePaths.isEmpty()
        || remotePaths.size() > static_cast<qsizetype>(sftp::maximumTransferSourceRoots))
    {
        return false;
    }
    const QUrl destinationUrl(localDirectoryUrl);
    const QString destinationPath = destinationUrl.isLocalFile() ? destinationUrl.toLocalFile() : localDirectoryUrl;
    if (!QFileInfo(destinationPath).isDir())
    {
        return false;
    }
    std::vector<std::string> roots;
    roots.reserve(static_cast<std::size_t>(remotePaths.size()));
    for (const QString &path : remotePaths)
    {
        auto normalized = sftp::normalizeRemotePath(utf8String(path));
        if (!normalized || *normalized == "/")
        {
            return false;
        }
        roots.push_back(std::move(*normalized));
    }
    const QString displayName = roots.size() == 1 ? QFileInfo(utf8QString(roots.front())).fileName()
                                                  : tr("Download %1 items").arg(static_cast<qulonglong>(roots.size()));
    sftp::TransferPlanRequest request{
        .batchId = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces)),
        .endpointId = utf8String(tab->sourceProfileId),
        .displayName = utf8String(displayName),
        .destinationRoot = utf8String(QFileInfo(destinationPath).absoluteFilePath()),
        .sourceRoots = std::move(roots),
        .direction = sftp::TransferBatchDirection::Download,
    };
    return m_transferBatchCoordinator
        ->enqueue(std::move(request), transferRequestProvider(tab->sourceProfileId),
                  utf8String(tab->sftpFilenameEncoding))
        .has_value();
}

void AppController::cancelTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->cancel(batchId);
    }
}

void AppController::pauseTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->pause(batchId);
    }
}

void AppController::resumeTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->resume(batchId);
    }
}

void AppController::retryTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->retry(batchId);
    }
}

void AppController::dismissTransferBatch(const QString &batchId)
{
    if (m_transferBatchCoordinator)
    {
        m_transferBatchCoordinator->dismiss(batchId);
    }
}

void AppController::cancelTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->cancel(taskId);
    }
}

void AppController::pauseTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->pause(taskId);
    }
}

void AppController::resumeTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->resume(taskId);
    }
}

void AppController::retryTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->retry(taskId);
    }
}

void AppController::pauseAllTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        m_transferBatchCoordinator->pauseAll();
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->pauseAll();
    }
}

void AppController::resumeAllTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        m_transferBatchCoordinator->resumeAll();
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->resumeAll();
    }
}

void AppController::cancelAllTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        m_transferBatchCoordinator->cancelAll();
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->cancelAll();
    }
}

bool AppController::copyTransferPath(const QString &taskId)
{
    const auto found = std::ranges::find_if(m_transferTasks, [&taskId](const QVariant &value) {
        return value.toMap().value(QStringLiteral("id")).toString() == taskId;
    });
    if (found == m_transferTasks.end())
    {
        return false;
    }
    const QVariantMap task = found->toMap();
    const QString path = task.value(QStringLiteral("destinationPath")).toString();
    if (path.isEmpty())
    {
        return false;
    }
    QGuiApplication::clipboard()->setText(path);
    return true;
}

bool AppController::openTransferTarget(const QString &taskId)
{
    const auto found = std::ranges::find_if(m_transferTasks, [&taskId](const QVariant &value) {
        return value.toMap().value(QStringLiteral("id")).toString() == taskId;
    });
    if (found == m_transferTasks.end())
    {
        return false;
    }
    const QVariantMap task = found->toMap();
    if (task.value(QStringLiteral("direction")).toString() != QStringLiteral("download")
        || task.value(QStringLiteral("status")).toString() != QStringLiteral("completed"))
    {
        return false;
    }
    const QFileInfo file(task.value(QStringLiteral("destinationPath")).toString());
    return file.exists() && QDesktopServices::openUrl(QUrl::fromLocalFile(file.absolutePath()));
}

void AppController::dismissTransfer(const QString &taskId)
{
    if (m_transferManager != nullptr)
    {
        m_transferManager->dismiss(taskId);
    }
}

void AppController::clearFinishedTransfers()
{
    if (m_transferBatchCoordinator != nullptr)
    {
        const QVariantList batches = m_transferBatches;
        for (const QVariant &value : batches)
        {
            const QVariantMap batch = value.toMap();
            const QString status = batch.value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("completed") || status == QStringLiteral("failed")
                || status == QStringLiteral("cancelled"))
            {
                m_transferBatchCoordinator->dismiss(batch.value(QStringLiteral("id")).toString());
            }
        }
    }
    if (m_transferManager != nullptr)
    {
        m_transferManager->dismissFinished();
    }
}

void AppController::resolveTransferConflict(const QString &taskId, const QString &action,
                                            const QString &renamedDestinationPath, const bool applyToRemainingBatch)
{
    if (m_transferManager == nullptr)
    {
        return;
    }
    const QString token = action.trimmed().toLower();
    const auto decision = token == QLatin1StringView("skip")      ? std::optional{sftp::ConflictAction::Skip}
                          : token == QLatin1StringView("replace") ? std::optional{sftp::ConflictAction::Replace}
                          : token == QLatin1StringView("rename")  ? std::optional{sftp::ConflictAction::Rename}
                          : token == QLatin1StringView("cancel")  ? std::optional{sftp::ConflictAction::Cancel}
                                                                  : std::nullopt;
    if (decision)
    {
        if (applyToRemainingBatch && m_transferBatchCoordinator != nullptr
            && (*decision == sftp::ConflictAction::Skip || *decision == sftp::ConflictAction::Replace))
        {
            m_transferBatchCoordinator->setConflictPolicyForChild(utf8String(taskId),
                                                                  *decision == sftp::ConflictAction::Skip
                                                                      ? sftp::TransferConflictPolicy::Skip
                                                                      : sftp::TransferConflictPolicy::Replace,
                                                                  true);
        }
        m_transferManager->resolveConflict(taskId, *decision, renamedDestinationPath);
    }
}

bool AppController::triggerAction(const QString &actionId)
{
    if (!m_actionRegistry.enabled(actionId, activeTab() != nullptr))
    {
        return false;
    }
    emit actionRequested(actionId);
    return true;
}

QVariantMap AppController::setActionShortcut(const QString &actionId, const QString &shortcut)
{
    const QMap<QString, QString> previousOverrides = m_actionRegistry.overrides();
    const actions::ShortcutValidation validation = m_actionRegistry.setShortcut(actionId, shortcut);
    if (!validation.valid())
    {
        return shortcutResult(validation);
    }

    config::ApplicationSettings updated = m_settings;
    updated.shortcutOverrides = m_actionRegistry.overrides();
    if (!m_settingsStore.save(updated))
    {
        m_actionRegistry.setOverrides(previousOverrides);
        return {
            {QStringLiteral("valid"), false},
            {QStringLiteral("shortcut"), QString{}},
            {QStringLiteral("error"), tr("The shortcut could not be saved.")},
            {QStringLiteral("conflictingActionId"), QString{}},
        };
    }

    m_settings = std::move(updated);
    emit actionRegistryChanged();
    return shortcutResult(validation);
}

QVariantMap AppController::setActionShortcutFromKey(const QString &actionId, const int key, const int modifiers)
{
    return setActionShortcut(actionId, actions::ActionRegistry::shortcutFromKeyEvent(key, modifiers));
}

bool AppController::resetActionShortcut(const QString &actionId)
{
    if (!m_actionRegistry.contains(actionId))
    {
        return false;
    }
    return setActionShortcut(actionId, m_actionRegistry.defaultShortcut(actionId))
        .value(QStringLiteral("valid"))
        .toBool();
}

bool AppController::resetAllActionShortcuts()
{
    if (m_actionRegistry.overrides().isEmpty())
    {
        return true;
    }
    const QMap<QString, QString> previousOverrides = m_actionRegistry.overrides();
    static_cast<void>(m_actionRegistry.resetAllShortcuts());
    config::ApplicationSettings updated = m_settings;
    updated.shortcutOverrides.clear();
    if (!m_settingsStore.save(updated))
    {
        m_actionRegistry.setOverrides(previousOverrides);
        return false;
    }
    m_settings = std::move(updated);
    emit actionRegistryChanged();
    return true;
}

void AppController::applyTerminalHistoryTaskResult(const QString &tabId, const quint64 requestId,
                                                   ShellHistoryEntries entries, const QString &error)
{
    TerminalTab *target = findTab(tabId);
    if (target == nullptr || target->historyRequestId != requestId)
    {
        return;
    }
    if (!error.isEmpty())
    {
        target->history.clear();
        target->historyState = QStringLiteral("error");
        target->historyError = error;
    }
    else
    {
        target->history = std::move(entries);
        target->historyState = QStringLiteral("ready");
        target->historyError.clear();
    }
    emit terminalHistoryChanged();
}

bool AppController::connectPrivateKey(const QString &host, const int port, const QString &username,
                                      const QString &privateKeyPath, const QString &passphrase)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || privateKeyPath.trimmed().isEmpty() || port <= 0
        || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("Complete the SSH host, port, username, and private-key fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = privateKeyPath.trimmed(),
        .secret = security::SensitiveByteArray(passphrase.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::connectPassword(const QString &host, const int port, const QString &username,
                                    const QString &password)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || password.isEmpty() || port <= 0 || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("Complete the SSH host, port, username, and password fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::Password,
        .privateKeyPath = {},
        .secret = security::SensitiveByteArray(password.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::startSshConnection(ssh::SshConnectionRequest request, QString sourceProfileId)
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(tr("The maximum of 32 terminal tabs is already open"));
        }
        return false;
    }

    const std::string previousActiveWorkspaceId = m_workspaceState.activeTerminalWorkspaceId;
    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->workspaceId = tab->id;
    tab->paneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString fallbackTitle = QStringLiteral("%1@%2").arg(request.username, request.host);
    const std::string profileId = utf8String(sourceProfileId.trimmed());
    const auto sourceProfile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    const QString profileName =
        sourceProfile == m_profiles.end() ? QString{} : utf8QString(sourceProfile->name).trimmed();
    tab->title = profileName.isEmpty() ? fallbackTitle : profileName;
    tab->identity = QStringLiteral("%1@%2:%3").arg(request.username, request.host).arg(request.port);
    tab->address = request.host;
    tab->status = tr("Starting SSH connection...");
    tab->kind = TerminalTabKind::Ssh;
    tab->sourceProfileId = std::move(sourceProfileId);
    if (sourceProfile != m_profiles.end())
    {
        tab->keywordHighlightRules = sourceProfile->keywordHighlightRules;
        tab->keywordHighlightEnabled = sourceProfile->keywordHighlightEnabled;
    }
    applyWorkspaceState(*tab);
    tab->sshPhase = ssh::SshConnectionPhase::Resolving;
    tab->ssh = std::make_unique<ssh::SshTerminalSession>();
    const QString tabId = tab->id;
    initializeSessionLog(*tab);
    initializeTerminalOutputSink(*tab);
    auto workspace = workbench::makeSinglePaneTerminalWorkspace(
        utf8String(tab->workspaceId), utf8String(tab->paneId),
        {.id = utf8String(tab->id),
         .profileId = utf8String(tab->sourceProfileId),
         .title = utf8String(tab->title),
         .kind = tab->sourceProfileId.isEmpty() ? workbench::TerminalRestoreKind::Transient
                                                : workbench::TerminalRestoreKind::SshProfile});
    workspace.title = utf8String(tab->title);
    connectSshTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    m_workspaceState.terminalWorkspaces.push_back(std::move(workspace));
    m_workspaceState.activeTerminalWorkspaceId = utf8String(tabId);
    if (!persistTerminalWorkspaces())
    {
        m_workspaceState.terminalWorkspaces.pop_back();
        m_workspaceState.activeTerminalWorkspaceId = previousActiveWorkspaceId;
        m_tabs.pop_back();
        return false;
    }
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->ssh)
    {
        return false;
    }
    const std::error_code error = created->ssh->start(std::move(request), {.columns = 100, .rows = 30});
    if (error)
    {
        closeTerminalTab(tabId);
        return false;
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return true;
}

void AppController::scheduleSshReconnect(TerminalTab &tab, const ssh::SshFailureKind failure)
{
    if (tab.reconnectPending || tab.sourceProfileId.isEmpty())
    {
        return;
    }
    const auto profile = std::ranges::find(m_profiles, utf8String(tab.sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || !ssh::shouldReconnectAfter(profile->sessionOptions.reconnectPolicy, failure))
    {
        return;
    }

    constexpr qint64 stableConnectionMilliseconds = 30'000;
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (tab.connectedUtcMs > 0 && now - tab.connectedUtcMs >= stableConnectionMilliseconds)
    {
        tab.reconnectAttempt = 0;
    }
    if (tab.reconnectAttempt >= profile->sessionOptions.reconnectMaximumAttempts)
    {
        tab.status = tr("Automatic reconnect stopped after %1 attempt(s).")
                         .arg(static_cast<int>(profile->sessionOptions.reconnectMaximumAttempts));
        emit terminalTabsChanged();
        return;
    }

    ++tab.reconnectAttempt;
    ++tab.reconnectGeneration;
    tab.reconnectPending = true;
    const std::uint32_t delay = ssh::reconnectBackoffMilliseconds(profile->sessionOptions, tab.reconnectAttempt);
    tab.status = tr("SSH connection lost. Reconnecting in %1 second(s) (attempt %2 of %3).")
                     .arg((delay + 999U) / 1000U)
                     .arg(static_cast<int>(tab.reconnectAttempt))
                     .arg(static_cast<int>(profile->sessionOptions.reconnectMaximumAttempts));
    const QString tabId = tab.id;
    const std::uint64_t generation = tab.reconnectGeneration;
    QTimer::singleShot(static_cast<int>(delay), this, [this, tabId, generation] {
        attemptSshReconnect(tabId, generation);
    });
    emit terminalTabsChanged();
}

void AppController::attemptSshReconnect(const QString &tabId, const std::uint64_t generation)
{
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->ssh == nullptr || !tab->reconnectPending
        || tab->reconnectGeneration != generation)
    {
        return;
    }
    tab->reconnectPending = false;
    const auto profile = std::ranges::find(m_profiles, utf8String(tab->sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        tab->status = tr("SSH reconnect stopped because the saved host no longer exists.");
        emit terminalTabsChanged();
        return;
    }
    auto request = connectionRequestForProfile(*profile);
    if (!request)
    {
        tab->status = tr("SSH reconnect needs an available saved credential.");
        emit terminalTabsChanged();
        return;
    }

    tab->sshFailure.reset();
    tab->sshPhase = ssh::SshConnectionPhase::Resolving;
    tab->status = tr("Reconnecting to SSH host...");
    emit terminalTabsChanged();
    const std::error_code error = tab->ssh->start(std::move(*request), {.columns = 100, .rows = 30});
    if (error)
    {
        tab->sshPhase = ssh::SshConnectionPhase::Failed;
        tab->sshFailure = ssh::SshFailureKind::ProtocolError;
        tab->status = tr("SSH reconnect could not start.");
        emit terminalTabsChanged();
        return;
    }
    if (const TerminalTab *started = findTab(tabId); started != nullptr)
    {
        if (ui::TerminalItem *terminal = m_terminalViewports.value(started->paneId))
        {
            terminal->requestCurrentSize();
        }
    }
}

bool AppController::reconnectTerminalTab(const QString &id)
{
    TerminalTab *tab = findTab(id);
    if (tab == nullptr)
    {
        if (const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(id); workspace != nullptr)
        {
            tab = findTabForPane(utf8QString(workspace->activePaneId));
        }
    }
    if (tab == nullptr || tab->kind != TerminalTabKind::Ssh || tab->ssh == nullptr || tab->running
        || tab->sourceProfileId.isEmpty())
    {
        return false;
    }
    const auto profile = std::ranges::find(m_profiles, utf8String(tab->sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return false;
    }
    ++tab->reconnectGeneration;
    tab->reconnectAttempt = 0;
    tab->reconnectPending = true;
    attemptSshReconnect(tab->id, tab->reconnectGeneration);
    return true;
}

bool AppController::cancelTerminalReconnect(const QString &id)
{
    TerminalTab *tab = findTab(id);
    if (tab == nullptr)
    {
        if (const workbench::TerminalWorkspaceLayout *workspace = findTerminalWorkspace(id); workspace != nullptr)
        {
            tab = findTabForPane(utf8QString(workspace->activePaneId));
        }
    }
    if (tab == nullptr || !tab->reconnectPending)
    {
        return false;
    }
    tab->reconnectPending = false;
    ++tab->reconnectGeneration;
    tab->status = tr("Automatic SSH reconnect cancelled.");
    emit terminalTabsChanged();
    return true;
}

std::expected<ssh::SshConnectionRequest, sftp::TransferCredentialError>
AppController::sftpConnectionRequest(const TerminalTab &tab)
{
    if (tab.sourceProfileId.isEmpty())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }
    const std::string profileId = utf8String(tab.sourceProfileId);
    const auto profile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }

    security::SensitiveByteArray secret;
    if (profile->credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile->credentialReference, .kind = credentialKind(*profile)});
        if (!stored)
        {
            return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                       ? sftp::TransferCredentialError::Locked
                                       : sftp::TransferCredentialError::Unavailable);
        }
        secret = std::move(*stored);
    }
    if ((profile->authentication == ssh::SshAuthenticationMethod::Password || profile->privateKeyPassphraseRequired)
        && secret.empty())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }
    security::SensitiveByteArray proxySecret;
    if (profile->proxy.credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile->proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
        if (!stored)
        {
            return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                       ? sftp::TransferCredentialError::Locked
                                       : sftp::TransferCredentialError::Unavailable);
        }
        proxySecret = std::move(*stored);
    }
    if (!profile->proxy.username.empty() && proxySecret.empty())
    {
        return std::unexpected(sftp::TransferCredentialError::Unavailable);
    }
    auto jumpHosts = storedJumpHostRequests(*profile, m_profiles, m_credentialVaults->active());
    if (!jumpHosts)
    {
        return std::unexpected(jumpHosts.error());
    }
    return ssh::SshConnectionRequest{
        .host = utf8QString(profile->host),
        .port = profile->port,
        .username = utf8QString(profile->username),
        .authentication = profile->authentication,
        .privateKeyPath = utf8QString(profile->privateKeyPath),
        .secret = std::move(secret),
        .proxy = profile->proxy,
        .proxySecret = std::move(proxySecret),
        .jumpHosts = std::move(*jumpHosts),
        .knownHostsPath = m_knownHostsPath,
        .sessionOptions = profile->sessionOptions,
    };
}

sftp::TransferRequestProvider AppController::transferRequestProvider(const QString &profileId)
{
    const std::string id = utf8String(profileId);
    const auto found = std::ranges::find(m_profiles, id, &ssh::SshProfile::id);
    if (found == m_profiles.end() || m_credentialVaults == nullptr)
    {
        return []() -> std::expected<ssh::SshConnectionRequest, sftp::TransferCredentialError> {
            return std::unexpected(sftp::TransferCredentialError::Unavailable);
        };
    }
    ssh::SshProfile profile = *found;
    std::vector<ssh::SshProfile> profiles = m_profiles;
    security::CredentialVault *vault = &m_credentialVaults->active();
    QString knownHostsPath = m_knownHostsPath;
    return [profile = std::move(profile), profiles = std::move(profiles), vault,
            knownHostsPath = std::move(
                knownHostsPath)]() noexcept -> std::expected<ssh::SshConnectionRequest, sftp::TransferCredentialError> {
        try
        {
            security::SensitiveByteArray secret;
            if (profile.credentialReference)
            {
                auto stored = vault->read({.profileId = *profile.credentialReference, .kind = credentialKind(profile)});
                if (!stored)
                {
                    return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                               ? sftp::TransferCredentialError::Locked
                                               : sftp::TransferCredentialError::Unavailable);
                }
                secret = std::move(*stored);
            }
            if ((profile.authentication == ssh::SshAuthenticationMethod::Password
                 || profile.privateKeyPassphraseRequired)
                && secret.empty())
            {
                return std::unexpected(sftp::TransferCredentialError::Unavailable);
            }
            security::SensitiveByteArray proxySecret;
            if (profile.proxy.credentialReference)
            {
                auto stored = vault->read(
                    {.profileId = *profile.proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
                if (!stored)
                {
                    return std::unexpected(stored.error() == security::CredentialVaultError::Locked
                                               ? sftp::TransferCredentialError::Locked
                                               : sftp::TransferCredentialError::Unavailable);
                }
                proxySecret = std::move(*stored);
            }
            if (!profile.proxy.username.empty() && proxySecret.empty())
            {
                return std::unexpected(sftp::TransferCredentialError::Unavailable);
            }
            auto jumpHosts = storedJumpHostRequests(profile, profiles, *vault);
            if (!jumpHosts)
            {
                return std::unexpected(jumpHosts.error());
            }
            return ssh::SshConnectionRequest{
                .host = utf8QString(profile.host),
                .port = profile.port,
                .username = utf8QString(profile.username),
                .authentication = profile.authentication,
                .privateKeyPath = utf8QString(profile.privateKeyPath),
                .secret = std::move(secret),
                .proxy = profile.proxy,
                .proxySecret = std::move(proxySecret),
                .jumpHosts = std::move(*jumpHosts),
                .knownHostsPath = knownHostsPath,
                .sessionOptions = profile.sessionOptions,
            };
        }
        catch (...)
        {
            return std::unexpected(sftp::TransferCredentialError::Unavailable);
        }
    };
}

void AppController::initializeTransferManager()
{
    m_transferManager = std::make_unique<sftp::TransferManager>();
    QObject::connect(m_transferManager.get(), &sftp::TransferManager::tasksChanged, this,
                     &AppController::applyTransferSnapshot);
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::conflictRequired, this,
        [this](const QString &taskId, const sftp::FileConflictPtr &conflict) {
            if (!conflict)
            {
                return;
            }
            if (m_transferBatchCoordinator != nullptr)
            {
                const auto policy = m_transferBatchCoordinator->automaticConflictPolicy(utf8String(taskId));
                if (policy == sftp::TransferConflictPolicy::Skip || policy == sftp::TransferConflictPolicy::Replace)
                {
                    m_transferManager->resolveConflict(taskId, policy == sftp::TransferConflictPolicy::Skip
                                                                   ? sftp::ConflictAction::Skip
                                                                   : sftp::ConflictAction::Replace);
                    return;
                }
            }
            emit transferConflictRequested(
                taskId,
                QVariantMap{
                    {QStringLiteral("sourcePath"), utf8QString(conflict->sourcePath)},
                    {QStringLiteral("destinationPath"), utf8QString(conflict->destinationPath)},
                    {QStringLiteral("destinationExists"), true},
                    {QStringLiteral("destinationDirectory"), conflict->destinationType == sftp::EntryType::Directory},
                    {QStringLiteral("sourceSize"), QVariant::fromValue<qulonglong>(conflict->sourceSize)},
                    {QStringLiteral("destinationSize"), QVariant::fromValue<qulonglong>(conflict->destinationSize)},
                    {QStringLiteral("batchChild"),
                     m_transferBatchCoordinator != nullptr
                         && m_transferBatchCoordinator->ownsChildTask(utf8String(taskId))},
                });
        });
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::hostKeyConfirmationRequired, this,
        [this](const QString &taskId, const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
            if (m_hostKeyPromptVisible)
            {
                m_transferManager->rejectHostKey(taskId);
                return;
            }
            m_hostKeyTransferTaskId = taskId;
            m_hostKeyTabId.clear();
            m_hostKeyForSftp = false;
            m_hostKeyChangedWarning = false;
            m_hostKeyEndpoint = endpoint;
            m_hostKeyAlgorithm = algorithm;
            m_hostKeyFingerprint = fingerprint;
            m_hostKeyPromptVisible = true;
            emit hostKeyPromptChanged();
        });
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::hostKeyChanged, this,
        [this](const QString &taskId, const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
            if (m_hostKeyPromptVisible)
            {
                return;
            }
            m_hostKeyTransferTaskId = taskId;
            m_hostKeyTabId.clear();
            m_hostKeyForSftp = false;
            m_hostKeyChangedWarning = true;
            m_hostKeyEndpoint = endpoint;
            m_hostKeyAlgorithm = algorithm;
            m_hostKeyFingerprint = fingerprint;
            m_hostKeyPromptVisible = true;
            emit hostKeyPromptChanged();
        });
    QObject::connect(
        m_transferManager.get(), &sftp::TransferManager::recoveryError, this, [this](const QString &errorCode) {
            emit transferNotificationRequested({
                {QStringLiteral("kind"), QStringLiteral("error")},
                {QStringLiteral("title"), tr("Transfer recovery unavailable")},
                {QStringLiteral("message"),
                 errorCode == QStringLiteral("recovery-load-failed")
                     ? tr("The previous transfer state could not be read. New transfers are still available.")
                     : tr("Transfer recovery state could not be saved. Active transfers may not be recoverable after "
                          "exit.")},
            });
        });
    m_transferManager->enableRecovery(siblingTransferRecoveryFile(m_settingsStore.filePath()),
                                      [this](const std::string &endpointId) {
                                          return transferRequestProvider(utf8QString(endpointId));
                                      });
    m_transferBatchCoordinator = std::make_unique<sftp::TransferBatchCoordinator>(*m_transferManager);
    QObject::connect(m_transferBatchCoordinator.get(), &sftp::TransferBatchCoordinator::batchesChanged, this,
                     &AppController::applyTransferBatchSnapshot);
    QObject::connect(
        m_transferBatchCoordinator.get(), &sftp::TransferBatchCoordinator::recoveryError, this,
        [this](const QString &errorCode) {
            emit transferNotificationRequested({
                {QStringLiteral("kind"), QStringLiteral("error")},
                {QStringLiteral("title"), tr("Batch transfer recovery unavailable")},
                {QStringLiteral("message"),
                 errorCode == QStringLiteral("batch-recovery-load-failed")
                     ? tr("Previous batch transfer state could not be read. New transfers remain available.")
                     : tr("Batch transfer state could not be saved. Active batches may not be recoverable "
                          "after exit.")},
            });
        });
    m_transferBatchCoordinator->enableRecovery(siblingTransferBatchRecoveryFile(m_settingsStore.filePath()));
}

void AppController::applyTransferSnapshot(const sftp::TransferTasksPtr &tasks)
{
    QHash<QString, QString> previousStatuses;
    previousStatuses.reserve(m_transferTasks.size());
    for (const QVariant &value : std::as_const(m_transferTasks))
    {
        const QVariantMap task = value.toMap();
        previousStatuses.insert(task.value(QStringLiteral("id")).toString(),
                                task.value(QStringLiteral("status")).toString());
    }

    QVariantList values;
    if (tasks)
    {
        values.reserve(static_cast<qsizetype>(tasks->size()));
        for (const sftp::TransferTask &task : *tasks)
        {
            const QVariantMap value = transferTaskValue(task);
            values.push_back(value);

            const QString taskId = value.value(QStringLiteral("id")).toString();
            const QString status = value.value(QStringLiteral("status")).toString();
            const QString previousStatus = previousStatuses.value(taskId);
            const bool terminalStatus = status == QStringLiteral("completed") || status == QStringLiteral("failed")
                                        || status == QStringLiteral("cancelled");
            if (!previousStatus.isEmpty() && previousStatus != status && terminalStatus)
            {
                const bool download = value.value(QStringLiteral("direction")).toString() == QStringLiteral("download");
                QString kind = QStringLiteral("info");
                QString title;
                if (status == QStringLiteral("completed"))
                {
                    kind = QStringLiteral("success");
                    title = download ? tr("Download complete") : tr("Upload complete");
                }
                else if (status == QStringLiteral("failed"))
                {
                    kind = QStringLiteral("error");
                    title = tr("File transfer failed");
                }
                else
                {
                    title = tr("File transfer cancelled");
                }
                emit transferNotificationRequested({
                    {QStringLiteral("taskId"), taskId},
                    {QStringLiteral("kind"), kind},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("message"), value.value(QStringLiteral("displayName")).toString()},
                });
            }
        }
    }
    m_transferTasks = std::move(values);
    emit transferTasksChanged();
}

void AppController::applyTransferBatchSnapshot(const sftp::TransferBatchesPtr &batches)
{
    QVariantList values;
    if (batches)
    {
        values.reserve(static_cast<qsizetype>(batches->size()));
        for (const sftp::TransferBatch &batch : *batches)
        {
            values.push_back(transferBatchValue(batch));
        }
    }
    m_transferBatches = std::move(values);
    emit transferTasksChanged();
}

bool AppController::startSftpSession(TerminalTab &tab)
{
    if (tab.sftpSession != nullptr)
    {
        return true;
    }
    tab.sftpModel = std::make_unique<sftp::SftpDirectoryModel>();
    tab.sftpModel->setShowHidden(m_settings.sftpShowHiddenFiles);
    tab.sftpModel->setViewMode(tab.sftpViewMode);
    tab.sftpModel->setSortColumn(tab.sftpSortColumn);
    tab.sftpModel->setSortAscending(tab.sftpSortAscending);
    tab.sftpModel->setDirectoriesFirst(tab.sftpDirectoriesFirst);
    const QString tabId = tab.id;
    QObject::connect(tab.sftpModel.get(), &sftp::SftpDirectoryModel::treeDirectoryRequested, this,
                     [this, tabId](const QString &remotePath) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpSession == nullptr)
                         {
                             return;
                         }
                         updated->sftpSession->requestTreeDirectory(++updated->sftpTreeRequestId,
                                                                    updated->sftpGeneration, remotePath);
                     });
    auto request = sftpConnectionRequest(tab);
    if (!request)
    {
        tab.sftpState = QStringLiteral("error");
        tab.sftpError = request.error() == sftp::TransferCredentialError::Locked
                            ? tr("Unlock the credential vault to browse remote files.")
                            : tr("This SSH session needs a saved credential before remote files can be opened.");
        emit sftpChanged();
        return false;
    }

    tab.sftpSession = std::make_unique<sftp::SftpSession>();
    tab.sftpSession->setFilenameEncoding(tab.sftpFilenameEncoding);
    connectSftpTabSignals(tab);
    tab.sftpState = QStringLiteral("connecting");
    tab.sftpError.clear();
    emit sftpChanged();
    const std::error_code error = tab.sftpSession->start(std::move(*request));
    if (error)
    {
        tab.sftpSession.reset();
        tab.sftpState = QStringLiteral("error");
        tab.sftpError = tr("The SFTP session could not be started.");
        emit sftpChanged();
        return false;
    }
    return true;
}

void AppController::stopSftpSession(TerminalTab &tab)
{
    if (tab.sftpSession != nullptr)
    {
        deferSftpSessionStop(std::move(tab.sftpSession));
    }
    tab.sftpModel.reset();
    tab.sftpState = QStringLiteral("idle");
    tab.sftpError.clear();
    tab.sftpHomePath.clear();
    tab.sftpHasListing = false;
    ++tab.sftpGeneration;
}

void AppController::deferSftpSessionStop(std::unique_ptr<sftp::SftpSession> session)
{
    if (session == nullptr)
    {
        return;
    }

    sftp::SftpSession *sessionPointer = session.get();
    QObject::connect(sessionPointer, &sftp::SftpSession::workerFinishedChanged, this, [this, sessionPointer] {
        reapStoppedSftpSession(sessionPointer);
    });
    m_stoppingSftpSessions.push_back(std::move(session));
    sessionPointer->requestStop();
    if (sessionPointer->workerFinished())
    {
        reapStoppedSftpSession(sessionPointer);
    }
}

void AppController::reapStoppedSftpSession(sftp::SftpSession *session)
{
    QTimer::singleShot(0, this, [this, session] {
        const auto position =
            std::ranges::find(m_stoppingSftpSessions, session, [](const std::unique_ptr<sftp::SftpSession> &candidate) {
                return candidate.get();
            });
        if (position != m_stoppingSftpSessions.end() && (*position)->workerFinished())
        {
            m_stoppingSftpSessions.erase(position);
        }
    });
}

void AppController::requestSftpDirectory(TerminalTab &tab, const QString &remotePath)
{
    if (tab.sftpSession == nullptr)
    {
        return;
    }
    tab.sftpRequestedPath = remotePath;
    tab.sftpState = QStringLiteral("loading");
    tab.sftpError.clear();
    const std::uint64_t requestId = ++tab.sftpRequestId;
    const std::uint64_t generation = ++tab.sftpGeneration;
    emit sftpChanged();
    tab.sftpSession->requestDirectory(requestId, generation, remotePath);
}

bool AppController::saveHostProfile(const QString &id, const QString &name, const QString &host, const int port,
                                    const QString &username, const QString &authentication,
                                    const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                    const QString &group)
{
    return saveHostProfileInternal(id, name, host, port, username, authentication, privateKeyPath,
                                   privateKeyPassphraseRequired, group, {}, false, false);
}

bool AppController::saveHostProfileWithCredential(const QString &id, const QString &name, const QString &host,
                                                  const int port, const QString &username,
                                                  const QString &authentication, const QString &privateKeyPath,
                                                  const bool privateKeyPassphraseRequired, const QString &group,
                                                  const QString &secret, const bool rememberCredential,
                                                  const QVariantMap &sessionOptions, const QVariantMap &proxyOptions,
                                                  const QString &proxySecret, const bool rememberProxyCredential,
                                                  const QVariantMap &routeOptions)
{
    return saveHostProfileInternal(id, name, host, port, username, authentication, privateKeyPath,
                                   privateKeyPassphraseRequired, group, secret, rememberCredential, true,
                                   sessionOptions, proxyOptions, proxySecret, rememberProxyCredential, true,
                                   routeOptions);
}

bool AppController::saveAndConnectHostProfile(const QString &id, const QString &name, const QString &host,
                                              const int port, const QString &username, const QString &authentication,
                                              const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                              const QString &group, const QString &secret,
                                              const bool rememberCredential, const QVariantMap &sessionOptions,
                                              const QVariantMap &proxyOptions, const QString &proxySecret,
                                              const bool rememberProxyCredential, const QVariantMap &routeOptions)
{
    const QString profileId =
        id.trimmed().isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    if (!saveHostProfileInternal(profileId, name, host, port, username, authentication, privateKeyPath,
                                 privateKeyPassphraseRequired, group, secret, rememberCredential, true, sessionOptions,
                                 proxyOptions, proxySecret, rememberProxyCredential, true, routeOptions))
    {
        return false;
    }
    const auto profile = std::ranges::find(m_profiles, utf8String(profileId), &ssh::SshProfile::id);
    if (profile != m_profiles.end())
    {
        auto request = connectionRequestForProfile(*profile, rememberCredential ? QString{} : secret,
                                                   rememberProxyCredential ? QString{} : proxySecret);
        if (request && startSshConnection(std::move(*request), profileId))
        {
            return true;
        }
    }
    if (m_credentialOperationError.isEmpty())
    {
        setCredentialOperationError(tr("The profile was saved, but the connection could not be started."));
    }
    return false;
}

bool AppController::saveHostProfileInternal(const QString &id, const QString &name, const QString &host, const int port,
                                            const QString &username, const QString &authentication,
                                            const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                            const QString &group, const QString &secret, const bool rememberCredential,
                                            const bool manageCredential, const QVariantMap &sessionOptions,
                                            const QVariantMap &proxyOptions, const QString &proxySecret,
                                            const bool rememberProxyCredential, const bool manageProxyCredential,
                                            const QVariantMap &routeOptions)
{
    const QString normalizedHost = host.trimmed();
    const QString normalizedName = name.trimmed().isEmpty() ? normalizedHost : name.trimmed();
    const QString normalizedGroup = group.trimmed();
    const QString normalizedUsername = username.trimmed();
    const QString normalizedPrivateKeyPath = privateKeyPath.trimmed();
    const QString profileId = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    const std::optional<ssh::SshAuthenticationMethod> authenticationMethod = parseAuthenticationToken(authentication);
    if (!authenticationMethod)
    {
        return false;
    }
    const bool supportsCredential = *authenticationMethod != ssh::SshAuthenticationMethod::Agent;
    const bool effectiveRememberCredential = rememberCredential && supportsCredential;

    const std::string storedProfileId = utf8String(profileId);
    const auto storedProfile = std::ranges::find(m_profiles, storedProfileId, &ssh::SshProfile::id);
    ssh::SshSessionOptions resolvedSessionOptions =
        storedProfile == m_profiles.end() ? ssh::SshSessionOptions{} : storedProfile->sessionOptions;
    if (!sessionOptions.isEmpty())
    {
        auto merged = mergeSessionOptions(sessionOptions, std::move(resolvedSessionOptions));
        if (!merged)
        {
            return false;
        }
        resolvedSessionOptions = std::move(*merged);
    }
    ssh::SshProxyOptions resolvedProxy =
        storedProfile == m_profiles.end() ? ssh::SshProxyOptions{} : storedProfile->proxy;
    if (!proxyOptions.isEmpty())
    {
        auto merged = mergeProxyOptions(proxyOptions, std::move(resolvedProxy));
        if (!merged)
        {
            return false;
        }
        resolvedProxy = std::move(*merged);
    }
    auto resolvedJumpProfileIds = mergeJumpProfileIds(
        routeOptions, storedProfile == m_profiles.end() ? std::vector<std::string>{} : storedProfile->jumpProfileIds,
        storedProfileId, m_profiles);
    if (!resolvedJumpProfileIds)
    {
        setCredentialOperationError(tr("Select up to three distinct saved jump hosts."));
        return false;
    }

    ssh::SshProfile profile{
        .id = storedProfileId,
        .name = utf8String(normalizedName),
        .group = utf8String(normalizedGroup),
        .host = utf8String(normalizedHost),
        .port = port > 0 && port <= 65535 ? static_cast<std::uint16_t>(port) : std::uint16_t{0},
        .username = utf8String(normalizedUsername),
        .authentication = *authenticationMethod,
        .privateKeyPath = *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey
                              ? utf8String(normalizedPrivateKeyPath)
                              : std::string{},
        .privateKeyPassphraseRequired =
            *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && privateKeyPassphraseRequired,
        .sessionOptions = std::move(resolvedSessionOptions),
        .proxy = std::move(resolvedProxy),
        .jumpProfileIds = std::move(*resolvedJumpProfileIds),
    };
    if (!ssh::validSshProfile(profile))
    {
        return false;
    }

    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto existing = std::ranges::find(updated, profile.id, &ssh::SshProfile::id);
    std::optional<security::CredentialKey> previousKey;
    std::optional<security::CredentialKey> previousProxyKey;
    if (existing == updated.end())
    {
        updated.push_back(profile);
    }
    else
    {
        profile.lastConnectedUtcMs = existing->lastConnectedUtcMs;
        if (existing->credentialReference)
        {
            previousKey =
                security::CredentialKey{.profileId = *existing->credentialReference, .kind = credentialKind(*existing)};
        }
        if (existing->proxy.credentialReference)
        {
            previousProxyKey = security::CredentialKey{.profileId = *existing->proxy.credentialReference,
                                                       .kind = security::CredentialKind::ProxyPassword};
        }
        if ((!manageCredential && existing->authentication == profile.authentication)
            || (effectiveRememberCredential && secret.isEmpty() && existing->authentication == profile.authentication))
        {
            profile.credentialReference = existing->credentialReference;
        }
        const bool sameProxyIdentity =
            existing->proxy.type == profile.proxy.type && existing->proxy.host == profile.proxy.host
            && existing->proxy.port == profile.proxy.port && existing->proxy.username == profile.proxy.username;
        if (!sameProxyIdentity)
        {
            profile.proxy.credentialReference.reset();
        }
        else if (!manageProxyCredential
                 || (rememberProxyCredential && proxySecret.isEmpty() && !profile.proxy.username.empty()))
        {
            profile.proxy.credentialReference = existing->proxy.credentialReference;
        }
        *existing = profile;
    }

    const security::CredentialKey desiredKey{.profileId = profile.id, .kind = credentialKind(profile)};
    const bool shouldStore = manageCredential && effectiveRememberCredential && !secret.isEmpty();
    const bool effectiveRememberProxyCredential =
        rememberProxyCredential && profile.proxy.type != ssh::SshProxyType::None && !profile.proxy.username.empty();
    const security::CredentialKey desiredProxyKey{.profileId = profile.id,
                                                  .kind = security::CredentialKind::ProxyPassword};
    const bool shouldStoreProxy = manageProxyCredential && effectiveRememberProxyCredential && !proxySecret.isEmpty();
    if ((shouldStore || shouldStoreProxy) && m_credentialVaults->storage() == security::CredentialStorage::Portable
        && !m_credentialVaults->portableInitialized())
    {
        setCredentialOperationError(
            tr("Create the portable credential vault in Settings > Security before saving a secret."));
        return false;
    }
    const bool shouldRemovePrevious = manageCredential && previousKey
                                      && (!effectiveRememberCredential || (shouldStore && *previousKey != desiredKey)
                                          || (effectiveRememberCredential && secret.isEmpty()
                                              && profile.credentialReference != previousKey->profileId));
    const bool shouldRemovePreviousProxy =
        manageProxyCredential && previousProxyKey
        && (!effectiveRememberProxyCredential || (shouldStoreProxy && *previousProxyKey != desiredProxyKey)
            || (effectiveRememberProxyCredential && proxySecret.isEmpty()
                && profile.proxy.credentialReference != previousProxyKey->profileId));
    if (shouldStore)
    {
        profile.credentialReference = profile.id;
    }
    else if (manageCredential && !effectiveRememberCredential)
    {
        profile.credentialReference.reset();
    }
    if (shouldStoreProxy)
    {
        profile.proxy.credentialReference = profile.id;
    }
    else if (manageProxyCredential && !effectiveRememberProxyCredential)
    {
        profile.proxy.credentialReference.reset();
    }
    const auto target = std::ranges::find(updated, profile.id, &ssh::SshProfile::id);
    Q_ASSERT(target != updated.end());
    *target = profile;

    CredentialMutation targetCredentialMutation(m_credentialVaults->active());
    auto targetMutation = targetCredentialMutation.apply(desiredKey, previousKey, shouldStore, shouldRemovePrevious,
                                                         shouldStore ? security::SensitiveByteArray(secret.toUtf8())
                                                                     : security::SensitiveByteArray{});
    if (!targetMutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(targetMutation.error()));
        return false;
    }
    CredentialMutation proxyCredentialMutation(m_credentialVaults->active());
    auto proxyMutation = proxyCredentialMutation.apply(
        desiredProxyKey, previousProxyKey, shouldStoreProxy, shouldRemovePreviousProxy,
        shouldStoreProxy ? security::SensitiveByteArray(proxySecret.toUtf8()) : security::SensitiveByteArray{});
    if (!proxyMutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(proxyMutation.error()));
        return false;
    }

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles";
        return false;
    }
    targetCredentialMutation.commit();
    proxyCredentialMutation.commit();
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::duplicateHostProfile(const QString &id)
{
    const auto source = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (source == m_profiles.end())
    {
        return false;
    }

    const QString baseName = utf8QString(source->name) + QStringLiteral(" copy");
    QString copyName = baseName;
    for (int suffix = 2;
         std::ranges::any_of(m_profiles,
                             [&copyName](const ssh::SshProfile &profile) {
                                 return utf8QString(profile.name).compare(copyName, Qt::CaseInsensitive) == 0;
                             });
         ++suffix)
    {
        copyName = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
    }

    ssh::SshProfile copy = *source;
    copy.id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    copy.name = utf8String(copyName);
    copy.credentialReference.reset();
    copy.proxy.credentialReference.reset();
    copy.lastConnectedUtcMs.reset();
    std::vector<ssh::SshProfile> updated = m_profiles;
    updated.push_back(std::move(copy));
    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist duplicated SSH profile";
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::deleteHostProfile(const QString &id)
{
    const std::string profileId = utf8String(id);
    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto profile = std::ranges::find(updated, profileId, &ssh::SshProfile::id);
    if (profile == updated.end())
    {
        return false;
    }
    const auto dependent = std::ranges::find_if(updated, [&profileId](const ssh::SshProfile &candidate) {
        return std::ranges::find(candidate.jumpProfileIds, profileId) != candidate.jumpProfileIds.end();
    });
    if (dependent != updated.end())
    {
        setCredentialOperationError(tr("Remove this host from the jump-host chain of \"%1\" before deleting it.")
                                        .arg(utf8QString(dependent->name)));
        return false;
    }
    if (forwarding::portForwardingRulesReferenceProfile(m_portForwardingRules, profileId))
    {
        setCredentialOperationError(tr("Delete the port forwarding rules that use this host before deleting it."));
        return false;
    }
    const std::optional<security::CredentialKey> key =
        profile->credentialReference ? std::optional{security::CredentialKey{.profileId = *profile->credentialReference,
                                                                             .kind = credentialKind(*profile)}}
                                     : std::nullopt;
    const std::optional<security::CredentialKey> proxyKey =
        profile->proxy.credentialReference
            ? std::optional{security::CredentialKey{.profileId = *profile->proxy.credentialReference,
                                                    .kind = security::CredentialKind::ProxyPassword}}
            : std::nullopt;
    CredentialMutation credentialMutation(m_credentialVaults->active());
    const security::CredentialKey unusedTargetKey{.profileId = profile->id, .kind = credentialKind(*profile)};
    auto mutation = credentialMutation.apply(unusedTargetKey, key, false, key.has_value(), {});
    if (!mutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(mutation.error()));
        return false;
    }
    CredentialMutation proxyCredentialMutation(m_credentialVaults->active());
    const security::CredentialKey unusedProxyKey{.profileId = profile->id,
                                                 .kind = security::CredentialKind::ProxyPassword};
    mutation = proxyCredentialMutation.apply(unusedProxyKey, proxyKey, false, proxyKey.has_value(), {});
    if (!mutation)
    {
        setCredentialOperationError(credentialVaultErrorMessage(mutation.error()));
        return false;
    }
    updated.erase(profile);

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles after deletion";
        return false;
    }
    credentialMutation.commit();
    proxyCredentialMutation.commit();
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::clearRecentHostProfiles()
{
    std::vector<ssh::SshProfile> updated = m_profiles;
    bool changed = false;
    for (ssh::SshProfile &profile : updated)
    {
        changed = profile.lastConnectedUtcMs.has_value() || changed;
        profile.lastConnectedUtcMs.reset();
    }
    if (!changed)
    {
        return true;
    }
    if (!m_profileStore.save(updated))
    {
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::setHostSectionCollapsed(const QString &sectionId, const bool collapsed)
{
    const std::string section = utf8String(sectionId.trimmed());
    if (section.empty() || section.size() > 512 || section.find('\0') != std::string::npos)
    {
        return false;
    }

    workbench::WorkspaceState candidate = m_workspaceState;
    const auto existing = std::ranges::find(candidate.collapsedHostSections, section);
    if (collapsed)
    {
        if (existing != candidate.collapsedHostSections.end())
        {
            return true;
        }
        candidate.collapsedHostSections.push_back(section);
    }
    else
    {
        if (existing == candidate.collapsedHostSections.end())
        {
            return true;
        }
        candidate.collapsedHostSections.erase(existing);
    }
    if (!saveWorkspaceStateCandidate(candidate))
    {
        return false;
    }
    m_workspaceState = std::move(candidate);
    emit hostWorkspaceChanged();
    return true;
}

bool AppController::forgetHostCredential(const QString &id)
{
    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto profile = std::ranges::find(updated, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == updated.end() || !profile->credentialReference)
    {
        return false;
    }
    const security::CredentialKey key{.profileId = *profile->credentialReference, .kind = credentialKind(*profile)};
    auto current = m_credentialVaults->active().read(key);
    std::optional<security::SensitiveByteArray> removedSecret;
    if (current)
    {
        removedSecret = std::move(*current);
    }
    else if (current.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(current.error()));
        return false;
    }
    const auto removed = m_credentialVaults->active().remove(key);
    if (!removed && removed.error() != security::CredentialVaultError::NotFound)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    profile->credentialReference.reset();
    if (!m_profileStore.save(updated))
    {
        if (removedSecret)
        {
            const std::string_view bytes = removedSecret->view();
            logCredentialRollbackResult(
                m_credentialVaults->active().store(
                    key, security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                "forgotten credential restore");
        }
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    return true;
}

bool AppController::saveHostCredential(const QString &id, const QString &secret)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || profile->authentication == ssh::SshAuthenticationMethod::Agent
        || secret.isEmpty())
    {
        return false;
    }
    return saveHostProfileWithCredential(id, utf8QString(profile->name), utf8QString(profile->host), profile->port,
                                         utf8QString(profile->username), authenticationToken(profile->authentication),
                                         utf8QString(profile->privateKeyPath), profile->privateKeyPassphraseRequired,
                                         utf8QString(profile->group), secret, true);
}

bool AppController::saveProxyCredential(const QString &id, const QString &secret)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || profile->proxy.type == ssh::SshProxyType::None || profile->proxy.username.empty()
        || secret.isEmpty())
    {
        return false;
    }
    return saveHostProfileWithCredential(id, utf8QString(profile->name), utf8QString(profile->host), profile->port,
                                         utf8QString(profile->username), authenticationToken(profile->authentication),
                                         utf8QString(profile->privateKeyPath), profile->privateKeyPassphraseRequired,
                                         utf8QString(profile->group), {}, profile->credentialReference.has_value(),
                                         sessionOptionsVariantMap(profile->sessionOptions),
                                         proxyOptionsVariantMap(profile->proxy, false), secret, true);
}

QString AppController::readHostCredential(const QString &id)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || !profile->credentialReference)
    {
        setCredentialOperationError(tr("This host profile has no saved credential."));
        return {};
    }

    const security::CredentialKey key{.profileId = *profile->credentialReference, .kind = credentialKind(*profile)};
    auto secret = m_credentialVaults->active().read(key);
    if (!secret)
    {
        setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
        return {};
    }

    const std::string_view bytes = secret->view();
    QString result = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
    setCredentialOperationError({});
    return result;
}

QString AppController::readProxyCredential(const QString &id)
{
    const auto profile = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (profile == m_profiles.end() || !profile->proxy.credentialReference)
    {
        setCredentialOperationError(tr("This host profile has no saved proxy credential."));
        return {};
    }

    auto secret = m_credentialVaults->active().read(
        {.profileId = *profile->proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
    if (!secret)
    {
        setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
        return {};
    }

    const std::string_view bytes = secret->view();
    QString result = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
    setCredentialOperationError({});
    return result;
}

std::optional<ssh::SshConnectionRequest> AppController::connectionRequestForProfile(const ssh::SshProfile &profile,
                                                                                    const QString &secret,
                                                                                    const QString &proxySecret)
{
    security::SensitiveByteArray connectionSecret = profile.authentication == ssh::SshAuthenticationMethod::Agent
                                                        ? security::SensitiveByteArray{}
                                                        : security::SensitiveByteArray(secret.toUtf8());
    if (connectionSecret.empty() && profile.credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile.credentialReference, .kind = credentialKind(profile)});
        if (!stored)
        {
            setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
            return std::nullopt;
        }
        connectionSecret = std::move(*stored);
    }
    if ((profile.authentication == ssh::SshAuthenticationMethod::Password || profile.privateKeyPassphraseRequired)
        && connectionSecret.empty())
    {
        setCredentialOperationError(tr("Enter the credential required by this host."));
        return std::nullopt;
    }
    security::SensitiveByteArray connectionProxySecret(proxySecret.toUtf8());
    if (!profile.proxy.username.empty() && connectionProxySecret.empty() && profile.proxy.credentialReference)
    {
        auto stored = m_credentialVaults->active().read(
            {.profileId = *profile.proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
        if (!stored)
        {
            setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
            return std::nullopt;
        }
        connectionProxySecret = std::move(*stored);
    }
    if (!profile.proxy.username.empty() && connectionProxySecret.empty())
    {
        setCredentialOperationError(tr("Enter the credential required by this proxy."));
        return std::nullopt;
    }
    std::vector<ssh::SshJumpHostRequest> jumpHosts;
    jumpHosts.reserve(profile.jumpProfileIds.size());
    for (const std::string &jumpProfileId : profile.jumpProfileIds)
    {
        const auto jump = std::ranges::find(m_profiles, jumpProfileId, &ssh::SshProfile::id);
        if (jump == m_profiles.end())
        {
            setCredentialOperationError(tr("A configured jump host no longer exists."));
            return std::nullopt;
        }

        security::SensitiveByteArray jumpSecret;
        if (jump->credentialReference)
        {
            auto stored = m_credentialVaults->active().read(
                {.profileId = *jump->credentialReference, .kind = credentialKind(*jump)});
            if (!stored)
            {
                setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
                return std::nullopt;
            }
            jumpSecret = std::move(*stored);
        }
        if (profileRequiresCredential(*jump) && jumpSecret.empty())
        {
            setCredentialOperationError(
                tr("Save the credential for jump host \"%1\" before using this chain.").arg(utf8QString(jump->name)));
            return std::nullopt;
        }

        security::SensitiveByteArray jumpProxySecret;
        if (jump->proxy.credentialReference)
        {
            auto stored = m_credentialVaults->active().read(
                {.profileId = *jump->proxy.credentialReference, .kind = security::CredentialKind::ProxyPassword});
            if (!stored)
            {
                setCredentialOperationError(credentialVaultErrorMessage(stored.error()));
                return std::nullopt;
            }
            jumpProxySecret = std::move(*stored);
        }
        if (proxyRequiresCredential(*jump) && jumpProxySecret.empty())
        {
            setCredentialOperationError(tr("Save the proxy credential for jump host \"%1\" before using this chain.")
                                            .arg(utf8QString(jump->name)));
            return std::nullopt;
        }

        jumpHosts.push_back({
            .profileId = utf8QString(jump->id),
            .displayName = utf8QString(jump->name),
            .host = utf8QString(jump->host),
            .port = jump->port,
            .username = utf8QString(jump->username),
            .authentication = jump->authentication,
            .privateKeyPath = utf8QString(jump->privateKeyPath),
            .secret = std::move(jumpSecret),
            .proxy = jump->proxy,
            .proxySecret = std::move(jumpProxySecret),
        });
    }
    return ssh::SshConnectionRequest{
        .host = utf8QString(profile.host),
        .port = profile.port,
        .username = utf8QString(profile.username),
        .authentication = profile.authentication,
        .privateKeyPath = utf8QString(profile.privateKeyPath),
        .secret = std::move(connectionSecret),
        .proxy = profile.proxy,
        .proxySecret = std::move(connectionProxySecret),
        .jumpHosts = std::move(jumpHosts),
        .knownHostsPath = m_knownHostsPath,
        .sessionOptions = profile.sessionOptions,
    };
}

bool AppController::connectHostProfile(const QString &id, const QString &secret, const QString &proxySecret)
{
    const std::string profileId = utf8String(id);
    const auto profile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return false;
    }
    auto request = connectionRequestForProfile(*profile, secret, proxySecret);
    if (!request)
    {
        return false;
    }
    setCredentialOperationError({});
    return startSshConnection(std::move(*request), id);
}

QVariantMap AppController::parseQuickConnectTarget(const QString &target) const
{
    const auto parsed = ssh::parseSshTarget(utf8String(target));
    if (!parsed)
    {
        return {
            {QStringLiteral("valid"), false},
            {QStringLiteral("error"), quickConnectError(parsed.error())},
        };
    }
    return {
        {QStringLiteral("valid"), true},
        {QStringLiteral("error"), QString{}},
        {QStringLiteral("username"), utf8QString(parsed->username)},
        {QStringLiteral("host"), utf8QString(parsed->host)},
        {QStringLiteral("port"), parsed->port},
    };
}

bool AppController::connectQuick(const QString &target, const QString &authentication, const QString &privateKeyPath,
                                 const bool privateKeyPassphraseRequired, const QString &secret,
                                 const bool shouldSaveProfile, const QString &profileName, const QString &group)
{
    const auto parsed = ssh::parseSshTarget(utf8String(target));
    const auto authenticationMethod = parseAuthenticationToken(authentication);
    if (!parsed || !authenticationMethod)
    {
        return false;
    }
    const QString host = utf8QString(parsed->host);
    const QString username = utf8QString(parsed->username);
    const QString normalizedKeyPath = privateKeyPath.trimmed();
    if ((*authenticationMethod == ssh::SshAuthenticationMethod::Password && secret.isEmpty())
        || (*authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && normalizedKeyPath.isEmpty())
        || (*authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && privateKeyPassphraseRequired
            && secret.isEmpty()))
    {
        return false;
    }

    if (shouldSaveProfile)
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!saveHostProfileWithCredential(id, profileName, host, parsed->port, username, authentication,
                                           normalizedKeyPath, privateKeyPassphraseRequired, group, secret, true))
        {
            return false;
        }
        return connectHostProfile(id, {});
    }

    ssh::SshConnectionRequest request{
        .host = host,
        .port = parsed->port,
        .username = username,
        .authentication = *authenticationMethod,
        .privateKeyPath =
            *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey ? normalizedKeyPath : QString{},
        .secret = *authenticationMethod == ssh::SshAuthenticationMethod::Agent
                      ? security::SensitiveByteArray{}
                      : security::SensitiveByteArray(secret.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::saveApplicationSettings(const QString &theme, const qreal backdropOpacity, const QString &backdrop,
                                            const QString &accent, const QString &customAccent,
                                            const QString &uiFontFamily, const QString &fontFamily, const int fontSize,
                                            const bool showAllFonts, const bool ligatures,
                                            const qreal terminalBackgroundOpacity, const QString &cursor,
                                            const bool cursorShouldBlink, const bool shouldCopyOnSelect,
                                            const bool shouldConfirmMultilinePaste, const QString &language,
                                            const bool shouldShowHiddenSftpFiles, const bool shouldConfirmSftpDelete)
{
    const auto parsedTheme = config::parseThemePreference(theme);
    const auto parsedBackdrop = config::parseBackdropPreference(backdrop);
    const auto parsedAccent = config::parseAccentPreference(accent);
    const auto parsedCursor = config::parseCursorPreference(cursor);
    const auto parsedLanguage = config::parseLanguagePreference(language);
    if (!parsedTheme || !parsedBackdrop || !parsedAccent || !parsedCursor || !parsedLanguage)
    {
        return false;
    }

    return persistApplicationSettings({
        .theme = *parsedTheme,
        .backdropOpacity = backdropOpacity,
        .backdrop = *parsedBackdrop,
        .accent = *parsedAccent,
        .customAccent = customAccent.trimmed().toUpper(),
        .uiFontFamily = uiFontFamily,
        .terminalFontFamily = fontFamily,
        .terminalFontSize = fontSize,
        .showAllTerminalFonts = showAllFonts,
        .terminalLigatures = ligatures,
        .terminalBackgroundOpacity = terminalBackgroundOpacity,
        .cursor = *parsedCursor,
        .cursorBlink = cursorShouldBlink,
        .copyOnSelect = shouldCopyOnSelect,
        .confirmMultilinePaste = shouldConfirmMultilinePaste,
        .sftpShowHiddenFiles = shouldShowHiddenSftpFiles,
        .sftpConfirmDelete = shouldConfirmSftpDelete,
        .credentialStorage = m_settings.credentialStorage,
        .language = *parsedLanguage,
    });
}

bool AppController::resetApplicationSettings()
{
    config::ApplicationSettings defaults;
    defaults.credentialStorage = m_settings.credentialStorage;
    if (!persistApplicationSettings(defaults))
    {
        return false;
    }
    m_actionRegistry.setOverrides(m_settings.shortcutOverrides);
    emit actionRegistryChanged();
    return true;
}

bool AppController::initializePortableCredentialVault(const QString &masterPassword)
{
    auto initialized = m_credentialVaults->initializePortable(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!initialized)
    {
        setCredentialOperationError(credentialVaultErrorMessage(initialized.error()));
        return false;
    }
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
    {
        if (rule.autoStart && findPortForwardingRuntime(rule.id) == nullptr)
        {
            (void)startPortForwardingRule(utf8QString(rule.id));
        }
    }
    return true;
}

bool AppController::unlockPortableCredentialVault(const QString &masterPassword)
{
    auto unlocked = m_credentialVaults->unlockPortable(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!unlocked)
    {
        setCredentialOperationError(credentialVaultErrorMessage(unlocked.error()));
        return false;
    }
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
    {
        if (rule.autoStart && findPortForwardingRuntime(rule.id) == nullptr)
        {
            (void)startPortForwardingRule(utf8QString(rule.id));
        }
    }
    return true;
}

bool AppController::changePortableVaultMasterPassword(const QString &masterPassword)
{
    auto changed =
        m_credentialVaults->changePortableMasterPassword(security::SensitiveByteArray(masterPassword.toUtf8()));
    if (!changed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(changed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

void AppController::lockPortableCredentialVault()
{
    m_credentialVaults->lockPortable();
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
}

bool AppController::migrateCredentialStorage(const QString &target, const bool removeSource)
{
    const auto parsedTarget = parseCredentialStorage(target);
    if (!parsedTarget)
    {
        setCredentialOperationError(tr("Choose system, portable, or session credential storage."));
        return false;
    }
    const security::CredentialStorage previousStorage = m_credentialVaults->storage();
    if (*parsedTarget == previousStorage)
    {
        setCredentialOperationError({});
        return true;
    }
    auto migrated = m_credentialVaults->migrate(*parsedTarget, false);
    if (!migrated)
    {
        setCredentialOperationError(credentialVaultErrorMessage(migrated.error()));
        return false;
    }

    config::ApplicationSettings updatedSettings = m_settings;
    updatedSettings.credentialStorage = credentialPreferenceForStorage(*parsedTarget);
    if (!persistApplicationSettings(updatedSettings))
    {
        m_credentialVaults->select(previousStorage);
        setCredentialOperationError(tr(
            "Credentials were copied, but the storage preference could not be saved. The old store remains active."));
        emit credentialVaultChanged();
        return false;
    }
    emit hostProfilesChanged();
    if (removeSource)
    {
        auto cleaned = m_credentialVaults->removeAll(previousStorage);
        if (!cleaned)
        {
            setCredentialOperationError(tr("Migration succeeded, but credentials remain in the previous store."));
            emit credentialVaultChanged();
            return false;
        }
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

bool AppController::removeAllSavedCredentials()
{
    std::vector<security::CredentialKey> keys;
    const bool emptyUninitializedPortable = m_credentialVaults->storage() == security::CredentialStorage::Portable
                                            && !m_credentialVaults->portableInitialized();
    if (!emptyUninitializedPortable)
    {
        auto listedKeys = m_credentialVaults->active().listKeys();
        if (!listedKeys)
        {
            setCredentialOperationError(credentialVaultErrorMessage(listedKeys.error()));
            return false;
        }
        keys = std::move(*listedKeys);
    }
    std::vector<std::pair<security::CredentialKey, security::SensitiveByteArray>> backup;
    backup.reserve(keys.size());
    for (const security::CredentialKey &key : keys)
    {
        auto secret = m_credentialVaults->active().read(key);
        if (!secret)
        {
            setCredentialOperationError(credentialVaultErrorMessage(secret.error()));
            return false;
        }
        backup.emplace_back(key, std::move(*secret));
    }
    auto removed = m_credentialVaults->removeAll();
    if (!removed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    std::vector<ssh::SshProfile> updated = m_profiles;
    for (ssh::SshProfile &profile : updated)
    {
        profile.credentialReference.reset();
    }
    if (!m_profileStore.save(updated))
    {
        for (const auto &[key, secret] : backup)
        {
            const std::string_view bytes = secret.view();
            logCredentialRollbackResult(
                m_credentialVaults->active().store(
                    key, security::SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())))),
                "remove-all restore");
        }
        setCredentialOperationError(tr("Credentials were restored because host profiles could not be updated."));
        return false;
    }
    m_profiles = std::move(updated);
    setCredentialOperationError({});
    emit hostProfilesChanged();
    emit credentialVaultChanged();
    return true;
}

bool AppController::clearCredentialStorage(const QString &target)
{
    const auto parsedTarget = parseCredentialStorage(target);
    if (!parsedTarget)
    {
        setCredentialOperationError(tr("Choose system, portable, or session credential storage."));
        return false;
    }
    if (*parsedTarget == m_credentialVaults->storage())
    {
        return removeAllSavedCredentials();
    }

    auto removed = m_credentialVaults->removeAll(*parsedTarget);
    if (!removed)
    {
        setCredentialOperationError(credentialVaultErrorMessage(removed.error()));
        return false;
    }
    setCredentialOperationError({});
    emit credentialVaultChanged();
    return true;
}

bool AppController::savePortForwardingRule(const QString &id, const QString &label, const QString &profileId,
                                           const QString &type, const QString &bindHost, const int bindPort,
                                           const QString &destinationHost, const int destinationPort,
                                           const bool autoStart)
{
    const auto parsedType = parseForwardingType(type.trimmed());
    const std::string resolvedProfileId = utf8String(profileId.trimmed());
    if (!parsedType || bindPort < 1 || bindPort > 65'535 || resolvedProfileId.empty()
        || std::ranges::find(m_profiles, resolvedProfileId, &ssh::SshProfile::id) == m_profiles.end())
    {
        m_portForwardingOperationError = tr("Choose a valid host profile, forwarding type, and bind port.");
        emit portForwardingRulesChanged();
        return false;
    }
    if (*parsedType != forwarding::PortForwardingType::Dynamic
        && (destinationPort < 1 || destinationPort > 65'535 || destinationHost.trimmed().isEmpty()))
    {
        m_portForwardingOperationError = tr("Enter a destination host and port for this forwarding rule.");
        emit portForwardingRulesChanged();
        return false;
    }

    forwarding::PortForwardingRule rule{
        .id = id.trimmed().isEmpty() ? utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces))
                                     : utf8String(id.trimmed()),
        .label = utf8String(label.trimmed()),
        .profileId = resolvedProfileId,
        .type = *parsedType,
        .bind = {.host = utf8String(bindHost.trimmed()), .port = static_cast<std::uint16_t>(bindPort)},
        .destination = *parsedType == forwarding::PortForwardingType::Dynamic
                           ? forwarding::PortForwardingEndpoint{}
                           : forwarding::PortForwardingEndpoint{.host = utf8String(destinationHost.trimmed()),
                                                                .port = static_cast<std::uint16_t>(destinationPort)},
        .autoStart = autoStart,
    };
    if (!forwarding::validPortForwardingRule(rule))
    {
        m_portForwardingOperationError = tr("Complete the forwarding rule with valid names and endpoints.");
        emit portForwardingRulesChanged();
        return false;
    }

    std::vector<forwarding::PortForwardingRule> candidate = m_portForwardingRules;
    const auto existing = std::ranges::find(candidate, rule.id, &forwarding::PortForwardingRule::id);
    if (existing == candidate.end())
    {
        candidate.push_back(rule);
    }
    else
    {
        *existing = rule;
    }
    if (!persistPortForwardingRules(candidate))
    {
        return false;
    }
    stopPortForwardingRule(utf8QString(rule.id));
    m_portForwardingRules = std::move(candidate);
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

bool AppController::duplicatePortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto existing = std::ranges::find(m_portForwardingRules, ruleId, &forwarding::PortForwardingRule::id);
    if (existing == m_portForwardingRules.end())
    {
        m_portForwardingOperationError = tr("The forwarding rule no longer exists.");
        emit portForwardingRulesChanged();
        return false;
    }

    forwarding::PortForwardingRule duplicate = *existing;
    duplicate.id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    duplicate.label = utf8String(tr("%1 copy").arg(utf8QString(existing->label)));
    duplicate.autoStart = false;
    std::vector<forwarding::PortForwardingRule> candidate = m_portForwardingRules;
    candidate.push_back(std::move(duplicate));
    if (!persistPortForwardingRules(candidate))
    {
        return false;
    }
    m_portForwardingRules = std::move(candidate);
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

bool AppController::copyPortForwardingBindEndpoint(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto rule = std::ranges::find(m_portForwardingRules, ruleId, &forwarding::PortForwardingRule::id);
    if (rule == m_portForwardingRules.end())
    {
        return false;
    }
    QString host = utf8QString(rule->bind.host);
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('[')))
    {
        host = QStringLiteral("[%1]").arg(host);
    }
    QGuiApplication::clipboard()->setText(QStringLiteral("%1:%2").arg(host).arg(rule->bind.port));
    return true;
}

bool AppController::deletePortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    std::vector<forwarding::PortForwardingRule> candidate = m_portForwardingRules;
    const auto rule = std::ranges::find(candidate, ruleId, &forwarding::PortForwardingRule::id);
    if (rule == candidate.end())
    {
        return false;
    }
    candidate.erase(rule);
    if (!persistPortForwardingRules(candidate))
    {
        return false;
    }
    stopPortForwardingRule(id);
    m_portForwardingRules = std::move(candidate);
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

bool AppController::startPortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto rule = std::ranges::find(m_portForwardingRules, ruleId, &forwarding::PortForwardingRule::id);
    if (rule == m_portForwardingRules.end())
    {
        m_portForwardingOperationError = tr("The forwarding rule no longer exists.");
        emit portForwardingRulesChanged();
        return false;
    }

    if (PortForwardingRuntime *existing = findPortForwardingRuntime(ruleId); existing != nullptr)
    {
        const auto state = existing->job->snapshot().state;
        if (state == forwarding::PortForwardingJobState::Starting
            || state == forwarding::PortForwardingJobState::Running)
        {
            return true;
        }
        stopPortForwardingRule(id);
    }
    if (m_portForwardingRuntimes.size() >= forwarding::maximumActivePortForwardingRuleCount)
    {
        m_portForwardingOperationError = tr("Stop another forwarding rule before starting this one.");
        emit portForwardingRulesChanged();
        return false;
    }

    const auto profile = std::ranges::find(m_profiles, rule->profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        m_portForwardingOperationError = tr("The host profile used by this forwarding rule no longer exists.");
        emit portForwardingRulesChanged();
        return false;
    }
    auto request = connectionRequestForProfile(*profile);
    if (!request)
    {
        m_portForwardingOperationError = m_credentialOperationError;
        emit portForwardingRulesChanged();
        return false;
    }

    PortForwardingRuntime *runtimePointer = nullptr;
    try
    {
        auto runtime = std::make_unique<PortForwardingRuntime>();
        runtime->ruleId = ruleId;
        runtime->job = std::make_unique<forwarding::PortForwardingJob>();
        runtimePointer = runtime.get();
        m_portForwardingRuntimes.push_back(std::move(runtime));
    }
    catch (const std::bad_alloc &)
    {
        m_portForwardingOperationError = tr("Unable to allocate the forwarding worker.");
        emit portForwardingRulesChanged();
        return false;
    }

    const QString qRuleId = utf8QString(ruleId);
    ssh::SshConnectionCallbacks callbacks{
        .confirmUnknownHostKey =
            [this, runtimePointer, qRuleId](const QString &endpoint, const QString &algorithm,
                                            const QString &fingerprint) {
                {
                    std::scoped_lock lock(runtimePointer->hostKeyMutex);
                    runtimePointer->hostKeyDecision.reset();
                    runtimePointer->awaitingHostKey = true;
                }
                try
                {
                    emit portForwardingHostKeyPromptRequested(qRuleId, endpoint, algorithm, fingerprint, false);
                }
                catch (...)
                {
                    resolvePortForwardingHostKey(*runtimePointer, ssh::UnknownHostKeyDecision::Reject);
                }
                std::unique_lock lock(runtimePointer->hostKeyMutex);
                runtimePointer->hostKeyAvailable.wait(lock, [runtimePointer] {
                    return runtimePointer->hostKeyDecision.has_value();
                });
                const ssh::UnknownHostKeyDecision decision = *runtimePointer->hostKeyDecision;
                runtimePointer->hostKeyDecision.reset();
                runtimePointer->awaitingHostKey = false;
                return decision;
            },
        .hostKeyChanged =
            [this, qRuleId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                try
                {
                    emit portForwardingHostKeyPromptRequested(qRuleId, endpoint, algorithm, fingerprint, true);
                }
                catch (...)
                {
                    return;
                }
            },
    };
    auto started = runtimePointer->job->start(
        *rule, std::move(*request), std::move(callbacks),
        [this, qRuleId](const forwarding::PortForwardingJobSnapshot &snapshot) noexcept {
            try
            {
                emit portForwardingSnapshotReady(
                    qRuleId, static_cast<int>(snapshot.state), static_cast<int>(snapshot.failure),
                    static_cast<qulonglong>(snapshot.activeClients), static_cast<qulonglong>(snapshot.bytesFromClients),
                    static_cast<qulonglong>(snapshot.bytesToClients),
                    static_cast<qulonglong>(snapshot.rejectedClients));
            }
            catch (...)
            {
                return;
            }
        });
    if (!started)
    {
        m_portForwardingRuntimes.erase(std::ranges::find(m_portForwardingRuntimes, runtimePointer,
                                                         [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                                             return candidate.get();
                                                         }));
        m_portForwardingOperationError = tr("Unable to start the forwarding worker.");
        emit portForwardingRulesChanged();
        return false;
    }
    m_portForwardingOperationError.clear();
    emit portForwardingRulesChanged();
    return true;
}

void AppController::stopPortForwardingRule(const QString &id)
{
    const std::string ruleId = utf8String(id.trimmed());
    const auto runtime = std::ranges::find(m_portForwardingRuntimes, ruleId,
                                           [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                               return candidate->ruleId;
                                           });
    if (runtime == m_portForwardingRuntimes.end())
    {
        return;
    }
    resolvePortForwardingHostKey(**runtime, ssh::UnknownHostKeyDecision::Reject);
    (*runtime)->job->stop();
    if (m_hostKeyForwardingRuleId == id)
    {
        clearHostKeyPrompt();
    }
    m_portForwardingRuntimes.erase(runtime);
    emit portForwardingRulesChanged();
}

void AppController::acceptHostKey(const bool remember)
{
    if (!m_hostKeyPromptVisible || m_hostKeyChangedWarning)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    const bool forSftp = m_hostKeyForSftp;
    const QString transferTaskId = m_hostKeyTransferTaskId;
    const QString forwardingRuleId = m_hostKeyForwardingRuleId;
    clearHostKeyPrompt();
    if (!forwardingRuleId.isEmpty())
    {
        if (PortForwardingRuntime *runtime = findPortForwardingRuntime(utf8String(forwardingRuleId));
            runtime != nullptr)
        {
            resolvePortForwardingHostKey(*runtime, remember ? ssh::UnknownHostKeyDecision::AcceptAndRemember
                                                            : ssh::UnknownHostKeyDecision::AcceptOnce);
        }
    }
    else if (!transferTaskId.isEmpty() && m_transferManager != nullptr)
    {
        m_transferManager->confirmHostKey(transferTaskId, remember);
    }
    else if (tab != nullptr && forSftp && tab->sftpSession)
    {
        tab->sftpSession->confirmHostKey(remember);
    }
    else if (tab != nullptr && tab->ssh)
    {
        tab->ssh->confirmHostKey(remember);
    }
}

void AppController::rejectHostKey()
{
    if (!m_hostKeyPromptVisible)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    const bool forSftp = m_hostKeyForSftp;
    const QString transferTaskId = m_hostKeyTransferTaskId;
    const QString forwardingRuleId = m_hostKeyForwardingRuleId;
    clearHostKeyPrompt();
    if (!forwardingRuleId.isEmpty())
    {
        if (PortForwardingRuntime *runtime = findPortForwardingRuntime(utf8String(forwardingRuleId));
            runtime != nullptr)
        {
            resolvePortForwardingHostKey(*runtime, ssh::UnknownHostKeyDecision::Reject);
        }
    }
    else if (!transferTaskId.isEmpty() && m_transferManager != nullptr)
    {
        m_transferManager->rejectHostKey(transferTaskId);
    }
    else if (tab != nullptr && forSftp && tab->sftpSession)
    {
        tab->sftpSession->rejectHostKey();
    }
    else if (tab != nullptr && tab->ssh)
    {
        tab->ssh->rejectHostKey();
    }
}

void AppController::initializeSessionLog(TerminalTab &tab)
{
    tab.sessionLog = std::make_shared<logging::SessionLogWriter>();
    const QString tabId = tab.id;
    QObject::connect(tab.sessionLog.get(), &logging::SessionLogWriter::stateChanged, this, [this, tabId] {
        TerminalTab *updated = findTab(tabId);
        if (updated == nullptr || updated->sessionLog == nullptr)
        {
            return;
        }
        if (updated->sessionLog->state() == logging::SessionLogState::Failed)
        {
            updated->status = tr("Session log failed: %1").arg(updated->sessionLog->errorString());
            if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
            {
                terminal->setStatusText(updated->status);
            }
        }
        emit terminalTabsChanged();
    });
}

void AppController::initializeTerminalOutputSink(TerminalTab &tab)
{
    const QString tabId = tab.id;
    tab.outputSink =
        std::make_shared<TerminalOutputFanout>(tab.sessionLog, [this, tabId](const std::span<const std::byte> bytes) {
            if (bytes.empty())
            {
                return;
            }
            const QByteArray copy(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
            QMetaObject::invokeMethod(
                this,
                [this, tabId, copy] {
                    observeScriptOutput(tabId, copy);
                },
                Qt::QueuedConnection);
        });
    if (tab.local)
    {
        tab.local->setOutputSink(tab.outputSink);
    }
    else if (tab.ssh)
    {
        tab.ssh->setOutputSink(tab.outputSink);
    }
}

void AppController::observeScriptOutput(const QString &tabId, const QByteArray &bytes)
{
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || bytes.isEmpty() || !tab->scriptExecution.active())
    {
        return;
    }
    const workbench::ScriptExecutionSnapshot before = tab->scriptExecution.snapshot();
    const auto view = std::span(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    const std::vector<std::string> commands =
        tab->scriptExecution.observeOutput(std::as_bytes(view), scriptExecutionNow());
    dispatchScriptCommands(*tab, commands);
    if (tab->scriptExecution.snapshot() != before)
    {
        emit terminalTabsChanged();
    }
}

void AppController::dispatchScriptCommands(TerminalTab &tab, const std::vector<std::string> &commands)
{
    for (const std::string &command : commands)
    {
        const QString normalized = normalizedQuickCommandText(utf8QString(command));
        if (!validTerminalCommand(normalized))
        {
            static_cast<void>(tab.scriptExecution.cancel());
            return;
        }
        QByteArray bytes = normalized.toUtf8();
        bytes.replace('\n', '\r');
        if (bytes.isEmpty() || bytes.back() != '\r')
        {
            bytes.append('\r');
        }
        appendCapturedHistory(tab, normalized);
        tab.inputHistoryBuffer.clear();
        tab.inputHistoryBufferReliable = true;
        dispatchInput(tab, bytes);
    }
}

void AppController::initializeScriptExecutionTimer()
{
    m_scriptExecutionTimer.setInterval(100);
    m_scriptExecutionTimer.setTimerType(Qt::CoarseTimer);
    QObject::connect(&m_scriptExecutionTimer, &QTimer::timeout, this, [this] {
        bool changed = false;
        const auto now = scriptExecutionNow();
        for (const auto &tab : m_tabs)
        {
            changed = tab->scriptExecution.tick(now) || changed;
        }
        if (changed)
        {
            emit terminalTabsChanged();
        }
    });
    m_scriptExecutionTimer.start();
}

void AppController::connectLocalTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         const TerminalTab *updated = findTab(tabId);
                         if (updated != nullptr)
                         {
                             if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                             {
                                 terminal->setClipboardText(text);
                             }
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::runningChanged, this,
                     [this, tabId](const bool running) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->running = running;
                             if (running && updated->connectedUtcMs == 0)
                             {
                                 updated->connectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
                             }
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_focusedTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
}

void AppController::connectSshTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         const QString workingDirectory =
                             snapshot ? normalizedTerminalWorkingDirectory(snapshot->workingDirectory) : QString{};
                         const bool workingDirectoryChanged = updated->terminalWorkingDirectory != workingDirectory;
                         updated->terminalWorkingDirectory = workingDirectory;
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setSnapshot(snapshot);
                         }
                         if (workingDirectoryChanged && updated->followTerminalDirectory
                             && updated->sftpSession != nullptr && updated->sftpState == QStringLiteral("ready")
                             && !workingDirectory.isEmpty() && workingDirectory != updated->sftpPath)
                         {
                             requestSftpDirectory(*updated, workingDirectory);
                         }
                         else if (workingDirectoryChanged && m_focusedTabId == tabId)
                         {
                             emit sftpChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                         {
                             terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         const TerminalTab *updated = findTab(tabId);
                         if (updated != nullptr)
                         {
                             if (ui::TerminalItem *terminal = m_terminalViewports.value(updated->paneId))
                             {
                                 terminal->setClipboardText(text);
                             }
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::runningChanged, this, [this, tabId](const bool running) {
        if (TerminalTab *updated = findTab(tabId))
        {
            updated->running = running;
            if (running && updated->connectedUtcMs == 0)
            {
                updated->connectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
            }
            emit terminalTabsChanged();
            updateTelemetryVisibility();
        }
    });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::phaseChanged, this,
                     [this, tabId](const ssh::SshConnectionPhase phase) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->sshPhase = phase;
                             if (phase == ssh::SshConnectionPhase::Connected)
                             {
                                 updated->reconnectPending = false;
                                 updated->sshFailure.reset();
                                 updated->connectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
                                 recordRecentConnection(*updated);
                             }
                             else if (phase == ssh::SshConnectionPhase::Failed && updated->sshFailure)
                             {
                                 scheduleSshReconnect(*updated, *updated->sshFailure);
                             }
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::failureOccurred, this,
                     [this, tabId](const ssh::SshFailureKind failure) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->sshFailure = failure;
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_focusedTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
    QObject::connect(
        tab.ssh.get(), &ssh::SshTerminalSession::shellHistoryReady, this,
        [this, tabId](const quint64 requestId, const QString &shell, const QByteArray &contents, const QString &error) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr || updated->historyRequestId != requestId)
            {
                return;
            }
            if (!error.isEmpty())
            {
                updated->history.clear();
                updated->historyState = QStringLiteral("error");
                updated->historyError = error;
                if (m_focusedTabId == tabId)
                {
                    emit terminalHistoryChanged();
                }
                return;
            }
            constexpr qsizetype maximumHistoryBytes = qsizetype{2} * 1024 * 1024;
            if (contents.size() > maximumHistoryBytes)
            {
                updated->history.clear();
                updated->historyState = QStringLiteral("error");
                updated->historyError = tr("Remote shell history exceeded the safety limit.");
                if (m_focusedTabId == tabId)
                {
                    emit terminalHistoryChanged();
                }
                return;
            }

            const QPointer<AppController> self(this);
            QThreadPool::globalInstance()->start([self, tabId, requestId, shell, contents] {
                std::vector<workbench::ShellHistoryEntry> parsed;
                QString parseError;
                try
                {
                    const std::string_view source(contents.constData(), static_cast<std::size_t>(contents.size()));
                    if (shell == QStringLiteral("bash"))
                    {
                        parsed = workbench::parseBashHistory(source);
                    }
                    else if (shell == QStringLiteral("zsh"))
                    {
                        parsed = workbench::parseZshHistory(source);
                    }
                    else if (shell == QStringLiteral("fish"))
                    {
                        parsed = workbench::parseFishHistory(source);
                    }
                    else
                    {
                        parseError = AppController::tr("This remote shell is not supported yet.");
                    }
                }
                catch (const std::bad_alloc &)
                {
                    parseError = AppController::tr("Remote shell history could not be parsed.");
                }
                if (self)
                {
                    emit self->terminalHistoryTaskCompleted(tabId, requestId, std::move(parsed), parseError);
                }
            });
        });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::remoteTelemetryReady, this,
                     [this, tabId](const telemetry::Sample &sample) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->telemetrySample = sample;
                         updated->telemetryHistory.push_back(sample);
                         while (updated->telemetryHistory.size() > telemetry::maximumHistorySamples)
                         {
                             updated->telemetryHistory.pop_front();
                         }
                         if (m_focusedTabId == tabId)
                         {
                             emit remoteTelemetryChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::remoteTelemetryStateChanged, this,
                     [this, tabId](const QString &state) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->telemetryState == state)
                         {
                             return;
                         }
                         updated->telemetryState = state;
                         if (m_focusedTabId == tabId)
                         {
                             emit remoteTelemetryChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyConfirmationRequired, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = false;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, false);
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyChanged, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = false;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, true);
                         if (m_terminal != nullptr)
                         {
                             m_terminal->setStatusText(tr("SSH host key changed; connection blocked"));
                         }
                     });
}

void AppController::updateTelemetryVisibility()
{
    for (const auto &tab : m_tabs)
    {
        if (tab->ssh)
        {
            tab->ssh->setRemoteTelemetryVisible(m_terminalTelemetryVisible && tab->id == m_focusedTabId
                                                && tab->sshPhase == ssh::SshConnectionPhase::Connected);
        }
    }
}

void AppController::connectSftpTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::runningChanged, this,
                     [this, tabId](const bool running) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpSession == nullptr)
                         {
                             return;
                         }
                         if (!running && updated->sftpState != QStringLiteral("error"))
                         {
                             updated->sftpState = QStringLiteral("idle");
                             emit sftpChanged();
                         }
                     });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::homeDirectoryReady, this, [this, tabId](const QString &homePath) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr || updated->sftpSession == nullptr)
            {
                return;
            }
            updated->sftpHomePath = homePath;
            const QString requestedPath =
                !updated->sftpHasListing && updated->sftpPath == QStringLiteral("/") ? homePath : updated->sftpPath;
            requestSftpDirectory(*updated, requestedPath);
        });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::directoryReady, this,
                     [this, tabId](const quint64 requestId, const quint64 generation, const QString &remotePath,
                                   const sftp::DirectoryListingPtr &entries) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpModel == nullptr || requestId != updated->sftpRequestId
                             || generation != updated->sftpGeneration)
                         {
                             return;
                         }
                         updated->sftpPath = remotePath;
                         updated->sftpRequestedPath = remotePath;
                         updated->sftpState = QStringLiteral("ready");
                         updated->sftpError.clear();
                         updated->sftpHasListing = true;
                         updated->sftpModel->setEntries(entries, remotePath);
                         persistWorkspaceState(*updated, true);
                         if (m_focusedTabId == tabId)
                         {
                             emit sftpChanged();
                         }
                     });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::treeDirectoryReady, this,
                     [this, tabId](const quint64, const quint64 generation, const QString &remotePath,
                                   const sftp::DirectoryListingPtr &entries) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpModel == nullptr
                             || generation != updated->sftpGeneration)
                         {
                             return;
                         }
                         updated->sftpModel->applyTreeEntries(remotePath, entries);
                     });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::treeDirectoryFailed, this,
                     [this, tabId](const quint64, const quint64 generation, const QString &remotePath,
                                   const ssh::SshTransportErrorKind) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || updated->sftpModel == nullptr
                             || generation != updated->sftpGeneration)
                         {
                             return;
                         }
                         updated->sftpModel->applyTreeError(remotePath, tr("This folder could not be expanded."));
                     });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::operationSucceeded, this,
                     [this, tabId](const quint64, const sftp::SftpOperationKind) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated != nullptr && updated->sftpSession != nullptr)
                         {
                             requestSftpDirectory(*updated, updated->sftpPath);
                         }
                     });
    QObject::connect(
        tab.sftpSession.get(), &sftp::SftpSession::operationFailed, this,
        [this, tabId](const quint64, const sftp::SftpOperationKind operation, const ssh::SshTransportErrorKind) {
            TerminalTab *updated = findTab(tabId);
            if (updated == nullptr)
            {
                return;
            }
            if (operation == sftp::SftpOperationKind::ListDirectory)
            {
                updated->sftpState = updated->sftpHasListing ? QStringLiteral("ready") : QStringLiteral("error");
                updated->sftpError = tr("The remote directory could not be loaded.");
            }
            else
            {
                updated->sftpError = tr("The remote file operation failed.");
            }
            if (m_focusedTabId == tabId)
            {
                emit sftpChanged();
            }
        });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::connectionFailed, this,
                     [this, tabId](const ssh::SshFailureKind) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->sftpState = QStringLiteral("error");
                         updated->sftpError = tr("The SFTP connection failed.");
                         if (m_focusedTabId == tabId)
                         {
                             emit sftpChanged();
                         }
                     });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::hostKeyConfirmationRequired, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = true;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, false);
                     });
    QObject::connect(tab.sftpSession.get(), &sftp::SftpSession::hostKeyChanged, this,
                     [this, tabId](const QString &endpoint, const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         m_hostKeyForSftp = true;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(endpoint, algorithm, fingerprint, true);
                     });
}

void AppController::recordRecentConnection(TerminalTab &tab)
{
    if (tab.recentConnectionRecorded || tab.sourceProfileId.isEmpty())
    {
        return;
    }
    tab.recentConnectionRecorded = true;
    const auto profile = std::ranges::find(m_profiles, utf8String(tab.sourceProfileId), &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return;
    }

    const std::optional<std::int64_t> previous = profile->lastConnectedUtcMs;
    profile->lastConnectedUtcMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (!m_profileStore.save(m_profiles))
    {
        profile->lastConnectedUtcMs = previous;
        qCWarning(appControllerLog) << "Unable to persist recent SSH connection metadata";
        return;
    }
    emit hostProfilesChanged();
}

void AppController::connectTerminalSignals(ui::TerminalItem &terminal, const QString &paneId)
{
    QObject::connect(&terminal, &ui::TerminalItem::inputGenerated, this, [this, paneId](const QByteArray &bytes) {
        if (activateTerminalPane(paneId))
        {
            queueInput(bytes);
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::pasteRequested, this, [this, paneId](const QByteArray &bytes) {
        if (activateTerminalPane(paneId))
        {
            queuePaste(bytes);
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::sizeRequested, this,
                     [this, paneId](const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                    const quint32 cellHeightPixels) {
                         TerminalTab *tab = findTabForPane(paneId);
                         if (tab != nullptr && tab->ssh)
                         {
                             tab->ssh->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
                         }
                         else if (tab != nullptr && tab->local)
                         {
                             tab->local->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
                         }
                     });
    QObject::connect(&terminal, &ui::TerminalItem::scrollRequested, this, [this, paneId](const int rows) {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->requestScroll(rows);
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->requestScroll(rows);
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::selectionRequested, this,
                     [this, paneId](const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                    const quint16 endRow, const bool rectangular) {
                         TerminalTab *tab = findTabForPane(paneId);
                         if (tab != nullptr && tab->ssh)
                         {
                             tab->ssh->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
                         }
                         else if (tab != nullptr && tab->local)
                         {
                             tab->local->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
                         }
                     });
    QObject::connect(&terminal, &ui::TerminalItem::clearSelectionRequested, this, [this, paneId] {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->clearSelection();
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->clearSelection();
        }
    });
    QObject::connect(&terminal, &ui::TerminalItem::copyRequested, this, [this, paneId] {
        TerminalTab *tab = findTabForPane(paneId);
        if (tab != nullptr && tab->ssh)
        {
            tab->ssh->copySelection();
        }
        else if (tab != nullptr && tab->local)
        {
            tab->local->copySelection();
        }
    });
}

void AppController::queueInput(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || bytes.isEmpty())
    {
        return;
    }
    observeTerminalInput(*tab, bytes);
    dispatchInput(*tab, bytes);
}

void AppController::queuePaste(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr || bytes.isEmpty())
    {
        return;
    }
    observeTerminalInput(*tab, bytes);
    dispatchPaste(*tab, bytes);
}

void AppController::dispatchInput(TerminalTab &tab, const QByteArray &bytes)
{
    if (tab.ssh)
    {
        tab.ssh->queueInput(bytes);
    }
    else if (tab.local)
    {
        tab.local->queueInput(bytes);
    }
}

void AppController::dispatchPaste(TerminalTab &tab, const QByteArray &bytes)
{
    if (tab.ssh)
    {
        tab.ssh->queuePaste(bytes);
    }
    else if (tab.local)
    {
        tab.local->queuePaste(bytes);
    }
}

void AppController::observeTerminalInput(TerminalTab &tab, const QByteArray &bytes)
{
    for (const char character : bytes)
    {
        const auto value = static_cast<unsigned char>(character);
        if (character == '\r' || character == '\n')
        {
            if (tab.inputHistoryBufferReliable)
            {
                appendCapturedHistory(tab, QString::fromUtf8(tab.inputHistoryBuffer));
            }
            tab.inputHistoryBuffer.clear();
            tab.inputHistoryBufferReliable = true;
            continue;
        }
        if (value == 0x08U || value == 0x7FU)
        {
            if (tab.inputHistoryBufferReliable)
            {
                removeLastUtf8CodePoint(tab.inputHistoryBuffer);
            }
            continue;
        }
        if (value == 0x15U || value == 0x03U)
        {
            tab.inputHistoryBuffer.clear();
            tab.inputHistoryBufferReliable = true;
            continue;
        }
        if (value < 0x20U)
        {
            tab.inputHistoryBufferReliable = false;
            continue;
        }
        if (tab.inputHistoryBufferReliable)
        {
            tab.inputHistoryBuffer.append(character);
            if (tab.inputHistoryBuffer.size() > maximumPendingHistoryBytes)
            {
                tab.inputHistoryBuffer.clear();
                tab.inputHistoryBufferReliable = false;
            }
        }
    }
}

void AppController::appendCapturedHistory(TerminalTab &tab, const QString &command)
{
    const QString normalized = normalizedQuickCommandText(command).trimmed();
    if (!validTerminalCommand(normalized))
    {
        return;
    }

    const workbench::ShellKind shell =
        tab.kind == TerminalTabKind::Local
            ? workbench::ShellKind::powershell
            : (!tab.history.empty() ? tab.history.front().shell : workbench::ShellKind::unknown);
    const std::int64_t timestamp = QDateTime::currentSecsSinceEpoch();
    if (!tab.capturedHistory.empty() && utf8QString(tab.capturedHistory.front().command) == normalized)
    {
        tab.capturedHistory.front().timestampUtcSeconds = timestamp;
    }
    else
    {
        tab.capturedHistory.insert(tab.capturedHistory.begin(),
                                   workbench::ShellHistoryEntry{.command = utf8String(normalized),
                                                                .shell = shell,
                                                                .timestampUtcSeconds = timestamp});
        if (tab.capturedHistory.size() > maximumHistoryEntries)
        {
            tab.capturedHistory.resize(maximumHistoryEntries);
        }
    }
    emit terminalHistoryChanged();
}

void AppController::requestScroll(const int rows)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestScroll(rows);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestScroll(rows);
    }
}

void AppController::requestSelection(const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                     const quint16 endRow, const bool rectangular)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
}

void AppController::clearSelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->clearSelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->clearSelection();
    }
}

void AppController::copySelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->copySelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->copySelection();
    }
}

void AppController::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                  const quint32 cellHeightPixels)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
}

AppController::TerminalTab *AppController::activeTab()
{
    return findTab(m_focusedTabId);
}

const AppController::TerminalTab *AppController::activeTab() const
{
    return findTab(m_focusedTabId);
}

AppController::TerminalTab *AppController::findTab(const QString &id)
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

const AppController::TerminalTab *AppController::findTab(const QString &id) const
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

AppController::TerminalTab *AppController::findTabForPane(const QString &paneId)
{
    const auto tab = std::ranges::find(m_tabs, paneId, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->paneId;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

const AppController::TerminalTab *AppController::findTabForPane(const QString &paneId) const
{
    const auto tab = std::ranges::find(m_tabs, paneId, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->paneId;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

workbench::TerminalWorkspaceLayout *AppController::findTerminalWorkspace(const QString &id)
{
    const auto workspace =
        std::ranges::find(m_workspaceState.terminalWorkspaces, utf8String(id), &workbench::TerminalWorkspaceLayout::id);
    return workspace == m_workspaceState.terminalWorkspaces.end() ? nullptr : &*workspace;
}

const workbench::TerminalWorkspaceLayout *AppController::findTerminalWorkspace(const QString &id) const
{
    const auto workspace =
        std::ranges::find(m_workspaceState.terminalWorkspaces, utf8String(id), &workbench::TerminalWorkspaceLayout::id);
    return workspace == m_workspaceState.terminalWorkspaces.end() ? nullptr : &*workspace;
}

QString AppController::firstTabIdForWorkspace(const workbench::TerminalWorkspaceLayout &workspace) const
{
    const std::vector<std::string> panes = workbench::terminalPaneOrder(workspace);
    if (panes.empty())
    {
        return {};
    }
    const TerminalTab *tab = findTabForPane(utf8QString(panes.front()));
    return tab == nullptr ? QString{} : tab->id;
}

bool AppController::persistTerminalWorkspaces()
{
    return saveWorkspaceStateCandidate(m_workspaceState);
}

bool AppController::saveWorkspaceStateCandidate(const workbench::WorkspaceState &candidate)
{
    workbench::WorkspaceState persistable = candidate;
    std::erase_if(persistable.terminalWorkspaces, [](const workbench::TerminalWorkspaceLayout &workspace) {
        return std::ranges::any_of(workspace.restoreIntents, [](const workbench::TerminalRestoreIntent &intent) {
            return intent.kind == workbench::TerminalRestoreKind::Transient;
        });
    });
    const auto active = std::ranges::find(persistable.terminalWorkspaces, persistable.activeTerminalWorkspaceId,
                                          &workbench::TerminalWorkspaceLayout::id);
    if (active == persistable.terminalWorkspaces.end())
    {
        persistable.activeTerminalWorkspaceId =
            persistable.terminalWorkspaces.empty() ? std::string{} : persistable.terminalWorkspaces.front().id;
    }
    if (!workbench::validWorkspaceState(persistable))
    {
        qCWarning(appControllerLog) << "Terminal workspace candidate failed domain validation"
                                    << "profiles=" << persistable.profiles.size()
                                    << "workspaces=" << persistable.terminalWorkspaces.size()
                                    << "activeWorkspaceEmpty=" << persistable.activeTerminalWorkspaceId.empty();
        for (const workbench::TerminalWorkspaceLayout &workspace : persistable.terminalWorkspaces)
        {
            qCWarning(appControllerLog) << "workspaceValid=" << workbench::validTerminalWorkspaceLayout(workspace)
                                        << "nodes=" << workspace.nodes.size()
                                        << "intents=" << workspace.restoreIntents.size();
        }
    }
    const auto saved = m_workspaceStateStore.save(persistable);
    if (!saved)
    {
        qCWarning(appControllerLog) << "Unable to persist terminal workspace topology"
                                    << "error=" << static_cast<int>(saved.error());
        return false;
    }
    return true;
}

void AppController::emitActiveTerminalContextChanged()
{
    updateTelemetryVisibility();
    emit activeTerminalTabChanged();
    emit terminalWorkspaceChanged();
    emit remoteTelemetryChanged();
    emit sshActiveChanged();
    emit terminalSearchChanged();
    emit terminalHistoryChanged();
    emit sftpChanged();
    showActiveTab();
}

void AppController::showActiveTab()
{
    showAllTerminalViewports();
    const TerminalTab *tab = activeTab();
    if (tab != nullptr)
    {
        m_terminal = m_terminalViewports.value(tab->paneId);
    }
    if (m_terminal != nullptr && tab == nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(tr("No terminal session"));
        m_terminal->setKeywordHighlightRules({});
    }
}

void AppController::showTabInViewport(const TerminalTab &tab)
{
    ui::TerminalItem *terminal = m_terminalViewports.value(tab.paneId);
    if (terminal == nullptr)
    {
        return;
    }
    terminal->setSnapshot(tab.snapshot);
    terminal->setStatusText(tab.status);
    terminal->setKeywordHighlightRules(tab.keywordHighlightEnabled ? keywordRulesVariant(tab) : QVariantList{});
    terminal->requestCurrentSize();
}

void AppController::showAllTerminalViewports()
{
    for (const auto &tab : m_tabs)
    {
        showTabInViewport(*tab);
    }
}

QVariantList AppController::keywordRulesVariant(const TerminalTab &tab) const
{
    QVariantList rules;
    rules.reserve(static_cast<qsizetype>(tab.keywordHighlightRules.size()));
    for (const ssh::SshKeywordHighlightRule &rule : tab.keywordHighlightRules)
    {
        rules.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(rule.id)},
            {QStringLiteral("pattern"), utf8QString(rule.pattern)},
            {QStringLiteral("foreground"), utf8QString(rule.foreground)},
            {QStringLiteral("background"), utf8QString(rule.background)},
            {QStringLiteral("enabled"), rule.enabled},
            {QStringLiteral("caseSensitive"), rule.caseSensitive},
        });
    }
    return rules;
}

bool AppController::persistKeywordRules(TerminalTab &tab)
{
    if (tab.sourceProfileId.isEmpty())
    {
        return true;
    }
    std::vector<ssh::SshProfile> candidate = m_profiles;
    const std::string profileId = utf8String(tab.sourceProfileId);
    const auto profile = std::ranges::find(candidate, profileId, &ssh::SshProfile::id);
    if (profile == candidate.end())
    {
        return false;
    }
    profile->keywordHighlightRules = tab.keywordHighlightRules;
    profile->keywordHighlightEnabled = tab.keywordHighlightEnabled;
    if (const auto saved = m_profileStore.save(candidate); !saved)
    {
        return false;
    }
    m_profiles = std::move(candidate);
    emit hostProfilesChanged();
    return true;
}

QVariantList AppController::recordedScriptStepsVariant(const TerminalTab &tab) const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(tab.scriptRecorder.steps().size()));
    for (const workbench::RecordedScriptStep &step : tab.scriptRecorder.steps())
    {
        result.append(step.kind == workbench::RecordedScriptStepKind::Send
                          ? QVariantMap{{QStringLiteral("type"), QStringLiteral("send")},
                                        {QStringLiteral("command"), utf8QString(step.command)}}
                          : QVariantMap{{QStringLiteral("type"), QStringLiteral("delay")},
                                        {QStringLiteral("milliseconds"), step.delayMilliseconds}});
    }
    return result;
}

void AppController::replayRecordedScriptStep(const QString &tabId, const std::size_t index,
                                             const std::uint64_t generation)
{
    TerminalTab *tab = findTab(tabId);
    if (tab == nullptr || !tab->scriptPlaybackActive || tab->scriptPlaybackGeneration != generation
        || tab->scriptRecorder.state() != workbench::ScriptRecorderState::Review || !tab->running)
    {
        return;
    }
    const auto &steps = tab->scriptRecorder.steps();
    if (index >= steps.size())
    {
        tab->scriptPlaybackActive = false;
        emit terminalTabsChanged();
        return;
    }

    const workbench::RecordedScriptStep &step = steps[index];
    if (step.kind == workbench::RecordedScriptStepKind::Delay)
    {
        const int delay = static_cast<int>(std::min<std::uint32_t>(step.delayMilliseconds, 60'000));
        QTimer::singleShot(delay, this, [this, tabId, index, generation] {
            replayRecordedScriptStep(tabId, index + 1, generation);
        });
        return;
    }

    const QString command = utf8QString(step.command);
    QByteArray bytes = command.toUtf8();
    bytes.replace('\n', '\r');
    if (bytes.isEmpty() || bytes.back() != '\r')
    {
        bytes.append('\r');
    }
    appendCapturedHistory(*tab, command);
    dispatchInput(*tab, bytes);
    QTimer::singleShot(0, this, [this, tabId, index, generation] {
        replayRecordedScriptStep(tabId, index + 1, generation);
    });
}

void AppController::setHostKeyPrompt(QString endpoint, QString algorithm, QString fingerprint, const bool changed)
{
    m_hostKeyEndpoint = std::move(endpoint);
    m_hostKeyAlgorithm = std::move(algorithm);
    m_hostKeyFingerprint = std::move(fingerprint);
    m_hostKeyPromptVisible = true;
    m_hostKeyChangedWarning = changed;
    emit hostKeyPromptChanged();
}

void AppController::clearHostKeyPrompt()
{
    if (!m_hostKeyPromptVisible && m_hostKeyTabId.isEmpty() && m_hostKeyTransferTaskId.isEmpty()
        && m_hostKeyForwardingRuleId.isEmpty() && m_hostKeyEndpoint.isEmpty() && m_hostKeyAlgorithm.isEmpty()
        && m_hostKeyFingerprint.isEmpty())
    {
        return;
    }
    m_hostKeyPromptVisible = false;
    m_hostKeyChangedWarning = false;
    m_hostKeyForSftp = false;
    m_hostKeyTransferTaskId.clear();
    m_hostKeyForwardingRuleId.clear();
    m_hostKeyTabId.clear();
    m_hostKeyEndpoint.clear();
    m_hostKeyAlgorithm.clear();
    m_hostKeyFingerprint.clear();
    emit hostKeyPromptChanged();
}

void AppController::loadPortForwardingRules()
{
    auto rules = m_portForwardingStore.load();
    if (!rules)
    {
        qCWarning(appControllerLog) << "Unable to load port forwarding rules";
        m_portForwardingOperationError = tr("Unable to load the saved port forwarding rules.");
        m_portForwardingRules.clear();
        return;
    }
    m_portForwardingRules = std::move(*rules);
    QTimer::singleShot(0, this, [this] {
        if (m_shutdownStarted || portableVaultLocked())
        {
            return;
        }
        std::vector<QString> autoStartIds;
        autoStartIds.reserve(m_portForwardingRules.size());
        for (const forwarding::PortForwardingRule &rule : m_portForwardingRules)
        {
            if (rule.autoStart)
            {
                autoStartIds.push_back(utf8QString(rule.id));
            }
        }
        for (const QString &id : autoStartIds)
        {
            (void)startPortForwardingRule(id);
        }
    });
}

bool AppController::persistPortForwardingRules(const std::vector<forwarding::PortForwardingRule> &rules)
{
    if (const auto saved = m_portForwardingStore.save(rules); !saved)
    {
        m_portForwardingOperationError = tr("Unable to save the port forwarding rules.");
        emit portForwardingRulesChanged();
        return false;
    }
    return true;
}

void AppController::applyPortForwardingSnapshot(const std::string &ruleId,
                                                const forwarding::PortForwardingJobSnapshot &snapshot)
{
    PortForwardingRuntime *runtime = findPortForwardingRuntime(ruleId);
    if (runtime == nullptr || !runtime->job || runtime->job->snapshot() != snapshot)
    {
        return;
    }
    if (snapshot.state == forwarding::PortForwardingJobState::Failed)
    {
        switch (snapshot.failure)
        {
            case forwarding::PortForwardingJobFailure::Connection:
                m_portForwardingOperationError = tr("The SSH connection for the forwarding rule failed.");
                break;
            case forwarding::PortForwardingJobFailure::Listener:
                m_portForwardingOperationError = tr("The local bind address or port is unavailable.");
                break;
            case forwarding::PortForwardingJobFailure::RemoteListener:
                m_portForwardingOperationError = tr("The SSH server rejected the remote listener.");
                break;
            case forwarding::PortForwardingJobFailure::Transport:
                m_portForwardingOperationError = tr("The forwarding transport was disconnected.");
                break;
            case forwarding::PortForwardingJobFailure::ResourceLimit:
                m_portForwardingOperationError = tr("The forwarding worker exhausted an internal resource.");
                break;
            case forwarding::PortForwardingJobFailure::None:
                break;
        }
    }
    emit portForwardingRulesChanged();
}

AppController::PortForwardingRuntime *AppController::findPortForwardingRuntime(const std::string_view ruleId) noexcept
{
    const auto runtime = std::ranges::find(m_portForwardingRuntimes, ruleId,
                                           [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                               return std::string_view(candidate->ruleId);
                                           });
    return runtime == m_portForwardingRuntimes.end() ? nullptr : runtime->get();
}

const AppController::PortForwardingRuntime *
AppController::findPortForwardingRuntime(const std::string_view ruleId) const noexcept
{
    const auto runtime = std::ranges::find(m_portForwardingRuntimes, ruleId,
                                           [](const std::unique_ptr<PortForwardingRuntime> &candidate) {
                                               return std::string_view(candidate->ruleId);
                                           });
    return runtime == m_portForwardingRuntimes.end() ? nullptr : runtime->get();
}

void AppController::resolvePortForwardingHostKey(PortForwardingRuntime &runtime,
                                                 const ssh::UnknownHostKeyDecision decision) noexcept
{
    {
        std::scoped_lock lock(runtime.hostKeyMutex);
        runtime.hostKeyDecision = decision;
    }
    runtime.hostKeyAvailable.notify_all();
}

void AppController::stopAllPortForwardingRules() noexcept
{
    for (const auto &runtime : m_portForwardingRuntimes)
    {
        resolvePortForwardingHostKey(*runtime, ssh::UnknownHostKeyDecision::Reject);
        runtime->job->stop();
    }
    m_portForwardingRuntimes.clear();
}

void AppController::loadHostProfiles()
{
    auto profiles = m_profileStore.load();
    if (!profiles)
    {
        qCWarning(appControllerLog) << "Unable to load SSH profiles; starting with an empty profile list";
        return;
    }
    m_profiles = std::move(*profiles);
}

void AppController::loadApplicationSettings()
{
    auto settings = m_settingsStore.load();
    if (!settings)
    {
        qCWarning(appControllerLog) << "Unable to load application settings; using defaults";
        return;
    }
    m_settings = std::move(*settings);
    security::CredentialStorage selected = m_defaultCredentialStorage;
    switch (m_settings.credentialStorage)
    {
        case config::CredentialStoragePreference::system:
            selected = security::CredentialStorage::System;
            break;
        case config::CredentialStoragePreference::portable:
            selected = security::CredentialStorage::Portable;
            break;
        case config::CredentialStoragePreference::session:
            selected = security::CredentialStorage::Session;
            break;
        case config::CredentialStoragePreference::automatic:
            break;
    }
    m_credentialVaults->select(selected);
}

void AppController::initializeActionRegistry()
{
    m_actionRegistry.setOverrides(m_settings.shortcutOverrides);
    m_settings.shortcutOverrides = m_actionRegistry.overrides();
    QObject::connect(this, &AppController::terminalTabsChanged, this, &AppController::actionRegistryChanged);
    QObject::connect(this, &AppController::activeTerminalTabChanged, this, &AppController::actionRegistryChanged);
}

QVariantMap AppController::shortcutResult(const actions::ShortcutValidation &validation) const
{
    QString conflictingLabel;
    if (!validation.conflictingActionId.isEmpty())
    {
        const QVariantList registeredActions = m_actionRegistry.actions(activeTab() != nullptr);
        for (const QVariant &entry : registeredActions)
        {
            const QVariantMap action = entry.toMap();
            if (action.value(QStringLiteral("id")).toString() == validation.conflictingActionId)
            {
                conflictingLabel = action.value(QStringLiteral("label")).toString();
                break;
            }
        }
    }
    return {
        {QStringLiteral("valid"), validation.valid()},
        {QStringLiteral("shortcut"), validation.normalized},
        {QStringLiteral("error"), actions::ActionRegistry::validationMessage(validation.error, conflictingLabel)},
        {QStringLiteral("conflictingActionId"), validation.conflictingActionId},
    };
}

void AppController::loadQuickCommands()
{
    auto scripts = m_scriptStore.loadOrMigrate(m_legacyQuickCommandPath);
    if (!scripts)
    {
        qCWarning(appControllerLog) << "Unable to load scripts; starting with an empty script list";
        setQuickCommandOperationError(tr("Saved scripts could not be loaded."));
        return;
    }
    m_scripts = std::move(*scripts);
}

void AppController::loadWorkspaceState()
{
    auto state = m_workspaceStateStore.load();
    if (!state)
    {
        qCWarning(appControllerLog) << "Unable to load workspace state; using safe defaults";
        return;
    }
    m_workspaceState = std::move(*state);
    restoreTerminalWorkspaces();
}

void AppController::restoreTerminalWorkspaces()
{
    for (const workbench::TerminalWorkspaceLayout &workspace : m_workspaceState.terminalWorkspaces)
    {
        for (const workbench::TerminalLayoutNode &node : workspace.nodes)
        {
            if (node.kind != workbench::TerminalLayoutNodeKind::Leaf)
            {
                continue;
            }
            const auto intent = std::ranges::find(workspace.restoreIntents, node.restoreIntentId,
                                                  &workbench::TerminalRestoreIntent::id);
            if (intent == workspace.restoreIntents.end() || intent->kind == workbench::TerminalRestoreKind::Transient)
            {
                continue;
            }
            auto tab = std::make_unique<TerminalTab>();
            tab->id = utf8QString(intent->id);
            tab->workspaceId = utf8QString(workspace.id);
            tab->paneId = utf8QString(node.id);
            tab->title = utf8QString(intent->title);
            if (intent->kind == workbench::TerminalRestoreKind::Local)
            {
                tab->kind = TerminalTabKind::Local;
                tab->title = tab->title.isEmpty() ? tr("PowerShell %1").arg(m_nextLocalTabNumber) : tab->title;
                ++m_nextLocalTabNumber;
                tab->status = tr("Restoring local terminal...");
                tab->local = m_localSessionFactory();
                if (!tab->local)
                {
                    continue;
                }
                initializeSessionLog(*tab);
                initializeTerminalOutputSink(*tab);
                connectLocalTabSignals(*tab);
                const QString tabId = tab->id;
                m_tabs.push_back(std::move(tab));
                TerminalTab *created = findTab(tabId);
                if (created != nullptr)
                {
                    const std::error_code error = created->local->start({.columns = 100, .rows = 30});
                    if (error)
                    {
                        created->status =
                            tr("Unable to restore local terminal: %1").arg(QString::fromStdString(error.message()));
                    }
                }
                continue;
            }

            tab->kind = TerminalTabKind::Ssh;
            tab->sourceProfileId = utf8QString(intent->profileId);
            const auto profile = std::ranges::find(m_profiles, intent->profileId, &ssh::SshProfile::id);
            if (profile != m_profiles.end())
            {
                tab->title = tab->title.isEmpty() ? utf8QString(profile->name) : tab->title;
                tab->identity = QStringLiteral("%1@%2:%3")
                                    .arg(utf8QString(profile->username), utf8QString(profile->host))
                                    .arg(profile->port);
                tab->address = utf8QString(profile->host);
                tab->keywordHighlightRules = profile->keywordHighlightRules;
                tab->keywordHighlightEnabled = profile->keywordHighlightEnabled;
                tab->status = tr("SSH workspace restored; reconnect when ready.");
            }
            else
            {
                tab->status = tr("The saved SSH host for this workspace no longer exists.");
            }
            applyWorkspaceState(*tab);
            tab->ssh = std::make_unique<ssh::SshTerminalSession>();
            initializeSessionLog(*tab);
            initializeTerminalOutputSink(*tab);
            connectSshTabSignals(*tab);
            m_tabs.push_back(std::move(tab));
        }
    }

    QString activeWorkspace = utf8QString(m_workspaceState.activeTerminalWorkspaceId);
    if (findTerminalWorkspace(activeWorkspace) == nullptr && !m_workspaceState.terminalWorkspaces.empty())
    {
        activeWorkspace = utf8QString(m_workspaceState.terminalWorkspaces.front().id);
    }
    if (!activeWorkspace.isEmpty())
    {
        static_cast<void>(activateTerminalTab(activeWorkspace));
    }
}

void AppController::applyWorkspaceState(TerminalTab &tab) const
{
    if (tab.sourceProfileId.isEmpty())
    {
        return;
    }
    const workbench::ProfileWorkspaceState *state =
        workbench::findProfileWorkspaceState(m_workspaceState, utf8String(tab.sourceProfileId));
    if (state == nullptr)
    {
        return;
    }
    tab.sftpPath = utf8QString(state->lastRemotePath);
    tab.sftpRequestedPath = tab.sftpPath;
    tab.workbenchPage = utf8QString(state->workbenchPage);
    tab.workbenchSide = utf8QString(state->workbenchSide);
    tab.sftpViewMode = utf8QString(state->sftpViewMode);
    tab.sftpSortColumn = utf8QString(state->sftpSortColumn);
    tab.sftpFilenameEncoding = utf8QString(state->sftpFilenameEncoding);
    tab.followTerminalDirectory = state->followTerminalDirectory;
    tab.sftpSortAscending = state->sftpSortAscending;
    tab.sftpDirectoriesFirst = state->sftpDirectoriesFirst;
    tab.sftpShowModifiedColumn = state->sftpShowModifiedColumn;
    tab.sftpShowSizeColumn = state->sftpShowSizeColumn;
    tab.sftpShowTypeColumn = state->sftpShowTypeColumn;
    tab.workbenchWidth = state->workbenchWidth;
    tab.composerHeight = state->composerHeight;
}

void AppController::persistWorkspaceState(const TerminalTab &tab, const bool shouldRecordRemotePath)
{
    if (tab.sourceProfileId.isEmpty())
    {
        return;
    }
    workbench::WorkspaceState candidate = m_workspaceState;
    workbench::ProfileWorkspaceState &state =
        workbench::ensureProfileWorkspaceState(candidate, utf8String(tab.sourceProfileId));
    if (shouldRecordRemotePath)
    {
        workbench::recordRecentRemotePath(state, utf8String(tab.sftpPath));
    }
    state.workbenchPage = utf8String(tab.workbenchPage);
    state.workbenchSide = utf8String(tab.workbenchSide);
    state.sftpViewMode = utf8String(tab.sftpViewMode);
    state.sftpSortColumn = utf8String(tab.sftpSortColumn);
    state.sftpFilenameEncoding = utf8String(tab.sftpFilenameEncoding);
    state.followTerminalDirectory = tab.followTerminalDirectory;
    state.sftpSortAscending = tab.sftpSortAscending;
    state.sftpDirectoriesFirst = tab.sftpDirectoriesFirst;
    state.sftpShowModifiedColumn = tab.sftpShowModifiedColumn;
    state.sftpShowSizeColumn = tab.sftpShowSizeColumn;
    state.sftpShowTypeColumn = tab.sftpShowTypeColumn;
    state.workbenchWidth = tab.workbenchWidth;
    state.composerHeight = tab.composerHeight;
    if (!saveWorkspaceStateCandidate(candidate))
    {
        qCWarning(appControllerLog) << "Unable to persist safe workspace state";
        return;
    }
    m_workspaceState = std::move(candidate);
}

bool AppController::persistApplicationSettings(const config::ApplicationSettings &settings)
{
    if (!m_settingsStore.save(settings))
    {
        qCWarning(appControllerLog) << "Unable to persist application settings";
        return false;
    }
    if (m_settings == settings)
    {
        return true;
    }
    m_settings = settings;
    for (const auto &tab : m_tabs)
    {
        if (tab->sftpModel != nullptr)
        {
            tab->sftpModel->setShowHidden(m_settings.sftpShowHiddenFiles);
        }
    }
    emit applicationSettingsChanged();
    return true;
}

void AppController::setCredentialOperationError(QString message)
{
    if (m_credentialOperationError == message)
    {
        return;
    }
    m_credentialOperationError = std::move(message);
    emit credentialVaultChanged();
}

void AppController::setQuickCommandOperationError(QString message)
{
    if (m_quickCommandOperationError == message)
    {
        return;
    }
    m_quickCommandOperationError = std::move(message);
    emit quickCommandsChanged();
}

} // namespace ztermy
