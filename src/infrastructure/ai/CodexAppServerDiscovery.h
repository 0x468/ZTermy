#pragma once

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>

class QTemporaryDir;

namespace ztermy::ai
{

struct CodexAppServerInstallation final
{
    QString program;
    QString version;
    bool dynamicToolsVerified = false;
};

class CodexAppServerDiscovery final : public QObject
{
public:
    using Handler = std::function<void(std::expected<CodexAppServerInstallation, QString>)>;

    explicit CodexAppServerDiscovery(QObject *parent = nullptr);
    ~CodexAppServerDiscovery() override;

    CodexAppServerDiscovery(const CodexAppServerDiscovery &) = delete;
    CodexAppServerDiscovery &operator=(const CodexAppServerDiscovery &) = delete;

    [[nodiscard]] std::expected<void, QString> start(const QString &program, Handler handler);
    void stop();
    [[nodiscard]] bool running() const noexcept;

    [[nodiscard]] static std::expected<CodexAppServerInstallation, QString>
    inspectGeneratedSchema(const QString &schemaDirectory, const QString &program, const QString &version);

private:
    enum class State : std::uint8_t
    {
        stopped,
        readingVersion,
        generatingSchema,
        failed,
    };

    void startSchemaGeneration();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void fail(const QString &message);

    QProcess m_process;
    QTimer m_deadline;
    std::unique_ptr<QTemporaryDir> m_schemaDirectory;
    Handler m_handler;
    QString m_program;
    QString m_version;
    QByteArray m_output;
    State m_state = State::stopped;
};

} // namespace ztermy::ai
