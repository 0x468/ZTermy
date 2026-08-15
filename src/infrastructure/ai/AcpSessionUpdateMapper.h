#pragma once

#include "domain/ai/AiProviderTypes.h"
#include "infrastructure/ai/AcpProtocol.h"

#include <QHash>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace ztermy::ai
{

struct AcpUsageUpdate final
{
    std::uint64_t used = 0;
    std::uint64_t size = 0;
    std::optional<double> costAmount;
    QString costCurrency;

    [[nodiscard]] friend bool operator==(const AcpUsageUpdate &, const AcpUsageUpdate &) = default;
};

struct AcpMappedSessionUpdate final
{
    std::vector<AiStreamEvent> streamEvents;
    std::optional<AiToolActivity> toolActivity;
    std::optional<AcpUsageUpdate> usage;
};

class AcpSessionUpdateMapper final
{
public:
    [[nodiscard]] std::expected<AcpMappedSessionUpdate, QString> map(const AcpMessage &message);
    void reset() noexcept;

private:
    [[nodiscard]] std::expected<std::optional<AiToolActivity>, QString> mapToolCall(const QJsonObject &update,
                                                                                    bool initial);

    QHash<QString, AiToolActivity> m_tools;
};

} // namespace ztermy::ai
