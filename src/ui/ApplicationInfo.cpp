#include "ui/ApplicationInfo.h"

#include "ztermy_version.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSysInfo>

namespace ztermy::ui
{
QString ApplicationInfo::platformDescription() const
{
    const QString product = QSysInfo::prettyProductName();
    const QString kernel = QSysInfo::kernelVersion();
    return kernel.isEmpty() ? product : QStringLiteral("%1 (%2)").arg(product, kernel);
}

QString ApplicationInfo::buildCommit() const
{
    return QStringLiteral(ZTERMY_BUILD_COMMIT);
}

void ApplicationInfo::copyBuildCommit() const
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(buildCommit());
}
} // namespace ztermy::ui
