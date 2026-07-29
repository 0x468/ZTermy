#include "application/AppController.h"
#include "core/config/ApplicationPaths.h"
#include "core/logging/Logging.h"
#include "platform/windows/CrashDiagnostics.h"
#include "platform/windows/NativeWindow.h"
#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QMetaType>
#include <QQuickItem>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <chrono>
#include <cstdlib>

Q_LOGGING_CATEGORY(applicationLog, "ztermy.application")

namespace
{

void processWindowEventsFor(const std::chrono::milliseconds duration)
{
    QEventLoop eventLoop;
    QTimer::singleShot(duration, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
}

[[nodiscard]] bool runWindowRuntimeSmoke(ztermy::NativeWindow &window)
{
    window.show();
    processWindowEventsFor(std::chrono::milliseconds{250});
    window.showMaximized();
    processWindowEventsFor(std::chrono::milliseconds{500});

    const bool workAreaMatches = window.maximized() && window.maximizedClientMatchesWorkArea();
    window.showNormal();
    processWindowEventsFor(std::chrono::milliseconds{250});
    return workAreaMatches && !window.maximized();
}

[[nodiscard]] bool captureLayout(ztermy::NativeWindow &window, const QString &outputDirectory, const QString &name)
{
    const QString path = QDir(outputDirectory).filePath(name + QStringLiteral(".png"));
    const bool saved = window.grabWindow().save(path);
    qCInfo(applicationLog) << "UI layout capture" << path << "saved=" << saved;
    return saved;
}

[[nodiscard]] bool verifyUiLayoutBreakpoint(ztermy::NativeWindow &window, const QSize size, const bool compact,
                                            const QString &themeName, const QString &outputDirectory)
{
    window.resize(size);
    processWindowEventsFor(std::chrono::milliseconds{250});

    QQuickItem *rootObject = window.rootObject();
    if (rootObject == nullptr)
    {
        return false;
    }

    rootObject->setProperty("currentPage", QStringLiteral("hosts"));
    processWindowEventsFor(std::chrono::milliseconds{100});
    auto *hostPane = rootObject->findChild<QObject *>(QStringLiteral("hostConnectionPane"));
    auto *hostContent = rootObject->findChild<QObject *>(QStringLiteral("hostContentColumn"));
    auto *hostEditorGrid = rootObject->findChild<QObject *>(QStringLiteral("hostEditorGrid"));
    const QString breakpointName = compact ? QStringLiteral("compact") : QStringLiteral("regular");
    const QString capturePrefix = themeName + QStringLiteral("-") + breakpointName;
    const bool hostCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-hosts"));

    rootObject->setProperty("currentPage", QStringLiteral("settings"));
    processWindowEventsFor(std::chrono::milliseconds{100});
    auto *settingsPane = rootObject->findChild<QObject *>(QStringLiteral("settingsPane"));
    auto *appearanceGrid = rootObject->findChild<QObject *>(QStringLiteral("settingsAppearanceGrid"));
    auto *terminalGrid = rootObject->findChild<QObject *>(QStringLiteral("settingsTerminalGrid"));
    const bool settingsCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-settings"));

    if (hostPane == nullptr || hostContent == nullptr || hostEditorGrid == nullptr || settingsPane == nullptr
        || appearanceGrid == nullptr || terminalGrid == nullptr)
    {
        qCWarning(applicationLog) << "UI layout smoke object lookup failed"
                                  << "hostPane=" << (hostPane != nullptr) << "hostContent=" << (hostContent != nullptr)
                                  << "hostEditorGrid=" << (hostEditorGrid != nullptr)
                                  << "settingsPane=" << (settingsPane != nullptr)
                                  << "appearanceGrid=" << (appearanceGrid != nullptr)
                                  << "terminalGrid=" << (terminalGrid != nullptr);
        return false;
    }

    const qreal hostPaneWidth = hostPane->property("width").toReal();
    const qreal hostContentWidth = hostContent->property("width").toReal();
    const bool hostMatches = hostPane->property("compactLayout").toBool() == compact
                             && hostEditorGrid->property("columns").toInt() == (compact ? 1 : 2)
                             && hostPane->property("profileActionColumns").toInt() == (compact ? 2 : 7)
                             && hostContentWidth > 0.0 && hostContentWidth <= hostPaneWidth;
    const bool settingsMatch = settingsPane->property("compactLayout").toBool() == compact
                               && appearanceGrid->property("columns").toInt() == (compact ? 1 : 2)
                               && terminalGrid->property("columns").toInt() == (compact ? 1 : 2);

    qCInfo(applicationLog) << "UI layout breakpoint check"
                           << "theme=" << themeName << "size=" << size << "compact=" << compact
                           << "hostPaneWidth=" << hostPaneWidth << "hostContentWidth=" << hostContentWidth
                           << "hostMatches=" << hostMatches << "settingsMatch=" << settingsMatch;
    return hostMatches && settingsMatch && hostCaptured && settingsCaptured;
}

[[nodiscard]] bool applyUiLayoutSmokeTheme(ztermy::AppController &controller, const QString &theme)
{
    return controller.saveApplicationSettings(theme, 1.0, QStringLiteral("none"), QStringLiteral("Cascadia Mono"), 14,
                                              QStringLiteral("terminal"), true, false, true);
}

[[nodiscard]] bool runUiLayoutRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                           const QString &outputDirectory)
{
    window.show();
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool darkCompactPassed =
        verifyUiLayoutBreakpoint(window, QSize{500, 360}, true, QStringLiteral("dark"), outputDirectory);
    const bool darkRegularPassed =
        verifyUiLayoutBreakpoint(window, QSize{1120, 800}, false, QStringLiteral("dark"), outputDirectory);
    if (!darkCompactPassed || !darkRegularPassed || !applyUiLayoutSmokeTheme(controller, QStringLiteral("light")))
    {
        return false;
    }

    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool lightCompactPassed =
        verifyUiLayoutBreakpoint(window, QSize{500, 360}, true, QStringLiteral("light"), outputDirectory);
    const bool lightRegularPassed =
        verifyUiLayoutBreakpoint(window, QSize{1120, 800}, false, QStringLiteral("light"), outputDirectory);
    const bool restoredDark = applyUiLayoutSmokeTheme(controller, QStringLiteral("dark"));
    return lightCompactPassed && lightRegularPassed && restoredDark;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setOrganizationName(QStringLiteral("ztermy"));

    const QString executableDirectory = QCoreApplication::applicationDirPath();
    const auto paths = ztermy::config::resolveApplicationPaths(
        QCoreApplication::arguments(), executableDirectory, QDir::currentPath(),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
        QFileInfo(QDir(executableDirectory).filePath(QStringLiteral("portable.flag"))).isFile());
    if (!paths)
    {
        qCritical().noquote() << paths.error();
        return EXIT_FAILURE;
    }
    if (const auto prepared = ztermy::config::prepareApplicationPaths(*paths); !prepared)
    {
        qCritical().noquote() << prepared.error();
        return EXIT_FAILURE;
    }

    ztermy::logging::initialize(paths->logsDirectory);
    ztermy::diagnostics::initialize(paths->crashDirectory);
    qInfo().noquote() << "storageMode=" << ztermy::config::storageModeName(paths->mode)
                      << "dataDirectory=" << paths->dataDirectory;
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qRegisterMetaType<ztermy::terminal::TerminalSnapshotPtr>();

    ztermy::AppController appController(paths->profilesFile, paths->knownHostsFile, paths->settingsFile);
    const bool uiLayoutSmoke = QCoreApplication::arguments().contains(QStringLiteral("--ui-layout-smoke"));
    if (uiLayoutSmoke
        && (!applyUiLayoutSmokeTheme(appController, QStringLiteral("dark"))
            || !appController.saveHostProfile(
                QStringLiteral("ui-layout-smoke-profile"), QStringLiteral("Layout test host"),
                QStringLiteral("192.0.2.10"), 22, QStringLiteral("developer"), QStringLiteral("private-key"),
                QStringLiteral("C:/test/id_ed25519"), false, QStringLiteral("Test fixtures"))))
    {
        qCCritical(applicationLog) << "Could not prepare the responsive UI layout fixture";
        return EXIT_FAILURE;
    }

    ztermy::NativeWindow window;
    QVariantMap initialProperties;
    initialProperties.insert(QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject *>(&appController)));
    if (!window.load(initialProperties))
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
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test")))
    {
        QCoreApplication::processEvents();
        appController.shutdown();
        window.releaseResources();
        qCInfo(applicationLog) << "QML and native-window smoke test completed";
        return EXIT_SUCCESS;
    }
    if (QCoreApplication::arguments().contains(QStringLiteral("--window-runtime-smoke")))
    {
        const bool passed = runWindowRuntimeSmoke(window);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "Maximized work-area runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Maximized work-area runtime smoke test completed";
        return EXIT_SUCCESS;
    }

    if (uiLayoutSmoke)
    {
        const bool passed = runUiLayoutRuntimeSmoke(window, appController, paths->dataDirectory);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "Responsive UI layout runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Responsive UI layout runtime smoke test completed";
        return EXIT_SUCCESS;
    }

    appController.startLocalTerminal();

    window.show();
    const int exitCode = application.exec();

    qCInfo(applicationLog) << "Application event loop stopped; beginning orderly shutdown";
    appController.shutdown();
    window.releaseResources();
    qCInfo(applicationLog) << "Terminal and scene graph resources released";
    return exitCode;
}
