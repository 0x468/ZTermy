#pragma once

#include "application/security/CredentialVaultCoordinator.h"
#include "domain/ssh/SshKeychain.h"
#include "domain/ssh/SshProfile.h"

#include <QMetaType>
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <vector>

namespace ztermy::ssh
{

class KeychainController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList keys READ keys NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList identities READ identities NOTIFY catalogChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged)
    Q_PROPERTY(bool keyGeneratorAvailable READ keyGeneratorAvailable CONSTANT)

public:
    KeychainController(QString storePath, QString managedKeyDirectory, SshKeychainCatalog catalog,
                       security::CredentialVaultCoordinator *vaults, QObject *parent = nullptr);
    ~KeychainController() override;

    KeychainController(const KeychainController &) = delete;
    KeychainController &operator=(const KeychainController &) = delete;

    [[nodiscard]] QVariantList keys() const;
    [[nodiscard]] QVariantList identities() const;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString operationError() const;
    [[nodiscard]] bool keyGeneratorAvailable() const;
    [[nodiscard]] const SshKeychainCatalog &catalog() const noexcept;

    void setProfiles(const std::vector<SshProfile> &profiles);
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool generateKey(const QString &label, const QString &type, int bits, const QString &passphrase);
    Q_INVOKABLE bool importKey(const QString &localFileUrl, const QString &label, bool copyIntoKeychain,
                               const QString &passphrase);
    Q_INVOKABLE bool importCertificate(const QString &privateKeyFileUrl, const QString &certificateFileUrl,
                                       const QString &label, bool copyIntoKeychain, const QString &passphrase);
    Q_INVOKABLE bool saveIdentity(const QString &id, const QString &label, const QString &username,
                                  const QString &authentication, const QString &keyId, bool credentialRequired,
                                  const QString &secret, bool rememberSecret);
    Q_INVOKABLE bool removeKey(const QString &id);
    Q_INVOKABLE bool removeIdentity(const QString &id);
    [[nodiscard]] Q_INVOKABLE bool copyText(const QString &value) const;

signals:
    void catalogChanged();
    void stateChanged();

private:
    Q_SIGNAL void operationFinished(SshKeychainCatalog catalog, const QString &error, const QVariantMap &summary);

    [[nodiscard]] bool beginOperation();
    void startTask(std::function<void()> task);
    void applyOperation(SshKeychainCatalog catalog, const QString &error, const QVariantMap &summary);
    void rebuildProjection();

    QString m_storePath;
    QString m_managedKeyDirectory;
    SshKeychainCatalog m_catalog;
    security::CredentialVaultCoordinator *m_vaults = nullptr;
    std::vector<SshProfile> m_profiles;
    QVariantList m_keyValues;
    QVariantList m_identityValues;
    QThreadPool m_worker;
    QString m_operationError;
    bool m_busy = false;
};

} // namespace ztermy::ssh

Q_DECLARE_METATYPE(ztermy::ssh::SshKeychainCatalog)
