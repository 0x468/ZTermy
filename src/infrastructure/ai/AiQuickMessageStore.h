#pragma once

#include "domain/ai/AiQuickMessage.h"

#include <QString>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ztermy::ai
{

inline constexpr qsizetype maximumAiQuickMessageCount = 200;

enum class AiQuickMessageStoreError : std::uint8_t
{
    invalidPath,
    ioError,
    invalidFormat,
    unsupportedVersion,
};

class AiQuickMessageStore final
{
public:
    explicit AiQuickMessageStore(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept;
    [[nodiscard]] std::expected<std::vector<AiQuickMessage>, AiQuickMessageStoreError> load() const;
    [[nodiscard]] std::expected<void, AiQuickMessageStoreError> save(std::span<const AiQuickMessage> messages) const;

private:
    QString m_filePath;
};

} // namespace ztermy::ai
