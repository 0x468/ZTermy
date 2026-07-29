#include "application/AppController.h"
#include "core/logging/Logging.h"
#include "platform/windows/CrashDiagnostics.h"
#include "platform/windows/NativeWindow.h"
#include "ui/terminal/TerminalItem.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtQml/qqml.h>

#include <cstdlib>

Q_LOGGING_CATEGORY(applicationLog, "ztermy.application")

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setOrganizationName(QStringLiteral("ztermy"));

    ztermy::logging::initialize();
    ztermy::diagnostics::initialize();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qRegisterMetaType<ztermy::terminal::TerminalSnapshotPtr>();
    qmlRegisterType<ztermy::ui::TerminalItem>("Ztermy.Terminal", 1, 0, "TerminalView");

    ztermy::AppController appController;
    ztermy::NativeWindow window;
    window.rootContext()->setContextProperty(QStringLiteral("appController"), &appController);
    if (!window.load())
    {
        return EXIT_FAILURE;
    }

    auto *terminalItem = window.findChild<ztermy::ui::TerminalItem *>(QStringLiteral("terminalViewport"));
    if (terminalItem == nullptr)
    {
        qCCritical(applicationLog) << "Terminal viewport was not created";
        return EXIT_FAILURE;
    }

    appController.attachTerminal(terminalItem);
    appController.startLocalTerminal();

    window.show();
    const int exitCode = application.exec();

    qCInfo(applicationLog) << "Application event loop stopped; beginning orderly shutdown";
    appController.shutdown();
    window.releaseResources();
    qCInfo(applicationLog) << "Terminal and scene graph resources released";
    return exitCode;
}
