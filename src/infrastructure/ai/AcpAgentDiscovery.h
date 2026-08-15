#pragma once

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <expected>
#include <functional>

namespace ztermy::ai
{

struct AcpAgentInstallation final
{
    QString program;
    QString version;
};

class AcpAgentDiscovery final : public QObject
{
public:
    using Handler = std::function<void(std::expected<AcpAgentInstallation, QString>)>;

    explicit AcpAgentDiscovery(QObject *parent = nullptr);
    ~AcpAgentDiscovery() override;

    AcpAgentDiscovery(const AcpAgentDiscovery &) = delete;
    AcpAgentDiscovery &operator=(const AcpAgentDiscovery &) = delete;

    [[nodiscard]] std::expected<void, QString> start(const QString &program, Handler handler);
    void stop();
    [[nodiscard]] bool running() const noexcept;

private:
    void fail(const QString &message);
    void finish(int exitCode, QProcess::ExitStatus status);

    QProcess m_process;
    QTimer m_deadline;
    Handler m_handler;
    QString m_program;
    QByteArray m_output;
    bool m_running = false;
};

} // namespace ztermy::ai
