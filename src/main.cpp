#include "application/terminal/LocalTerminalSession.h"
#include "core/logging/Logging.h"
#include "platform/windows/NativeWindow.h"
#include "ui/terminal/TerminalItem.h"

#include <QGuiApplication>
#include <QLoggingCategory>
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
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qRegisterMetaType<ztermy::terminal::TerminalSnapshotPtr>();
    qmlRegisterType<ztermy::ui::TerminalItem>("Ztermy.Terminal", 1, 0, "TerminalView");

    ztermy::NativeWindow window;
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

    ztermy::terminal::LocalTerminalSession terminalSession;
    QObject::connect(&terminalSession, &ztermy::terminal::LocalTerminalSession::snapshotReady, terminalItem,
                     &ztermy::ui::TerminalItem::setSnapshot);
    QObject::connect(&terminalSession, &ztermy::terminal::LocalTerminalSession::statusChanged, terminalItem,
                     &ztermy::ui::TerminalItem::setStatusText);
    QObject::connect(&terminalSession, &ztermy::terminal::LocalTerminalSession::clipboardTextReady, terminalItem,
                     &ztermy::ui::TerminalItem::setClipboardText);
    QObject::connect(terminalItem, &ztermy::ui::TerminalItem::inputGenerated, &terminalSession,
                     &ztermy::terminal::LocalTerminalSession::queueInput);
    QObject::connect(terminalItem, &ztermy::ui::TerminalItem::pasteRequested, &terminalSession,
                     &ztermy::terminal::LocalTerminalSession::queuePaste);
    QObject::connect(terminalItem, &ztermy::ui::TerminalItem::scrollRequested, &terminalSession,
                     &ztermy::terminal::LocalTerminalSession::requestScroll);
    QObject::connect(terminalItem, &ztermy::ui::TerminalItem::selectionRequested, &terminalSession,
                     &ztermy::terminal::LocalTerminalSession::requestSelection);
    QObject::connect(terminalItem, &ztermy::ui::TerminalItem::clearSelectionRequested, &terminalSession,
                     &ztermy::terminal::LocalTerminalSession::clearSelection);
    QObject::connect(terminalItem, &ztermy::ui::TerminalItem::copyRequested, &terminalSession,
                     &ztermy::terminal::LocalTerminalSession::copySelection);
    QObject::connect(terminalItem, &ztermy::ui::TerminalItem::sizeRequested, &terminalSession,
                     &ztermy::terminal::LocalTerminalSession::requestResize);

    if (const std::error_code startError = terminalSession.start({.columns = 100, .rows = 30}))
    {
        terminalItem->setStatusText(
            QStringLiteral("Unable to start local terminal: %1").arg(QString::fromStdString(startError.message())));
        qCCritical(applicationLog) << "Unable to start local terminal:" << QString::fromStdString(startError.message());
    }
    terminalItem->requestCurrentSize();

    window.show();
    return application.exec();
}
