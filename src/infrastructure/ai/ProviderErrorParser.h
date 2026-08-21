#pragma once

#include "domain/ai/AiProviderTypes.h"

#include <QByteArray>
#include <QJsonObject>

#include <string>

class QNetworkReply;

namespace ztermy::ai
{

[[nodiscard]] AiProviderError parseProviderError(const QJsonObject &envelope, AiProviderError fallback);
[[nodiscard]] AiProviderError parseProviderErrorBody(const QByteArray &body, AiProviderError fallback);
[[nodiscard]] AiProviderError parseProviderReplyError(const QNetworkReply &reply, const QByteArray &body,
                                                      std::string fallbackMessage = {});

} // namespace ztermy::ai
