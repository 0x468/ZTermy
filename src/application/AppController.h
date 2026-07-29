#pragma once

#include "application/ssh/SshTerminalSession.h"
#include "application/terminal/LocalTerminalSession.h"
#include "infrastructure/ssh/SshProfileStore.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <vector>

namespace ztermy::ui
{
class TerminalItem;
}

namespace ztermy
{

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sshActive READ sshActive NOTIFY sshActiveChanged)
    Q_PROPERTY(bool hostKeyPromptVisible READ hostKeyPromptVisible NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString hostKeyAlgorithm READ hostKeyAlgorithm NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString hostKeyFingerprint READ hostKeyFingerprint NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(bool hostKeyChangedWarning READ hostKeyChangedWarning NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString defaultPrivateKeyPath READ defaultPrivateKeyPath CONSTANT)
    Q_PROPERTY(QVariantList hostProfiles READ hostProfiles NOTIFY hostProfilesChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    explicit AppController(QString profileStorePath, QObject *parent = nullptr);
    ~AppController() override;

    AppController(const AppController &) = delete;
    AppController &operator=(const AppController &) = delete;

    void attachTerminal(ui::TerminalItem *terminal);
    void shutdown() noexcept;

    [[nodiscard]] bool sshActive() const noexcept;
    [[nodiscard]] bool hostKeyPromptVisible() const noexcept;
    [[nodiscard]] QString hostKeyAlgorithm() const;
    [[nodiscard]] QString hostKeyFingerprint() const;
    [[nodiscard]] bool hostKeyChangedWarning() const noexcept;
    [[nodiscard]] QString defaultPrivateKeyPath() const;
    [[nodiscard]] QVariantList hostProfiles() const;

    Q_INVOKABLE void startLocalTerminal();
    Q_INVOKABLE bool connectPrivateKey(const QString &host, int port, const QString &username,
                                       const QString &privateKeyPath, const QString &passphrase);
    Q_INVOKABLE bool connectPassword(const QString &host, int port, const QString &username, const QString &password);
    Q_INVOKABLE bool saveHostProfile(const QString &id, const QString &name, const QString &host, int port,
                                     const QString &username, const QString &authentication,
                                     const QString &privateKeyPath, bool privateKeyPassphraseRequired);
    Q_INVOKABLE bool deleteHostProfile(const QString &id);
    Q_INVOKABLE bool connectHostProfile(const QString &id, const QString &secret);
    Q_INVOKABLE void acceptHostKey(bool remember);
    Q_INVOKABLE void rejectHostKey();

signals:
    void sshActiveChanged();
    void hostKeyPromptChanged();
    void hostProfilesChanged();

private:
    enum class ActiveSession : std::uint8_t
    {
        None,
        Local,
        Ssh,
    };

    void connectTerminalSignals();
    void queueInput(const QByteArray &bytes);
    void queuePaste(const QByteArray &bytes);
    void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void setHostKeyPrompt(QString algorithm, QString fingerprint, bool changed);
    void clearHostKeyPrompt();
    void loadHostProfiles();
    [[nodiscard]] bool startSshConnection(ssh::SshConnectionRequest request);

    ui::TerminalItem *m_terminal = nullptr;
    terminal::LocalTerminalSession m_localSession;
    ssh::SshTerminalSession m_sshSession;
    ssh::SshProfileStore m_profileStore;
    std::vector<ssh::SshProfile> m_profiles;
    ActiveSession m_activeSession = ActiveSession::None;
    QString m_hostKeyAlgorithm;
    QString m_hostKeyFingerprint;
    bool m_hostKeyPromptVisible = false;
    bool m_hostKeyChangedWarning = false;
};

} // namespace ztermy
