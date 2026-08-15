#include "infrastructure/ai/CodexAppServerDiscovery.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <array>
#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumProcessOutputBytes = qsizetype{64} * 1024;
constexpr qint64 maximumSchemaBytes = qint64{4} * 1024 * 1024;

[[nodiscard]] std::expected<QJsonObject, QString> schemaObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > maximumSchemaBytes)
    {
        return std::unexpected(QStringLiteral("The Codex experimental schema is missing or exceeds 4 MiB."));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(QStringLiteral("The Codex experimental schema is invalid JSON."));
    }
    return document.object();
}

[[nodiscard]] bool hasRequired(const QJsonObject &schema, const std::span<const QString> names)
{
    const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
    for (const QString &name : names)
    {
        if (!required.contains(name))
        {
            return false;
        }
    }
    return true;
}

} // namespace

CodexAppServerDiscovery::CodexAppServerDiscovery(QObject *parent) : QObject(parent)
{
    QObject::connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray bytes = m_process.readAllStandardOutput();
        if (bytes.size() > maximumProcessOutputBytes || m_output.size() > maximumProcessOutputBytes - bytes.size())
        {
            fail(QStringLiteral("The Codex discovery output exceeded 64 KiB."));
            return;
        }
        m_output.append(bytes);
    });
    QObject::connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        static_cast<void>(m_process.readAllStandardError());
    });
    QObject::connect(&m_process, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError) {
        fail(QStringLiteral("Codex discovery could not start: %1").arg(m_process.errorString()));
    });
    QObject::connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                     [this](const int exitCode, const QProcess::ExitStatus exitStatus) {
                         handleFinished(exitCode, exitStatus);
                     });
    m_deadline.setSingleShot(true);
    QObject::connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("Codex discovery timed out."));
    });
}

CodexAppServerDiscovery::~CodexAppServerDiscovery()
{
    stop();
}

std::expected<void, QString> CodexAppServerDiscovery::start(const QString &program, Handler handler)
{
    stop();
    const QFileInfo executable(program);
    if (!executable.isAbsolute() || !executable.exists() || !executable.isFile())
    {
        return std::unexpected(QStringLiteral("Codex discovery requires an existing absolute executable path."));
    }
    if (!handler)
    {
        return std::unexpected(QStringLiteral("Codex discovery requires a completion handler."));
    }
    m_program = executable.absoluteFilePath();
    m_handler = std::move(handler);
    m_output.clear();
    m_state = State::readingVersion;
    m_process.setProgram(m_program);
    m_process.setArguments({QStringLiteral("--version")});
    m_process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start(QIODevice::ReadOnly);
    m_deadline.start(10'000);
    return {};
}

void CodexAppServerDiscovery::stop()
{
    m_deadline.stop();
    m_state = State::stopped;
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
        static_cast<void>(m_process.waitForFinished(1'000));
    }
    m_schemaDirectory.reset();
    m_handler = {};
    m_output.clear();
}

bool CodexAppServerDiscovery::running() const noexcept
{
    return m_state == State::readingVersion || m_state == State::generatingSchema;
}

std::expected<CodexAppServerInstallation, QString>
CodexAppServerDiscovery::inspectGeneratedSchema(const QString &schemaDirectory, const QString &program,
                                                const QString &version)
{
    const auto thread = schemaObject(schemaDirectory + QStringLiteral("/v2/ThreadStartParams.json"));
    const auto call = schemaObject(schemaDirectory + QStringLiteral("/DynamicToolCallParams.json"));
    const auto response = schemaObject(schemaDirectory + QStringLiteral("/DynamicToolCallResponse.json"));
    if (!thread.has_value())
    {
        return std::unexpected(thread.error());
    }
    if (!call.has_value())
    {
        return std::unexpected(call.error());
    }
    if (!response.has_value())
    {
        return std::unexpected(response.error());
    }
    const QJsonObject properties = thread->value(QStringLiteral("properties")).toObject();
    const std::array callFields{QStringLiteral("arguments"), QStringLiteral("callId"), QStringLiteral("threadId"),
                                QStringLiteral("tool"), QStringLiteral("turnId")};
    const std::array responseFields{QStringLiteral("contentItems"), QStringLiteral("success")};
    if (!properties.contains(QStringLiteral("dynamicTools")) || !hasRequired(*call, callFields)
        || !hasRequired(*response, responseFields))
    {
        return std::unexpected(
            QStringLiteral("This Codex version does not expose the required dynamic-tool protocol."));
    }
    return CodexAppServerInstallation{.program = program, .version = version, .dynamicToolsVerified = true};
}

void CodexAppServerDiscovery::startSchemaGeneration()
{
    m_schemaDirectory = std::make_unique<QTemporaryDir>(QStringLiteral("ztermy-codex-schema-XXXXXX"));
    if (!m_schemaDirectory->isValid())
    {
        fail(QStringLiteral("A temporary Codex schema directory could not be created."));
        return;
    }
    m_output.clear();
    m_state = State::generatingSchema;
    m_process.setArguments({QStringLiteral("app-server"), QStringLiteral("generate-json-schema"),
                            QStringLiteral("--out"), m_schemaDirectory->path(), QStringLiteral("--experimental")});
    m_process.start(QIODevice::ReadOnly);
    m_deadline.start(30'000);
}

void CodexAppServerDiscovery::handleFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    if (!running())
    {
        return;
    }
    m_deadline.stop();
    if (exitStatus != QProcess::NormalExit || exitCode != 0)
    {
        fail(QStringLiteral("Codex discovery exited with code %1.").arg(exitCode));
        return;
    }
    if (m_state == State::readingVersion)
    {
        m_version = QString::fromUtf8(m_output).simplified();
        if (m_version.isEmpty() || m_version.size() > 200)
        {
            fail(QStringLiteral("Codex returned an invalid version string."));
            return;
        }
        startSchemaGeneration();
        return;
    }

    auto installation = inspectGeneratedSchema(m_schemaDirectory->path(), m_program, m_version);
    if (!installation.has_value())
    {
        fail(installation.error());
        return;
    }
    m_state = State::stopped;
    m_schemaDirectory.reset();
    if (m_handler)
    {
        auto handler = std::move(m_handler);
        handler(std::move(*installation));
    }
}

void CodexAppServerDiscovery::fail(const QString &message)
{
    if (!running())
    {
        return;
    }
    m_deadline.stop();
    m_state = State::failed;
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
    }
    m_schemaDirectory.reset();
    if (m_handler)
    {
        auto handler = std::move(m_handler);
        handler(std::unexpected(message));
    }
}

} // namespace ztermy::ai
