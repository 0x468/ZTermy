#include "application/AppController.h"

#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QStandardPaths>

#include <system_error>

namespace ztermy
{

AppController::AppController(QObject *parent) : QObject(parent)
{
    QObject::connect(&m_localSession, &terminal::LocalTerminalSession::snapshotReady, this,
                     [this](const terminal::TerminalSnapshotPtr &snapshot) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Local)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(&m_localSession, &terminal::LocalTerminalSession::statusChanged, this,
                     [this](const QString &status) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Local)
                         {
                             m_terminal->setStatusText(status);
                         }
                     });
    QObject::connect(&m_localSession, &terminal::LocalTerminalSession::clipboardTextReady, this,
                     [this](const QString &text) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Local)
                         {
                             m_terminal->setClipboardText(text);
                         }
                     });

    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::snapshotReady, this,
                     [this](const terminal::TerminalSnapshotPtr &snapshot) {
                         if (m_terminal != nullptr && m_activeSession == ActiveSession::Ssh)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::statusChanged, this, [this](const QString &status) {
        if (m_terminal != nullptr && m_activeSession == ActiveSession::Ssh)
        {
            m_terminal->setStatusText(status);
        }
    });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::runningChanged, this, [this](const bool running) {
        if (m_activeSession == ActiveSession::Ssh || running)
        {
            emit sshActiveChanged();
        }
    });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::hostKeyConfirmationRequired, this,
                     [this](const QString &algorithm, const QString &fingerprint) {
                         setHostKeyPrompt(algorithm, fingerprint, false);
                     });
    QObject::connect(&m_sshSession, &ssh::SshTerminalSession::hostKeyChanged, this,
                     [this](const QString &algorithm, const QString &fingerprint) {
                         setHostKeyPrompt(algorithm, fingerprint, true);
                         if (m_terminal != nullptr)
                         {
                             m_terminal->setStatusText(QStringLiteral("SSH host key changed; connection blocked"));
                         }
                     });
}

AppController::~AppController()
{
    shutdown();
}

void AppController::attachTerminal(ui::TerminalItem *terminal)
{
    if (m_terminal == terminal)
    {
        return;
    }
    if (m_terminal != nullptr)
    {
        QObject::disconnect(m_terminal, nullptr, this, nullptr);
    }
    m_terminal = terminal;
    connectTerminalSignals();
}

void AppController::shutdown() noexcept
{
    clearHostKeyPrompt();
    m_sshSession.stop();
    m_localSession.stop();
    m_activeSession = ActiveSession::None;
    if (m_terminal != nullptr)
    {
        QObject::disconnect(m_terminal, nullptr, this, nullptr);
        m_terminal->setSnapshot({});
        m_terminal = nullptr;
    }
}

bool AppController::sshActive() const noexcept
{
    return m_activeSession == ActiveSession::Ssh;
}

bool AppController::hostKeyPromptVisible() const noexcept
{
    return m_hostKeyPromptVisible;
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

void AppController::startLocalTerminal()
{
    clearHostKeyPrompt();
    m_sshSession.stop();
    m_localSession.stop();
    m_activeSession = ActiveSession::Local;
    emit sshActiveChanged();

    if (m_terminal != nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(QStringLiteral("Starting local terminal..."));
    }
    const std::error_code error = m_localSession.start({.columns = 100, .rows = 30});
    if (error && m_terminal != nullptr)
    {
        m_terminal->setStatusText(
            QStringLiteral("Unable to start local terminal: %1").arg(QString::fromStdString(error.message())));
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
}

bool AppController::connectPrivateKey(const QString &host, const int port, const QString &username,
                                      const QString &privateKeyPath)
{
    if (host.trimmed().isEmpty() || username.isEmpty() || privateKeyPath.isEmpty() || port <= 0 || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("Complete the SSH host, port, username, and private-key fields"));
        }
        return false;
    }

    clearHostKeyPrompt();
    m_localSession.stop();
    m_sshSession.stop();
    m_activeSession = ActiveSession::Ssh;
    emit sshActiveChanged();

    if (m_terminal != nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(QStringLiteral("Starting SSH connection..."));
    }

    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const ssh::SshPrivateKeyProfile profile{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username,
        .privateKeyPath = privateKeyPath,
        .knownHostsPath = QDir(dataDirectory).filePath(QStringLiteral("known_hosts.json")),
    };
    const std::error_code error = m_sshSession.start(profile, {.columns = 100, .rows = 30});
    if (error)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("Unable to start SSH connection"));
        }
        return false;
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return true;
}

void AppController::acceptHostKey(const bool remember)
{
    if (!m_hostKeyPromptVisible || m_hostKeyChangedWarning)
    {
        return;
    }
    clearHostKeyPrompt();
    m_sshSession.confirmHostKey(remember);
}

void AppController::rejectHostKey()
{
    if (!m_hostKeyPromptVisible)
    {
        return;
    }
    clearHostKeyPrompt();
    m_sshSession.rejectHostKey();
}

void AppController::connectTerminalSignals()
{
    if (m_terminal == nullptr)
    {
        return;
    }
    QObject::connect(m_terminal, &ui::TerminalItem::inputGenerated, this, &AppController::queueInput);
    QObject::connect(m_terminal, &ui::TerminalItem::pasteRequested, this, &AppController::queuePaste);
    QObject::connect(m_terminal, &ui::TerminalItem::sizeRequested, this, &AppController::requestResize);
    QObject::connect(m_terminal, &ui::TerminalItem::scrollRequested, &m_localSession,
                     &terminal::LocalTerminalSession::requestScroll);
    QObject::connect(m_terminal, &ui::TerminalItem::selectionRequested, &m_localSession,
                     &terminal::LocalTerminalSession::requestSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::clearSelectionRequested, &m_localSession,
                     &terminal::LocalTerminalSession::clearSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::copyRequested, &m_localSession,
                     &terminal::LocalTerminalSession::copySelection);
}

void AppController::queueInput(const QByteArray &bytes)
{
    if (m_activeSession == ActiveSession::Ssh)
    {
        m_sshSession.queueInput(bytes);
    }
    else if (m_activeSession == ActiveSession::Local)
    {
        m_localSession.queueInput(bytes);
    }
}

void AppController::queuePaste(const QByteArray &bytes)
{
    if (m_activeSession == ActiveSession::Ssh)
    {
        m_sshSession.queueInput(bytes);
    }
    else if (m_activeSession == ActiveSession::Local)
    {
        m_localSession.queuePaste(bytes);
    }
}

void AppController::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                  const quint32 cellHeightPixels)
{
    if (m_activeSession == ActiveSession::Ssh)
    {
        m_sshSession.requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
    else if (m_activeSession == ActiveSession::Local)
    {
        m_localSession.requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
}

void AppController::setHostKeyPrompt(QString algorithm, QString fingerprint, const bool changed)
{
    m_hostKeyAlgorithm = std::move(algorithm);
    m_hostKeyFingerprint = std::move(fingerprint);
    m_hostKeyPromptVisible = true;
    m_hostKeyChangedWarning = changed;
    emit hostKeyPromptChanged();
}

void AppController::clearHostKeyPrompt()
{
    if (!m_hostKeyPromptVisible && m_hostKeyAlgorithm.isEmpty() && m_hostKeyFingerprint.isEmpty())
    {
        return;
    }
    m_hostKeyPromptVisible = false;
    m_hostKeyChangedWarning = false;
    m_hostKeyAlgorithm.clear();
    m_hostKeyFingerprint.clear();
    emit hostKeyPromptChanged();
}

} // namespace ztermy
