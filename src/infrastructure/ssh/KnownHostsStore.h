#pragma once

#include "domain/ssh/SshHostKey.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ztermy::ssh
{

enum class KnownHostsStoreError : std::uint8_t
{
    InvalidPath,
    IoError,
    InvalidFormat,
    UnsupportedVersion,
};

struct KnownHostsMergeResult final
{
    std::vector<KnownHostEntry> entries;
    std::size_t added = 0;
    std::size_t duplicates = 0;
    std::size_t conflicts = 0;
};

class KnownHostsStore final
{
public:
    explicit KnownHostsStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<std::vector<KnownHostEntry>, KnownHostsStoreError> load() const;
    [[nodiscard]] std::expected<void, KnownHostsStoreError> save(std::span<const KnownHostEntry> entries) const;
    [[nodiscard]] std::expected<KnownHostsMergeResult, KnownHostsStoreError>
    mergeMissing(std::span<const KnownHostEntry> entries) const;
    [[nodiscard]] std::expected<std::vector<KnownHostEntry>, KnownHostsStoreError>
    remove(const SshEndpoint &endpoint, HostKeyAlgorithm algorithm) const;
    [[nodiscard]] std::expected<void, KnownHostsStoreError> clear() const;

private:
    [[nodiscard]] std::expected<std::vector<KnownHostEntry>, KnownHostsStoreError> loadUnlocked() const;
    [[nodiscard]] std::expected<void, KnownHostsStoreError> saveUnlocked(std::span<const KnownHostEntry> entries) const;

    QString m_filePath;
};

} // namespace ztermy::ssh
