#include "domain/ssh/SshHostKey.h"

#include <array>
#include <cstddef>
#include <string>

namespace
{

constexpr std::array Base64Alphabet{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
    'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};

[[nodiscard]] std::string base64WithoutPadding(const std::span<const std::uint8_t> bytes)
{
    std::string result;
    result.reserve((bytes.size() * 4 + 2) / 3);

    std::size_t offset = 0;
    while (offset + 3 <= bytes.size())
    {
        const std::uint32_t value = (static_cast<std::uint32_t>(bytes[offset]) << 16)
                                    | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
                                    | static_cast<std::uint32_t>(bytes[offset + 2]);
        result.push_back(Base64Alphabet[(value >> 18) & 0x3f]);
        result.push_back(Base64Alphabet[(value >> 12) & 0x3f]);
        result.push_back(Base64Alphabet[(value >> 6) & 0x3f]);
        result.push_back(Base64Alphabet[value & 0x3f]);
        offset += 3;
    }

    const std::size_t remaining = bytes.size() - offset;
    if (remaining == 1)
    {
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[offset]) << 16;
        result.push_back(Base64Alphabet[(value >> 18) & 0x3f]);
        result.push_back(Base64Alphabet[(value >> 12) & 0x3f]);
    }
    else if (remaining == 2)
    {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(bytes[offset]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8);
        result.push_back(Base64Alphabet[(value >> 18) & 0x3f]);
        result.push_back(Base64Alphabet[(value >> 12) & 0x3f]);
        result.push_back(Base64Alphabet[(value >> 6) & 0x3f]);
    }

    return result;
}

[[nodiscard]] bool malformed(const ztermy::ssh::KnownHostEntry &entry) noexcept
{
    return entry.endpoint.host.empty() || entry.endpoint.port == 0
           || entry.algorithm == ztermy::ssh::HostKeyAlgorithm::Unknown || entry.encodedKey.empty();
}

} // namespace

namespace ztermy::ssh
{

bool ObservedHostKey::valid() const noexcept
{
    return algorithm != HostKeyAlgorithm::Unknown && !encodedKey.empty();
}

std::string sha256Fingerprint(const ObservedHostKey &hostKey)
{
    std::string result = "SHA256:";
    result += base64WithoutPadding(hostKey.sha256);
    return result;
}

std::string_view hostKeyAlgorithmName(const HostKeyAlgorithm algorithm) noexcept
{
    switch (algorithm)
    {
        case HostKeyAlgorithm::Rsa:
            return "RSA";
        case HostKeyAlgorithm::EcdsaP256:
            return "ECDSA P-256";
        case HostKeyAlgorithm::EcdsaP384:
            return "ECDSA P-384";
        case HostKeyAlgorithm::EcdsaP521:
            return "ECDSA P-521";
        case HostKeyAlgorithm::Ed25519:
            return "ED25519";
        case HostKeyAlgorithm::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

std::string_view hostKeyAlgorithmToken(const HostKeyAlgorithm algorithm) noexcept
{
    switch (algorithm)
    {
        case HostKeyAlgorithm::Rsa:
            return "ssh-rsa";
        case HostKeyAlgorithm::EcdsaP256:
            return "ecdsa-sha2-nistp256";
        case HostKeyAlgorithm::EcdsaP384:
            return "ecdsa-sha2-nistp384";
        case HostKeyAlgorithm::EcdsaP521:
            return "ecdsa-sha2-nistp521";
        case HostKeyAlgorithm::Ed25519:
            return "ssh-ed25519";
        case HostKeyAlgorithm::Unknown:
            return {};
    }
    return {};
}

std::optional<HostKeyAlgorithm> parseHostKeyAlgorithm(const std::string_view token) noexcept
{
    if (token == "ssh-rsa")
    {
        return HostKeyAlgorithm::Rsa;
    }
    if (token == "ecdsa-sha2-nistp256")
    {
        return HostKeyAlgorithm::EcdsaP256;
    }
    if (token == "ecdsa-sha2-nistp384")
    {
        return HostKeyAlgorithm::EcdsaP384;
    }
    if (token == "ecdsa-sha2-nistp521")
    {
        return HostKeyAlgorithm::EcdsaP521;
    }
    if (token == "ssh-ed25519")
    {
        return HostKeyAlgorithm::Ed25519;
    }
    return std::nullopt;
}

HostKeyTrust evaluateHostKeyTrust(const SshEndpoint &endpoint, const ObservedHostKey &observed,
                                  const std::span<const KnownHostEntry> knownHosts) noexcept
{
    if (endpoint.host.empty() || endpoint.port == 0 || !observed.valid())
    {
        return HostKeyTrust::Invalid;
    }

    bool endpointFound = false;
    for (const KnownHostEntry &knownHost : knownHosts)
    {
        if (knownHost.endpoint != endpoint)
        {
            continue;
        }

        endpointFound = true;
        if (malformed(knownHost))
        {
            return HostKeyTrust::Invalid;
        }
        if (knownHost.algorithm == observed.algorithm && knownHost.encodedKey == observed.encodedKey)
        {
            return HostKeyTrust::Trusted;
        }
    }

    return endpointFound ? HostKeyTrust::Changed : HostKeyTrust::Unknown;
}

} // namespace ztermy::ssh
