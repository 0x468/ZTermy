#pragma once

#include "application/terminal/LocalTerminalSession.h"

#include <QList>
#include <QString>

#include <optional>

namespace ztermy::terminal
{

struct LocalShellProfile final
{
    QString id;
    QString name;
    QString executable;
    QStringList arguments;
    bool powerShellIntegration = false;
    bool available = false;
};

class WindowsLocalShellCatalog final
{
public:
    [[nodiscard]] static QList<LocalShellProfile> detect();
    [[nodiscard]] static std::optional<LocalShellProfile> resolve(const QList<LocalShellProfile> &profiles,
                                                                  const QString &preference);
    [[nodiscard]] static LocalTerminalLaunchSpec launchSpec(const LocalShellProfile &profile,
                                                            const QString &workingDirectory = {});
};

} // namespace ztermy::terminal
