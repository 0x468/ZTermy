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
    QString shellId;
    QString windowMode = QStringLiteral("main");
    QString profileId;
    QString host;
    QString username;
    int port = 22;
    QString authentication;
    QString identityFile;
    QString certificateFile;
    QString passwordFile;
    QString remoteDirectory;

    [[nodiscard]] bool hasTarget() const noexcept;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] static QString helpText();

    [[nodiscard]] QByteArray toMessage() const;
    [[nodiscard]] static std::expected<ApplicationLaunchRequest, QString> fromMessage(const QByteArray &message);
    [[nodiscard]] static std::expected<ApplicationLaunchRequest, QString> fromArguments(const QStringList &arguments);
};
} // namespace ztermy::config
