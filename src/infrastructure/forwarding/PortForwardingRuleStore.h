#pragma once

#include "domain/forwarding/PortForwardingRule.h"

#include <QString>

#include <expected>
#include <span>
#include <vector>

namespace ztermy::forwarding
{

enum class PortForwardingRuleStoreError : std::uint8_t
{
    InvalidPath,
    Io,
    InvalidDocument,
    UnsupportedVersion,
};

class PortForwardingRuleStore final
{
public:
    explicit PortForwardingRuleStore(QString filePath);

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool lastLoadRecoveredFromBackup() const noexcept;
    [[nodiscard]] std::expected<std::vector<PortForwardingRule>, PortForwardingRuleStoreError> load() const;
    [[nodiscard]] std::expected<void, PortForwardingRuleStoreError>
    save(std::span<const PortForwardingRule> rules) const;

private:
    QString m_filePath;
    mutable bool m_lastLoadRecoveredFromBackup = false;
};

} // namespace ztermy::forwarding
