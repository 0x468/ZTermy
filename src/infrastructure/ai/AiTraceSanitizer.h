#pragma once

#include <QJsonValue>

namespace ztermy::ai
{

// Keeps provider traces useful without duplicating multi-megabyte image payloads.
[[nodiscard]] QJsonValue sanitizeAiTraceValue(const QJsonValue &value);

} // namespace ztermy::ai
