#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <QByteArray>
#include <QJsonObject>

namespace ztermy::ai
{

[[nodiscard]] AiProviderError parseProviderError(const QJsonObject &envelope, AiProviderError fallback);
[[nodiscard]] AiProviderError parseProviderErrorBody(const QByteArray &body, AiProviderError fallback);

} // namespace ztermy::ai
