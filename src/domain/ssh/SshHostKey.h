#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ztermy::ssh
{

enum class HostKeyAlgorithm : std::uint8_t
{
    Unknown,
    Rsa,
    EcdsaP256,
    EcdsaP384,
    EcdsaP521,
    Ed25519,
};

struct SshEndpoint final
{
    std::string host;
    std::uint16_t port = 22;

    [[nodiscard]] friend bool operator==(const SshEndpoint &, const SshEndpoint &) = default;
};

struct ObservedHostKey final
{
    HostKeyAlgorithm algorithm = HostKeyAlgorithm::Unknown;
    std::vector<std::uint8_t> encodedKey;
    std::array<std::uint8_t, 32> sha256{};

    [[nodiscard]] bool valid() const noexcept;
};

struct KnownHostEntry final
{
    SshEndpoint endpoint;
    HostKeyAlgorithm algorithm = HostKeyAlgorithm::Unknown;
    std::vector<std::uint8_t> encodedKey;
};

enum class HostKeyTrust : std::uint8_t
{
    Trusted,
    Unknown,
    Changed,
    Invalid,
};

[[nodiscard]] std::string sha256Fingerprint(const ObservedHostKey &hostKey);
[[nodiscard]] std::string_view hostKeyAlgorithmName(HostKeyAlgorithm algorithm) noexcept;
[[nodiscard]] HostKeyTrust evaluateHostKeyTrust(const SshEndpoint &endpoint, const ObservedHostKey &observed,
                                                std::span<const KnownHostEntry> knownHosts) noexcept;

} // namespace ztermy::ssh
