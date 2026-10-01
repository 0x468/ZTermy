#pragma once

#include <QByteArray>
#include <QStringList>
#include <expected>

namespace ztermy::config
{
// A directory is data, never a command to inject into a shell.
struct ApplicationLaunchRequest final
{
    QString directory;
    bool background = false;

    [[nodiscard]] QByteArray toMessage() const;
    [[nodiscard]] static std::expected<ApplicationLaunchRequest, QString> fromMessage(const QByteArray &message);
    [[nodiscard]] static std::expected<ApplicationLaunchRequest, QString> fromArguments(const QStringList &arguments);
};
} // namespace ztermy::config
