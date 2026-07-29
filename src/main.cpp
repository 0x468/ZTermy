#include "core/logging/Logging.h"
#include "platform/windows/NativeWindow.h"

#include <QGuiApplication>
#include <QQuickStyle>

#include <cstdlib>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setOrganizationName(QStringLiteral("ztermy"));

    ztermy::logging::initialize();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ztermy::NativeWindow window;
    if (!window.load())
    {
        return EXIT_FAILURE;
    }

    window.show();
    return application.exec();
}
