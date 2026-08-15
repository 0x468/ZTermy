#include "infrastructure/ai/AcpAgentDiscovery.h"

#include <QFileInfo>

#include <utility>

namespace ztermy::ai
{
namespace
{

constexpr qsizetype maximumOutputBytes = qsizetype{64} * 1024;

} // namespace

AcpAgentDiscovery::AcpAgentDiscovery(QObject *parent) : QObject(parent)
{
    QObject::connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray bytes = m_process.readAllStandardOutput();
        if (bytes.size() > maximumOutputBytes || m_output.size() > maximumOutputBytes - bytes.size())
        {
            fail(QStringLiteral("ACP Agent discovery output exceeded 64 KiB."));
            return;
        }
        m_output.append(bytes);
    });
    QObject::connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        static_cast<void>(m_process.readAllStandardError());
    });
    QObject::connect(&m_process, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError) {
        if (m_running)
        {
            fail(QStringLiteral("ACP Agent discovery could not start: %1").arg(m_process.errorString()));
        }
    });
    QObject::connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                     [this](const int exitCode, const QProcess::ExitStatus status) {
                         finish(exitCode, status);
                     });
    m_deadline.setSingleShot(true);
    QObject::connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("ACP Agent discovery timed out."));
    });
}

AcpAgentDiscovery::~AcpAgentDiscovery()
{
    stop();
}

std::expected<void, QString> AcpAgentDiscovery::start(const QString &program, Handler handler)
{
    if (m_running)
    {
        return std::unexpected(QStringLiteral("ACP Agent discovery is already running."));
    }
    const QFileInfo executable(program);
    if (!executable.isAbsolute() || !executable.exists() || !executable.isFile() || !handler)
    {
        return std::unexpected(QStringLiteral("ACP Agent discovery requires an existing absolute executable."));
    }
    m_program = executable.absoluteFilePath();
    m_handler = std::move(handler);
    m_output.clear();
    m_running = true;
    m_process.setProgram(m_program);
    m_process.setArguments({QStringLiteral("--version")});
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start(QIODevice::ReadOnly);
    m_deadline.start(5000);
    return {};
}

void AcpAgentDiscovery::stop()
{
    m_deadline.stop();
    m_handler = {};
    m_running = false;
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
        if (!m_process.waitForFinished(1000))
        {
            m_process.close();
        }
    }
    m_output.clear();
    m_program.clear();
}

bool AcpAgentDiscovery::running() const noexcept
{
    return m_running;
}

void AcpAgentDiscovery::fail(const QString &message)
{
    if (!m_running)
    {
        return;
    }
    m_deadline.stop();
    m_running = false;
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
    }
    auto handler = std::move(m_handler);
    if (handler)
    {
        handler(std::unexpected(message));
    }
}

void AcpAgentDiscovery::finish(const int exitCode, const QProcess::ExitStatus status)
{
    if (!m_running)
    {
        return;
    }
    m_deadline.stop();
    m_output.append(m_process.readAllStandardOutput());
    const QString version = QString::fromUtf8(m_output).trimmed().section(QLatin1Char('\n'), 0, 0).trimmed();
    if (status != QProcess::NormalExit || exitCode != 0 || version.isEmpty() || version.size() > 256)
    {
        fail(QStringLiteral("ACP Agent version discovery failed."));
        return;
    }
    m_running = false;
    auto handler = std::move(m_handler);
    if (handler)
    {
        handler(AcpAgentInstallation{.program = m_program, .version = version});
    }
}

} // namespace ztermy::ai
