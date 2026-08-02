#pragma once

#include "core/security/CredentialVault.h"

#include <QByteArray>
#include <QString>

#include <expected>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace ztermy::security
{

class PortableCredentialVault final : public CredentialVault
{
public:
    explicit PortableCredentialVault(QString filePath);
    ~PortableCredentialVault() override;

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool locked() const noexcept;
    [[nodiscard]] std::expected<void, CredentialVaultError> initialize(SensitiveByteArray masterPassword);
    [[nodiscard]] std::expected<void, CredentialVaultError> unlock(SensitiveByteArray masterPassword);
    [[nodiscard]] std::expected<void, CredentialVaultError> changeMasterPassword(SensitiveByteArray masterPassword);
    void lock() noexcept;

    [[nodiscard]] std::string_view backendId() const noexcept override;
    [[nodiscard]] bool persistent() const noexcept override;
    [[nodiscard]] std::expected<void, CredentialVaultError> store(const CredentialKey &key,
                                                                  SensitiveByteArray secret) override;
    [[nodiscard]] std::expected<SensitiveByteArray, CredentialVaultError> read(const CredentialKey &key) const override;
    [[nodiscard]] std::expected<void, CredentialVaultError> remove(const CredentialKey &key) override;
    [[nodiscard]] std::expected<std::vector<CredentialKey>, CredentialVaultError> listKeys() const override;

private:
    struct Record final
    {
        CredentialKey key;
        SensitiveByteArray secret;
    };

    struct Envelope final
    {
        QByteArray salt;
        QByteArray nonce;
        QByteArray tag;
        QByteArray ciphertext;
    };

    [[nodiscard]] std::expected<Envelope, CredentialVaultError> loadEnvelope() const;
    [[nodiscard]] std::expected<std::vector<Record>, CredentialVaultError>
    loadRecords(std::string_view key, const QByteArray &expectedSalt) const;
    [[nodiscard]] std::expected<void, CredentialVaultError>
    writeRecords(std::span<const Record> records, std::string_view key, const QByteArray &salt) const;

    QString m_filePath;
    mutable std::recursive_mutex m_mutex;
    QByteArray m_salt;
    SensitiveByteArray m_key;
};

} // namespace ztermy::security
