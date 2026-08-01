#include "infrastructure/security/PortableCredentialVault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace
{

using ztermy::security::CredentialVaultError;
using ztermy::security::SensitiveByteArray;

constexpr std::array<char, 8> VaultMagic{'Z', 'T', 'E', 'R', 'M', 'Y', 'V', '1'};
constexpr std::array<char, 4> PayloadMagic{'Z', 'V', 'P', 'D'};
constexpr std::uint32_t VaultVersion = 1;
constexpr std::uint32_t KdfScrypt = 1;
constexpr std::uint32_t CipherAes256Gcm = 1;
constexpr std::uint64_t ScryptCost = 1ULL << 15U;
constexpr std::uint32_t ScryptBlockSize = 8;
constexpr std::uint32_t ScryptParallelism = 1;
constexpr std::uint64_t ScryptMaximumMemory = 64ULL * 1024ULL * 1024ULL;
constexpr qsizetype SaltSize = 16;
constexpr qsizetype KeySize = 32;
constexpr qsizetype NonceSize = 12;
constexpr qsizetype TagSize = 16;
constexpr qsizetype MinimumMasterPasswordBytes = 8;
constexpr qint64 MaximumVaultFileSize = 16LL * 1024LL * 1024LL;
constexpr std::uint32_t MaximumRecordCount = 4096;
constexpr std::string_view AdditionalAuthenticatedData = "ztermy-portable-credential-vault-v1";

struct CipherContextDeleter final
{
    void operator()(EVP_CIPHER_CTX *context) const noexcept { EVP_CIPHER_CTX_free(context); }
};

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

void clearBytes(QByteArray &bytes) noexcept
{
    volatile char *data = bytes.data();
    for (qsizetype index = 0; index < bytes.size(); ++index)
    {
        data[index] = '\0';
    }
    bytes.clear();
    bytes.squeeze();
}

void appendU32(QByteArray &output, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        output.append(static_cast<char>((value >> shift) & 0xffU));
    }
}

void appendU64(QByteArray &output, const std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        output.append(static_cast<char>((value >> shift) & 0xffU));
    }
}

[[nodiscard]] bool readU32(const QByteArray &input, qsizetype &offset, std::uint32_t &value) noexcept
{
    if (offset < 0 || input.size() - offset < 4)
    {
        return false;
    }
    value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + index])) << (index * 8U);
    }
    offset += 4;
    return true;
}

[[nodiscard]] bool readU64(const QByteArray &input, qsizetype &offset, std::uint64_t &value) noexcept
{
    if (offset < 0 || input.size() - offset < 8)
    {
        return false;
    }
    value = 0;
    for (unsigned index = 0; index < 8; ++index)
    {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(input[offset + index])) << (index * 8U);
    }
    offset += 8;
    return true;
}

[[nodiscard]] std::expected<QByteArray, CredentialVaultError> randomBytes(const qsizetype size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), static_cast<int>(bytes.size())) != 1)
    {
        clearBytes(bytes);
        return std::unexpected(CredentialVaultError::CryptoError);
    }
    return bytes;
}

[[nodiscard]] std::expected<SensitiveByteArray, CredentialVaultError> deriveKey(const std::string_view password,
                                                                                const QByteArray &salt)
{
    if (password.size() < static_cast<std::size_t>(MinimumMasterPasswordBytes))
    {
        return std::unexpected(CredentialVaultError::WeakMasterPassword);
    }
    QByteArray key(KeySize, Qt::Uninitialized);
    if (EVP_PBE_scrypt(password.data(), password.size(), reinterpret_cast<const unsigned char *>(salt.constData()),
                       static_cast<std::size_t>(salt.size()), ScryptCost, ScryptBlockSize, ScryptParallelism,
                       ScryptMaximumMemory, reinterpret_cast<unsigned char *>(key.data()),
                       static_cast<std::size_t>(key.size()))
        != 1)
    {
        clearBytes(key);
        return std::unexpected(CredentialVaultError::CryptoError);
    }
    return SensitiveByteArray(std::move(key));
}

struct EncryptionResult final
{
    QByteArray ciphertext;
    QByteArray tag;
};

[[nodiscard]] std::expected<EncryptionResult, CredentialVaultError>
encryptPayload(const QByteArray &plaintext, const std::string_view key, const QByteArray &nonce)
{
    if (key.size() != static_cast<std::size_t>(KeySize) || nonce.size() != NonceSize
        || plaintext.size() > std::numeric_limits<int>::max())
    {
        return std::unexpected(CredentialVaultError::CryptoError);
    }

    CipherContext context(EVP_CIPHER_CTX_new());
    QByteArray ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    QByteArray tag(TagSize, Qt::Uninitialized);
    int outputLength = 0;
    int finalLength = 0;
    int aadLength = 0;
    const bool success =
        context != nullptr && EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1
        && EVP_EncryptInit_ex(context.get(), nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.data()),
                              reinterpret_cast<const unsigned char *>(nonce.constData()))
               == 1
        && EVP_EncryptUpdate(context.get(), nullptr, &aadLength,
                             reinterpret_cast<const unsigned char *>(AdditionalAuthenticatedData.data()),
                             static_cast<int>(AdditionalAuthenticatedData.size()))
               == 1
        && EVP_EncryptUpdate(context.get(), reinterpret_cast<unsigned char *>(ciphertext.data()), &outputLength,
                             reinterpret_cast<const unsigned char *>(plaintext.constData()),
                             static_cast<int>(plaintext.size()))
               == 1
        && EVP_EncryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(ciphertext.data()) + outputLength,
                               &finalLength)
               == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) == 1;
    if (!success)
    {
        clearBytes(ciphertext);
        clearBytes(tag);
        return std::unexpected(CredentialVaultError::CryptoError);
    }
    ciphertext.resize(outputLength + finalLength);
    return EncryptionResult{.ciphertext = std::move(ciphertext), .tag = std::move(tag)};
}

[[nodiscard]] std::expected<QByteArray, CredentialVaultError>
decryptPayload(const QByteArray &ciphertext, const QByteArray &tag, const std::string_view key, const QByteArray &nonce)
{
    if (key.size() != static_cast<std::size_t>(KeySize) || nonce.size() != NonceSize || tag.size() != TagSize
        || ciphertext.size() > std::numeric_limits<int>::max())
    {
        return std::unexpected(CredentialVaultError::CorruptData);
    }

    CipherContext context(EVP_CIPHER_CTX_new());
    QByteArray plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    int outputLength = 0;
    int finalLength = 0;
    int aadLength = 0;
    const bool initialized =
        context != nullptr && EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1
        && EVP_DecryptInit_ex(context.get(), nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.data()),
                              reinterpret_cast<const unsigned char *>(nonce.constData()))
               == 1
        && EVP_DecryptUpdate(context.get(), nullptr, &aadLength,
                             reinterpret_cast<const unsigned char *>(AdditionalAuthenticatedData.data()),
                             static_cast<int>(AdditionalAuthenticatedData.size()))
               == 1
        && EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()), &outputLength,
                             reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                             static_cast<int>(ciphertext.size()))
               == 1
        && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()),
                               const_cast<char *>(tag.constData()))
               == 1;
    if (!initialized
        || EVP_DecryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()) + outputLength,
                               &finalLength)
               != 1)
    {
        clearBytes(plaintext);
        return std::unexpected(CredentialVaultError::AuthenticationFailed);
    }
    plaintext.resize(outputLength + finalLength);
    return plaintext;
}

[[nodiscard]] QByteArray serializeEnvelope(const QByteArray &salt, const QByteArray &nonce, const QByteArray &tag,
                                           const QByteArray &ciphertext)
{
    QByteArray data;
    data.reserve(static_cast<qsizetype>(VaultMagic.size()) + 64 + ciphertext.size());
    data.append(VaultMagic.data(), static_cast<qsizetype>(VaultMagic.size()));
    appendU32(data, VaultVersion);
    appendU32(data, KdfScrypt);
    appendU64(data, ScryptCost);
    appendU32(data, ScryptBlockSize);
    appendU32(data, ScryptParallelism);
    appendU32(data, static_cast<std::uint32_t>(salt.size()));
    data.append(salt);
    appendU32(data, CipherAes256Gcm);
    appendU32(data, static_cast<std::uint32_t>(nonce.size()));
    data.append(nonce);
    appendU32(data, static_cast<std::uint32_t>(tag.size()));
    data.append(tag);
    appendU64(data, static_cast<std::uint64_t>(ciphertext.size()));
    data.append(ciphertext);
    return data;
}

} // namespace

namespace ztermy::security
{

PortableCredentialVault::PortableCredentialVault(QString filePath) : m_filePath(std::move(filePath)) {}

PortableCredentialVault::~PortableCredentialVault()
{
    lock();
}

const QString &PortableCredentialVault::filePath() const noexcept
{
    return m_filePath;
}

bool PortableCredentialVault::initialized() const noexcept
{
    return !m_filePath.isEmpty() && QFileInfo::exists(m_filePath);
}

bool PortableCredentialVault::locked() const noexcept
{
    return m_key.empty();
}

std::expected<void, CredentialVaultError> PortableCredentialVault::initialize(SensitiveByteArray masterPassword)
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(CredentialVaultError::IoError);
    }
    if (initialized())
    {
        return std::unexpected(CredentialVaultError::AlreadyInitialized);
    }
    auto salt = randomBytes(SaltSize);
    if (!salt)
    {
        return std::unexpected(salt.error());
    }
    auto key = deriveKey(masterPassword.view(), *salt);
    if (!key)
    {
        return std::unexpected(key.error());
    }
    const std::vector<Record> records;
    if (auto written = writeRecords(records, key->view(), *salt); !written)
    {
        return written;
    }
    m_salt = *salt;
    m_key = std::move(*key);
    return {};
}

std::expected<void, CredentialVaultError> PortableCredentialVault::unlock(SensitiveByteArray masterPassword)
{
    auto envelope = loadEnvelope();
    if (!envelope)
    {
        return std::unexpected(envelope.error());
    }
    auto key = deriveKey(masterPassword.view(), envelope->salt);
    if (!key)
    {
        return std::unexpected(key.error());
    }
    auto records = loadRecords(key->view(), envelope->salt);
    if (!records)
    {
        return std::unexpected(records.error());
    }
    m_salt = envelope->salt;
    m_key = std::move(*key);
    return {};
}

std::expected<void, CredentialVaultError>
PortableCredentialVault::changeMasterPassword(SensitiveByteArray masterPassword)
{
    if (locked())
    {
        return std::unexpected(CredentialVaultError::Locked);
    }
    auto records = loadRecords(m_key.view(), m_salt);
    if (!records)
    {
        return std::unexpected(records.error());
    }
    auto salt = randomBytes(SaltSize);
    if (!salt)
    {
        return std::unexpected(salt.error());
    }
    auto key = deriveKey(masterPassword.view(), *salt);
    if (!key)
    {
        return std::unexpected(key.error());
    }
    if (auto written = writeRecords(*records, key->view(), *salt); !written)
    {
        return written;
    }
    m_salt = *salt;
    m_key = std::move(*key);
    return {};
}

void PortableCredentialVault::lock() noexcept
{
    clearBytes(m_salt);
    m_key.clear();
}

std::string_view PortableCredentialVault::backendId() const noexcept
{
    return "portable";
}

bool PortableCredentialVault::persistent() const noexcept
{
    return true;
}

std::expected<void, CredentialVaultError> PortableCredentialVault::store(const CredentialKey &key,
                                                                         SensitiveByteArray secret)
{
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    if (secret.empty())
    {
        return std::unexpected(CredentialVaultError::EmptySecret);
    }
    if (secret.view().size() > MaximumCredentialSecretSize)
    {
        return std::unexpected(CredentialVaultError::SecretTooLarge);
    }
    if (locked())
    {
        return std::unexpected(CredentialVaultError::Locked);
    }
    auto records = loadRecords(m_key.view(), m_salt);
    if (!records)
    {
        return std::unexpected(records.error());
    }
    const auto existing = std::ranges::find_if(*records, [&key](const Record &record) {
        return record.key == key;
    });
    if (existing == records->end())
    {
        records->push_back(Record{.key = key, .secret = std::move(secret)});
    }
    else
    {
        existing->secret = std::move(secret);
    }
    return writeRecords(*records, m_key.view(), m_salt);
}

std::expected<SensitiveByteArray, CredentialVaultError> PortableCredentialVault::read(const CredentialKey &key) const
{
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    if (locked())
    {
        return std::unexpected(CredentialVaultError::Locked);
    }
    auto records = loadRecords(m_key.view(), m_salt);
    if (!records)
    {
        return std::unexpected(records.error());
    }
    const auto existing = std::ranges::find_if(*records, [&key](const Record &record) {
        return record.key == key;
    });
    if (existing == records->end())
    {
        return std::unexpected(CredentialVaultError::NotFound);
    }
    const std::string_view bytes = existing->secret.view();
    return SensitiveByteArray(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())));
}

std::expected<void, CredentialVaultError> PortableCredentialVault::remove(const CredentialKey &key)
{
    if (!validCredentialKey(key))
    {
        return std::unexpected(CredentialVaultError::InvalidKey);
    }
    if (locked())
    {
        return std::unexpected(CredentialVaultError::Locked);
    }
    auto records = loadRecords(m_key.view(), m_salt);
    if (!records)
    {
        return std::unexpected(records.error());
    }
    const auto existing = std::ranges::find_if(*records, [&key](const Record &record) {
        return record.key == key;
    });
    if (existing == records->end())
    {
        return std::unexpected(CredentialVaultError::NotFound);
    }
    records->erase(existing);
    return writeRecords(*records, m_key.view(), m_salt);
}

std::expected<std::vector<CredentialKey>, CredentialVaultError> PortableCredentialVault::listKeys() const
{
    if (locked())
    {
        return std::unexpected(CredentialVaultError::Locked);
    }
    auto records = loadRecords(m_key.view(), m_salt);
    if (!records)
    {
        return std::unexpected(records.error());
    }
    std::vector<CredentialKey> keys;
    keys.reserve(records->size());
    for (const Record &record : *records)
    {
        keys.push_back(record.key);
    }
    return keys;
}

std::expected<PortableCredentialVault::Envelope, CredentialVaultError> PortableCredentialVault::loadEnvelope() const
{
    if (m_filePath.isEmpty())
    {
        return std::unexpected(CredentialVaultError::IoError);
    }
    QFile file(m_filePath);
    if (!file.exists())
    {
        return std::unexpected(CredentialVaultError::NotFound);
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > MaximumVaultFileSize)
    {
        return std::unexpected(file.size() > MaximumVaultFileSize ? CredentialVaultError::CorruptData
                                                                  : CredentialVaultError::IoError);
    }
    const QByteArray data = file.readAll();
    qsizetype offset = 0;
    if (data.size() < static_cast<qsizetype>(VaultMagic.size())
        || std::memcmp(data.constData(), VaultMagic.data(), VaultMagic.size()) != 0)
    {
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    offset += static_cast<qsizetype>(VaultMagic.size());

    std::uint32_t version = 0;
    std::uint32_t kdf = 0;
    std::uint64_t cost = 0;
    std::uint32_t blockSize = 0;
    std::uint32_t parallelism = 0;
    std::uint32_t saltSize = 0;
    if (!readU32(data, offset, version) || !readU32(data, offset, kdf) || !readU64(data, offset, cost)
        || !readU32(data, offset, blockSize) || !readU32(data, offset, parallelism) || !readU32(data, offset, saltSize))
    {
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    if (version != VaultVersion)
    {
        return std::unexpected(CredentialVaultError::UnsupportedVersion);
    }
    if (kdf != KdfScrypt || cost != ScryptCost || blockSize != ScryptBlockSize || parallelism != ScryptParallelism
        || saltSize != static_cast<std::uint32_t>(SaltSize) || data.size() - offset < static_cast<qsizetype>(saltSize))
    {
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    QByteArray salt = data.mid(offset, static_cast<qsizetype>(saltSize));
    offset += static_cast<qsizetype>(saltSize);

    std::uint32_t cipher = 0;
    std::uint32_t nonceSize = 0;
    if (!readU32(data, offset, cipher) || !readU32(data, offset, nonceSize) || cipher != CipherAes256Gcm
        || nonceSize != static_cast<std::uint32_t>(NonceSize)
        || data.size() - offset < static_cast<qsizetype>(nonceSize))
    {
        clearBytes(salt);
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    QByteArray nonce = data.mid(offset, static_cast<qsizetype>(nonceSize));
    offset += static_cast<qsizetype>(nonceSize);

    std::uint32_t tagSize = 0;
    if (!readU32(data, offset, tagSize) || tagSize != static_cast<std::uint32_t>(TagSize)
        || data.size() - offset < static_cast<qsizetype>(tagSize))
    {
        clearBytes(salt);
        clearBytes(nonce);
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    QByteArray tag = data.mid(offset, static_cast<qsizetype>(tagSize));
    offset += static_cast<qsizetype>(tagSize);

    std::uint64_t ciphertextSize = 0;
    if (!readU64(data, offset, ciphertextSize) || ciphertextSize > static_cast<std::uint64_t>(MaximumVaultFileSize)
        || std::cmp_not_equal(ciphertextSize, data.size() - offset))
    {
        clearBytes(salt);
        clearBytes(nonce);
        clearBytes(tag);
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    return Envelope{.salt = std::move(salt),
                    .nonce = std::move(nonce),
                    .tag = std::move(tag),
                    .ciphertext = data.mid(offset)};
}

std::expected<std::vector<PortableCredentialVault::Record>, CredentialVaultError>
PortableCredentialVault::loadRecords(const std::string_view key, const QByteArray &expectedSalt) const
{
    auto envelope = loadEnvelope();
    if (!envelope)
    {
        return std::unexpected(envelope.error());
    }
    if (envelope->salt != expectedSalt)
    {
        return std::unexpected(CredentialVaultError::AuthenticationFailed);
    }
    auto plaintext = decryptPayload(envelope->ciphertext, envelope->tag, key, envelope->nonce);
    if (!plaintext)
    {
        return std::unexpected(plaintext.error());
    }

    qsizetype offset = 0;
    if (plaintext->size() < static_cast<qsizetype>(PayloadMagic.size())
        || std::memcmp(plaintext->constData(), PayloadMagic.data(), PayloadMagic.size()) != 0)
    {
        clearBytes(*plaintext);
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    offset += static_cast<qsizetype>(PayloadMagic.size());
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!readU32(*plaintext, offset, version) || version != VaultVersion || !readU32(*plaintext, offset, count)
        || count > MaximumRecordCount)
    {
        clearBytes(*plaintext);
        return std::unexpected(version != VaultVersion ? CredentialVaultError::UnsupportedVersion
                                                       : CredentialVaultError::CorruptData);
    }

    std::vector<Record> records;
    records.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t idSize = 0;
        if (!readU32(*plaintext, offset, idSize) || idSize == 0 || idSize > 128
            || plaintext->size() - offset < static_cast<qsizetype>(idSize) + 1)
        {
            clearBytes(*plaintext);
            return std::unexpected(CredentialVaultError::CorruptData);
        }
        CredentialKey recordKey;
        recordKey.profileId.assign(plaintext->constData() + offset, idSize);
        offset += static_cast<qsizetype>(idSize);
        const auto kind = static_cast<CredentialKind>(static_cast<unsigned char>((*plaintext)[offset++]));
        recordKey.kind = kind;
        std::uint32_t secretSize = 0;
        if (!validCredentialKey(recordKey) || credentialKindToken(kind).empty()
            || !readU32(*plaintext, offset, secretSize) || secretSize == 0 || secretSize > MaximumCredentialSecretSize
            || plaintext->size() - offset < static_cast<qsizetype>(secretSize)
            || std::ranges::any_of(
                records,
                [&recordKey](const Record &record) {
                    return record.key == recordKey;
                }))
        {
            clearBytes(*plaintext);
            return std::unexpected(CredentialVaultError::CorruptData);
        }
        QByteArray secret(plaintext->constData() + offset, static_cast<qsizetype>(secretSize));
        offset += static_cast<qsizetype>(secretSize);
        records.push_back(Record{.key = std::move(recordKey), .secret = SensitiveByteArray(std::move(secret))});
    }
    const bool complete = offset == plaintext->size();
    clearBytes(*plaintext);
    if (!complete)
    {
        return std::unexpected(CredentialVaultError::CorruptData);
    }
    return records;
}

std::expected<void, CredentialVaultError> PortableCredentialVault::writeRecords(const std::span<const Record> records,
                                                                                const std::string_view key,
                                                                                const QByteArray &salt) const
{
    if (m_filePath.isEmpty() || records.size() > MaximumRecordCount || salt.size() != SaltSize
        || key.size() != static_cast<std::size_t>(KeySize))
    {
        return std::unexpected(CredentialVaultError::CorruptData);
    }

    QByteArray plaintext;
    plaintext.append(PayloadMagic.data(), static_cast<qsizetype>(PayloadMagic.size()));
    appendU32(plaintext, VaultVersion);
    appendU32(plaintext, static_cast<std::uint32_t>(records.size()));
    for (const Record &record : records)
    {
        const std::string_view secret = record.secret.view();
        if (!validCredentialKey(record.key) || secret.empty() || secret.size() > MaximumCredentialSecretSize)
        {
            clearBytes(plaintext);
            return std::unexpected(CredentialVaultError::CorruptData);
        }
        appendU32(plaintext, static_cast<std::uint32_t>(record.key.profileId.size()));
        plaintext.append(record.key.profileId.data(), static_cast<qsizetype>(record.key.profileId.size()));
        plaintext.append(static_cast<char>(record.key.kind));
        appendU32(plaintext, static_cast<std::uint32_t>(secret.size()));
        plaintext.append(secret.data(), static_cast<qsizetype>(secret.size()));
    }

    auto nonce = randomBytes(NonceSize);
    if (!nonce)
    {
        clearBytes(plaintext);
        return std::unexpected(nonce.error());
    }
    auto encrypted = encryptPayload(plaintext, key, *nonce);
    clearBytes(plaintext);
    if (!encrypted)
    {
        return std::unexpected(encrypted.error());
    }
    const Envelope envelope{.salt = salt, .nonce = *nonce, .tag = encrypted->tag, .ciphertext = encrypted->ciphertext};
    const QByteArray data = serializeEnvelope(envelope.salt, envelope.nonce, envelope.tag, envelope.ciphertext);
    const QFileInfo fileInfo(m_filePath);
    if (!fileInfo.absoluteDir().mkpath(QStringLiteral(".")))
    {
        return std::unexpected(CredentialVaultError::IoError);
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        file.cancelWriting();
        return std::unexpected(CredentialVaultError::IoError);
    }
    return {};
}

} // namespace ztermy::security
