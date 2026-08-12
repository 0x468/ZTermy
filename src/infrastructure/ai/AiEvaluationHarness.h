#pragma once

#include <QJsonObject>
#include <QString>

#include <expected>

namespace ztermy::ai
{

class AiEvaluationHarness final
{
public:
    [[nodiscard]] static std::expected<QJsonObject, QString> replay(const QString &corpusPath,
                                                                    const QString &tracePath);
};

} // namespace ztermy::ai
