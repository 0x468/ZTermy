#include "application/AppController.h"
#include "core/config/ApplicationPaths.h"
#include "core/logging/Logging.h"
#include "platform/windows/CrashDiagnostics.h"
#include "platform/windows/NativeWindow.h"
#include "ui/terminal/TerminalItem.h"

#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QMetaType>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <dwmapi.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <utility>

Q_LOGGING_CATEGORY(applicationLog, "ztermy.application")

QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleKeyEvent(QWindow *window, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers,
                                    const QString &text = {}, bool autorepeat = false, ushort count = 1);
QT_END_NAMESPACE

namespace
{

void processWindowEventsFor(const std::chrono::milliseconds duration)
{
    QEventLoop eventLoop;
    QTimer::singleShot(duration, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();
}

template <typename Predicate>
[[nodiscard]] bool processWindowEventsUntil(Predicate predicate, const std::chrono::milliseconds timeout)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < timeout.count())
    {
        if (predicate())
        {
            return true;
        }
        processWindowEventsFor(std::chrono::milliseconds{25});
    }
    return predicate();
}

[[nodiscard]] bool runWindowRuntimeSmoke(ztermy::NativeWindow &window)
{
    window.show();
    if (!processWindowEventsUntil(
            [&window]() {
                return window.isVisible();
            },
            std::chrono::seconds{2}))
    {
        qCWarning(applicationLog) << "Window runtime smoke did not become visible";
        return false;
    }

    window.showMaximized();
    const bool workAreaMatches = processWindowEventsUntil(
        [&window]() {
            return window.maximized() && window.maximizedClientMatchesWorkArea();
        },
        std::chrono::seconds{3});
    if (!workAreaMatches)
    {
        qCWarning(applicationLog) << "Window runtime smoke did not reach the maximized work area"
                                  << "maximized=" << window.maximized()
                                  << "workAreaMatches=" << window.maximizedClientMatchesWorkArea();
    }

    window.showNormal();
    const bool restored = processWindowEventsUntil(
        [&window]() {
            return !window.maximized();
        },
        std::chrono::seconds{2});
    return workAreaMatches && restored;
}

struct ResizeHitRuntimeCase
{
    const char *name;
    QPoint clientPoint;
    LRESULT expectedHit;
    LPCWSTR expectedCursor;
};

[[nodiscard]] LPARAM screenPointParameter(const HWND windowHandle, const QPoint clientPoint)
{
    POINT screenPoint{.x = clientPoint.x(), .y = clientPoint.y()};
    if (ClientToScreen(windowHandle, &screenPoint) == FALSE)
    {
        return 0;
    }
    return MAKELPARAM(screenPoint.x, screenPoint.y);
}

[[nodiscard]] bool isResizeHit(const LRESULT hit) noexcept
{
    return hit == HTLEFT || hit == HTRIGHT || hit == HTTOP || hit == HTBOTTOM || hit == HTTOPLEFT || hit == HTTOPRIGHT
           || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT;
}

[[nodiscard]] bool runWindowResizeRuntimeSmoke(ztermy::NativeWindow &window)
{
    window.resize(QSize{1120, 800});
    window.show();
    window.requestActivate();
    processWindowEventsFor(std::chrono::milliseconds{250});

    const auto windowHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    RECT clientRect{};
    if (GetClientRect(windowHandle, &clientRect) == FALSE)
    {
        qCWarning(applicationLog) << "Window resize smoke could not read the client rectangle";
        return false;
    }
    const int right = clientRect.right - 2;
    const int bottom = clientRect.bottom - 2;
    const int horizontalMiddle = clientRect.right / 2;
    const int verticalMiddle = clientRect.bottom / 2;
    const std::array<ResizeHitRuntimeCase, 8> cases{
        ResizeHitRuntimeCase{.name = "left",
                             .clientPoint = QPoint{1, verticalMiddle},
                             .expectedHit = HTLEFT,
                             .expectedCursor = IDC_SIZEWE},
        ResizeHitRuntimeCase{.name = "right",
                             .clientPoint = QPoint{right, verticalMiddle},
                             .expectedHit = HTRIGHT,
                             .expectedCursor = IDC_SIZEWE},
        ResizeHitRuntimeCase{.name = "top",
                             .clientPoint = QPoint{horizontalMiddle, 1},
                             .expectedHit = HTTOP,
                             .expectedCursor = IDC_SIZENS},
        ResizeHitRuntimeCase{.name = "bottom",
                             .clientPoint = QPoint{horizontalMiddle, bottom},
                             .expectedHit = HTBOTTOM,
                             .expectedCursor = IDC_SIZENS},
        ResizeHitRuntimeCase{.name = "top-left",
                             .clientPoint = QPoint{1, 1},
                             .expectedHit = HTTOPLEFT,
                             .expectedCursor = IDC_SIZENWSE},
        ResizeHitRuntimeCase{.name = "top-right",
                             .clientPoint = QPoint{right, 1},
                             .expectedHit = HTTOPRIGHT,
                             .expectedCursor = IDC_SIZENESW},
        ResizeHitRuntimeCase{.name = "bottom-left",
                             .clientPoint = QPoint{1, bottom},
                             .expectedHit = HTBOTTOMLEFT,
                             .expectedCursor = IDC_SIZENESW},
        ResizeHitRuntimeCase{.name = "bottom-right",
                             .clientPoint = QPoint{right, bottom},
                             .expectedHit = HTBOTTOMRIGHT,
                             .expectedCursor = IDC_SIZENWSE},
    };

    bool normalStatePassed = true;
    for (const ResizeHitRuntimeCase &testCase : cases)
    {
        const LPARAM pointParameter = screenPointParameter(windowHandle, testCase.clientPoint);
        const LRESULT actualHit = SendMessageW(windowHandle, WM_NCHITTEST, 0, pointParameter);
        const HCURSOR arrowCursor = LoadCursorW(nullptr, IDC_ARROW);
        const HCURSOR expectedCursor = LoadCursorW(nullptr, testCase.expectedCursor);
        SetCursor(arrowCursor);
        const LRESULT cursorHandled = SendMessageW(windowHandle, WM_SETCURSOR, reinterpret_cast<WPARAM>(windowHandle),
                                                   MAKELPARAM(static_cast<WORD>(testCase.expectedHit), WM_MOUSEMOVE));
        const HCURSOR actualCursor = GetCursor();
        const bool hitMatches = actualHit == testCase.expectedHit;
        const bool cursorMatches = cursorHandled != FALSE && actualCursor == expectedCursor;
        qCInfo(applicationLog) << "Window resize runtime check"
                               << "area=" << testCase.name << "actualHit=" << actualHit
                               << "expectedHit=" << testCase.expectedHit << "hitMatches=" << hitMatches
                               << "cursorHandled=" << cursorHandled << "cursorMatches=" << cursorMatches;
        normalStatePassed = normalStatePassed && hitMatches && cursorMatches;
    }

    window.showMaximized();
    processWindowEventsFor(std::chrono::milliseconds{250});
    bool maximizedStatePassed = window.maximized();
    RECT maximizedClientRect{};
    if (GetClientRect(windowHandle, &maximizedClientRect) == FALSE)
    {
        return false;
    }
    const int maximizedRight = maximizedClientRect.right - 2;
    const int maximizedBottom = maximizedClientRect.bottom - 2;
    const int maximizedHorizontalMiddle = maximizedClientRect.right / 2;
    const int maximizedVerticalMiddle = maximizedClientRect.bottom / 2;
    const std::array<QPoint, 8> maximizedPoints{
        QPoint{1, maximizedVerticalMiddle},
        QPoint{maximizedRight, maximizedVerticalMiddle},
        QPoint{maximizedHorizontalMiddle, 1},
        QPoint{maximizedHorizontalMiddle, maximizedBottom},
        QPoint{1, 1},
        QPoint{maximizedRight, 1},
        QPoint{1, maximizedBottom},
        QPoint{maximizedRight, maximizedBottom},
    };
    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        const ResizeHitRuntimeCase &testCase = cases[index];
        const LPARAM pointParameter = screenPointParameter(windowHandle, maximizedPoints[index]);
        const LRESULT actualHit = SendMessageW(windowHandle, WM_NCHITTEST, 0, pointParameter);
        const bool resizeDisabled = !isResizeHit(actualHit);
        qCInfo(applicationLog) << "Window maximized resize runtime check"
                               << "area=" << testCase.name << "actualHit=" << actualHit
                               << "resizeDisabled=" << resizeDisabled;
        maximizedStatePassed = maximizedStatePassed && resizeDisabled;
    }
    window.showNormal();
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool restored = !window.maximized();
    qCInfo(applicationLog) << "Window resize runtime summary"
                           << "normalStatePassed=" << normalStatePassed
                           << "maximizedStatePassed=" << maximizedStatePassed << "restored=" << restored;
    return normalStatePassed && maximizedStatePassed && restored;
}

[[nodiscard]] bool runWindowDpiRuntimeSmoke(ztermy::NativeWindow &window, const QString &outputDirectory)
{
    bool expectedDprValid = false;
    const qreal expectedDpr = qEnvironmentVariable("ZTERMY_TEST_EXPECTED_DPR").toDouble(&expectedDprValid);
    if (!expectedDprValid || expectedDpr < 1.0 || expectedDpr > 4.0)
    {
        qCWarning(applicationLog) << "Window DPI smoke requires ZTERMY_TEST_EXPECTED_DPR between 1 and 4";
        return false;
    }

    constexpr QSize logicalSize{800, 600};
    window.resize(logicalSize);
    window.show();
    window.requestActivate();
    processWindowEventsFor(std::chrono::milliseconds{350});

    QQuickItem *rootObject = window.rootObject();
    auto *maximizeButton =
        rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QStringLiteral("maximizeCaptionButton"));
    const HWND windowHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    RECT clientRect{};
    if (windowHandle == nullptr || maximizeButton == nullptr || GetClientRect(windowHandle, &clientRect) == FALSE)
    {
        qCWarning(applicationLog) << "Window DPI smoke could not inspect the native window"
                                  << "windowHandle=" << windowHandle << "rootObject=" << (rootObject != nullptr)
                                  << "maximizeButton=" << (maximizeButton != nullptr);
        return false;
    }

    const qreal actualDpr = window.devicePixelRatio();
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    const int expectedClientWidth = qRound(window.width() * actualDpr);
    const int expectedClientHeight = qRound(window.height() * actualDpr);
    const QImage capture = window.grabWindow();
    const QString capturePath =
        QDir(outputDirectory)
            .filePath(QStringLiteral("dpi-%1.png").arg(qRound(expectedDpr * 100.0), 3, 10, QLatin1Char('0')));
    const bool captureSaved = !capture.isNull() && capture.save(capturePath);

    const QPointF maximizeCenter =
        maximizeButton->mapToScene(QPointF{maximizeButton->width() / 2.0, maximizeButton->height() / 2.0});
    POINT maximizeClientPoint{
        .x = qRound(maximizeCenter.x() * actualDpr),
        .y = qRound(maximizeCenter.y() * actualDpr),
    };
    const bool maximizePointMapped = ClientToScreen(windowHandle, &maximizeClientPoint) != FALSE;
    const LRESULT maximizeHit = maximizePointMapped ? SendMessageW(windowHandle, WM_NCHITTEST, 0,
                                                                   MAKELPARAM(static_cast<WORD>(maximizeClientPoint.x),
                                                                              static_cast<WORD>(maximizeClientPoint.y)))
                                                    : HTNOWHERE;

    constexpr qreal dprTolerance = 0.01;
    constexpr int pixelTolerance = 1;
    const bool dprMatches =
        qAbs(actualDpr - expectedDpr) <= dprTolerance && qAbs(capture.devicePixelRatio() - expectedDpr) <= dprTolerance;
    const bool logicalSizeMatches = qAbs(window.width() - logicalSize.width()) <= pixelTolerance
                                    && qAbs(window.height() - logicalSize.height()) <= pixelTolerance
                                    && qAbs(rootObject->width() - window.width()) <= dprTolerance
                                    && qAbs(rootObject->height() - window.height()) <= dprTolerance;
    const bool clientPixelsMatch = qAbs(clientWidth - expectedClientWidth) <= pixelTolerance
                                   && qAbs(clientHeight - expectedClientHeight) <= pixelTolerance;
    const bool capturePixelsMatch = qAbs(capture.width() - clientWidth) <= pixelTolerance
                                    && qAbs(capture.height() - clientHeight) <= pixelTolerance;
    const bool maximizeHitMatches = maximizePointMapped && maximizeHit == HTMAXBUTTON;

    qCInfo(applicationLog) << "Window DPI runtime check"
                           << "expectedDpr=" << expectedDpr << "actualDpr=" << actualDpr
                           << "captureDpr=" << capture.devicePixelRatio() << "logicalSize=" << window.size()
                           << "clientPixels=" << QSize{clientWidth, clientHeight} << "capturePixels=" << capture.size()
                           << "dprMatches=" << dprMatches << "logicalSizeMatches=" << logicalSizeMatches
                           << "clientPixelsMatch=" << clientPixelsMatch << "capturePixelsMatch=" << capturePixelsMatch
                           << "maximizeHit=" << maximizeHit << "maximizeHitMatches=" << maximizeHitMatches
                           << "captureSaved=" << captureSaved << "capturePath=" << capturePath;
    return dprMatches && logicalSizeMatches && clientPixelsMatch && capturePixelsMatch && maximizeHitMatches
           && captureSaved;
}

[[nodiscard]] bool queryDwmIntAttribute(const HWND windowHandle, const DWORD attribute, int *value)
{
    return value != nullptr && SUCCEEDED(DwmGetWindowAttribute(windowHandle, attribute, value, sizeof(*value)));
}

[[nodiscard]] bool verifyWindowAppearance(ztermy::NativeWindow &window, const qreal opacity,
                                          const QString &backdropPreference, const bool darkMode,
                                          const int expectedBackdrop)
{
    if (!window.applyAppearance(opacity, backdropPreference, darkMode))
    {
        qCWarning(applicationLog) << "Window appearance request was rejected"
                                  << "opacity=" << opacity << "backdrop=" << backdropPreference
                                  << "darkMode=" << darkMode;
        return false;
    }
    processWindowEventsFor(std::chrono::milliseconds{150});

    const auto windowHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    int appliedDarkMode = -1;
    int appliedCornerPreference = -1;
    int appliedBackdrop = -1;
    constexpr DWORD useImmersiveDarkModeAttribute = 20;
    constexpr DWORD windowCornerPreferenceAttribute = 33;
    constexpr DWORD systemBackdropTypeAttribute = 38;
    const bool darkModeRead = queryDwmIntAttribute(windowHandle, useImmersiveDarkModeAttribute, &appliedDarkMode);
    const bool cornerPreferenceRead =
        queryDwmIntAttribute(windowHandle, windowCornerPreferenceAttribute, &appliedCornerPreference);
    const bool backdropRead = queryDwmIntAttribute(windowHandle, systemBackdropTypeAttribute, &appliedBackdrop);
    const bool opacityMatches = qAbs(window.opacity() - opacity) < 0.001;
    const bool darkModeMatches = darkModeRead && appliedDarkMode == static_cast<int>(darkMode);
    constexpr int roundCornerPreference = 2;
    const bool cornerPreferenceMatches = cornerPreferenceRead && appliedCornerPreference == roundCornerPreference;
    const bool backdropMatches = backdropRead && appliedBackdrop == expectedBackdrop;
    qCInfo(applicationLog) << "Window appearance runtime check"
                           << "opacity=" << window.opacity() << "opacityMatches=" << opacityMatches
                           << "darkModeRead=" << darkModeRead << "darkMode=" << appliedDarkMode
                           << "darkModeMatches=" << darkModeMatches << "cornerPreferenceRead=" << cornerPreferenceRead
                           << "cornerPreference=" << appliedCornerPreference
                           << "cornerPreferenceMatches=" << cornerPreferenceMatches << "backdropRead=" << backdropRead
                           << "backdrop=" << appliedBackdrop << "backdropMatches=" << backdropMatches;
    return opacityMatches && darkModeMatches && cornerPreferenceMatches && backdropMatches;
}

[[nodiscard]] bool runWindowAppearanceRuntimeSmoke(ztermy::NativeWindow &window)
{
    window.resize(QSize{1120, 800});
    window.show();
    processWindowEventsFor(std::chrono::milliseconds{250});

    constexpr int noBackdrop = 1;
    constexpr int micaBackdrop = 2;
    constexpr int acrylicBackdrop = 3;
    const bool darkNone = verifyWindowAppearance(window, 1.0, QStringLiteral("none"), true, noBackdrop);
    const qreal baselineOpacity = window.opacity();
    const bool invalidOpacityRejected = !window.applyAppearance(0.49, QStringLiteral("none"), false)
                                        && qAbs(window.opacity() - baselineOpacity) < 0.001;
    const bool invalidBackdropRejected = !window.applyAppearance(0.75, QStringLiteral("invalid"), false)
                                         && qAbs(window.opacity() - baselineOpacity) < 0.001;
    const bool lightMica = verifyWindowAppearance(window, 0.85, QStringLiteral("mica"), false, micaBackdrop);
    const bool darkAcrylic = verifyWindowAppearance(window, 0.75, QStringLiteral("acrylic"), true, acrylicBackdrop);
    const bool restored = verifyWindowAppearance(window, 1.0, QStringLiteral("none"), true, noBackdrop);

    qCInfo(applicationLog) << "Window appearance runtime summary"
                           << "darkNone=" << darkNone << "invalidOpacityRejected=" << invalidOpacityRejected
                           << "invalidBackdropRejected=" << invalidBackdropRejected << "lightMica=" << lightMica
                           << "darkAcrylic=" << darkAcrylic << "restored=" << restored;
    return darkNone && invalidOpacityRejected && invalidBackdropRejected && lightMica && darkAcrylic && restored;
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

[[nodiscard]] QQuickItem *quickItem(QQuickItem *rootObject, const char *objectName)
{
    return rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QString::fromLatin1(objectName));
}

[[nodiscard]] QString namedFocusItem(const ztermy::NativeWindow &window)
{
    for (QQuickItem *item = window.activeFocusItem(); item != nullptr; item = item->parentItem())
    {
        if (!item->objectName().isEmpty())
        {
            return item->objectName();
        }
    }
    return {};
}

void sendKey(ztermy::NativeWindow &window, const Qt::Key key, const Qt::KeyboardModifiers modifiers = {})
{
    qt_handleKeyEvent(&window, QEvent::KeyPress, key, modifiers);
    QCoreApplication::processEvents();
    qt_handleKeyEvent(&window, QEvent::KeyRelease, key, modifiers);
    processWindowEventsFor(std::chrono::milliseconds{40});
}

[[nodiscard]] bool focusItem(ztermy::NativeWindow &window, QQuickItem *item, const QString &expectedName)
{
    if (item == nullptr || !item->isVisible() || !item->isEnabled())
    {
        qCWarning(applicationLog) << "UI keyboard smoke focus target unavailable" << expectedName;
        return false;
    }
    item->forceActiveFocus(Qt::TabFocusReason);
    processWindowEventsFor(std::chrono::milliseconds{40});
    const QString actualName = namedFocusItem(window);
    if (actualName != expectedName)
    {
        qCWarning(applicationLog) << "UI keyboard smoke focus mismatch"
                                  << "expected=" << expectedName << "actual=" << actualName;
        return false;
    }
    return true;
}

[[nodiscard]] bool verifyAccessibleButton(QQuickItem *rootObject, const char *objectName, const char *expectedName)
{
    QQuickItem *item = quickItem(rootObject, objectName);
    QAccessibleInterface *interface = item == nullptr ? nullptr : QAccessible::queryAccessibleInterface(item);
    const QString expected = QString::fromLatin1(expectedName);
    if (interface == nullptr || interface->role() != QAccessible::Button
        || interface->text(QAccessible::Name) != expected)
    {
        qCWarning(applicationLog) << "Accessible button contract mismatch"
                                  << "object=" << objectName << "expectedName=" << expected
                                  << "hasInterface=" << (interface != nullptr)
                                  << "actualRole=" << (interface == nullptr ? -1 : static_cast<int>(interface->role()))
                                  << "actualName="
                                  << (interface == nullptr ? QString{} : interface->text(QAccessible::Name));
        return false;
    }
    return true;
}

[[nodiscard]] bool verifySettingsTabOrder(ztermy::NativeWindow &window, QQuickItem *rootObject)
{
    constexpr std::array<const char *, 12> order{
        "settingsTheme",          "settingsOpacity", "settingsBackdrop",    "settingsFontFamily",
        "settingsFontSize",       "settingsCursor",  "settingsCursorBlink", "settingsCopyOnSelect",
        "settingsMultilinePaste", "settingsReset",   "settingsDiscard",     "settingsApply",
    };

    if (!focusItem(window, quickItem(rootObject, order.front()), QString::fromLatin1(order.front())))
    {
        return false;
    }
    for (std::size_t index = 1; index < order.size(); ++index)
    {
        sendKey(window, Qt::Key_Tab);
        const QString expectedName = QString::fromLatin1(order[index]);
        const QString actualName = namedFocusItem(window);
        if (actualName != expectedName)
        {
            qCWarning(applicationLog) << "Settings Tab order mismatch"
                                      << "index=" << index << "expected=" << expectedName << "actual=" << actualName;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool verifyHostEditorTabOrder(ztermy::NativeWindow &window, QQuickItem *rootObject)
{
    constexpr std::array<const char *, 11> order{
        "hostName",     "hostGroup",          "hostAddress", "hostPort",
        "hostUsername", "hostAuthentication", "hostKeyPath", "hostPassphraseRequired",
        "hostCancel",   "hostSave",           "hostConnect",
    };

    if (!focusItem(window, quickItem(rootObject, order.front()), QString::fromLatin1(order.front())))
    {
        return false;
    }
    for (std::size_t index = 1; index < order.size(); ++index)
    {
        sendKey(window, Qt::Key_Tab);
        const QString expectedName = QString::fromLatin1(order[index]);
        const QString actualName = namedFocusItem(window);
        if (actualName != expectedName)
        {
            qCWarning(applicationLog) << "Host editor Tab order mismatch"
                                      << "index=" << index << "expected=" << expectedName << "actual=" << actualName;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool runUiKeyboardRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller)
{
    window.resize(QSize{1120, 800});
    window.show();
    window.requestActivate();
    processWindowEventsFor(std::chrono::milliseconds{250});

    QQuickItem *rootObject = window.rootObject();
    if (rootObject == nullptr)
    {
        return false;
    }

    constexpr std::array accessibleButtons{
        std::pair{"hostsTitleAction", "Hosts"},         std::pair{"titleNewTabAction", "New local terminal"},
        std::pair{"minimizeCaptionButton", "Minimize"}, std::pair{"maximizeCaptionButton", "Maximize"},
        std::pair{"closeCaptionButton", "Close"},       std::pair{"sideHostsAction", "Hosts"},
        std::pair{"sideSettingsAction", "Settings"},    std::pair{"localMachineAction", "Open local terminal"},
    };
    for (const auto &[objectName, expectedName] : accessibleButtons)
    {
        if (!verifyAccessibleButton(rootObject, objectName, expectedName))
        {
            return false;
        }
    }

    rootObject->setProperty("currentPage", QStringLiteral("hosts"));
    processWindowEventsFor(std::chrono::milliseconds{100});
    QQuickItem *settingsAction = quickItem(rootObject, "sideSettingsAction");
    if (!focusItem(window, settingsAction, QStringLiteral("sideSettingsAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    if (rootObject->property("currentPage").toString() != QStringLiteral("settings"))
    {
        qCWarning(applicationLog) << "Space did not activate Settings navigation";
        return false;
    }

    if (!verifySettingsTabOrder(window, rootObject))
    {
        return false;
    }
    window.resize(QSize{500, 360});
    processWindowEventsFor(std::chrono::milliseconds{150});
    if (!verifySettingsTabOrder(window, rootObject))
    {
        return false;
    }
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{150});

    QQuickItem *theme = quickItem(rootObject, "settingsTheme");
    QQuickItem *opacity = quickItem(rootObject, "settingsOpacity");
    QQuickItem *fontSize = quickItem(rootObject, "settingsFontSize");
    QQuickItem *cursorBlink = quickItem(rootObject, "settingsCursorBlink");
    QQuickItem *copyOnSelect = quickItem(rootObject, "settingsCopyOnSelect");
    QQuickItem *multilinePaste = quickItem(rootObject, "settingsMultilinePaste");
    QQuickItem *apply = quickItem(rootObject, "settingsApply");
    if (theme == nullptr || opacity == nullptr || fontSize == nullptr || cursorBlink == nullptr
        || copyOnSelect == nullptr || multilinePaste == nullptr || apply == nullptr)
    {
        qCWarning(applicationLog) << "Settings keyboard smoke object lookup failed";
        return false;
    }

    if (!focusItem(window, theme, QStringLiteral("settingsTheme")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Down, Qt::AltModifier);
    auto *themePopup = theme->property("popup").value<QObject *>();
    const bool popupOpened = themePopup != nullptr && themePopup->property("visible").toBool();
    sendKey(window, Qt::Key_Escape);
    const bool popupClosed = themePopup != nullptr && !themePopup->property("visible").toBool();
    sendKey(window, Qt::Key_Down);

    if (!focusItem(window, opacity, QStringLiteral("settingsOpacity")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Left);

    if (!focusItem(window, fontSize, QStringLiteral("settingsFontSize")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Up);

    const auto toggleWithSpace = [&window](QQuickItem *item, const QString &name) {
        if (!focusItem(window, item, name))
        {
            return false;
        }
        const bool before = item->property("checked").toBool();
        sendKey(window, Qt::Key_Space);
        return item->property("checked").toBool() != before;
    };
    const bool switchesChanged = toggleWithSpace(cursorBlink, QStringLiteral("settingsCursorBlink"))
                                 && toggleWithSpace(copyOnSelect, QStringLiteral("settingsCopyOnSelect"))
                                 && toggleWithSpace(multilinePaste, QStringLiteral("settingsMultilinePaste"));

    const bool draftMatches = popupOpened && popupClosed && theme->property("currentIndex").toInt() == 2
                              && qAbs(opacity->property("value").toReal() - 0.95) < 0.001
                              && fontSize->property("value").toInt() == 15 && switchesChanged;
    if (!draftMatches)
    {
        qCWarning(applicationLog) << "Settings keyboard edits did not produce the expected draft"
                                  << "popupOpened=" << popupOpened << "popupClosed=" << popupClosed
                                  << "themeIndex=" << theme->property("currentIndex").toInt()
                                  << "opacity=" << opacity->property("value").toReal()
                                  << "fontSize=" << fontSize->property("value").toInt()
                                  << "switchesChanged=" << switchesChanged;
        return false;
    }

    if (!focusItem(window, apply, QStringLiteral("settingsApply")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool settingsApplied = controller.themePreference() == QStringLiteral("light")
                                 && qAbs(controller.windowOpacity() - 0.95) < 0.001
                                 && controller.terminalFontSize() == 15 && !controller.cursorBlink()
                                 && controller.copyOnSelect() && !controller.confirmMultilinePaste();
    if (!settingsApplied)
    {
        qCWarning(applicationLog) << "Enter did not apply the keyboard-edited settings";
        return false;
    }

    rootObject->setProperty("currentPage", QStringLiteral("hosts"));
    processWindowEventsFor(std::chrono::milliseconds{100});
    QQuickItem *newHost = quickItem(rootObject, "hostNew");
    if (!verifyAccessibleButton(rootObject, "hostNew", "Create a new SSH host profile")
        || !focusItem(window, newHost, QStringLiteral("hostNew")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Tab);
    if (namedFocusItem(window) != QStringLiteral("hostSearch"))
    {
        qCWarning(applicationLog) << "Host page Tab order did not reach search after New host"
                                  << "actual=" << namedFocusItem(window);
        return false;
    }
    if (!focusItem(window, newHost, QStringLiteral("hostNew")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    QQuickItem *hostPane = quickItem(rootObject, "hostConnectionPane");
    const bool editorOpened = hostPane != nullptr && hostPane->property("editorExpanded").toBool()
                              && namedFocusItem(window) == QStringLiteral("hostName");
    if (!editorOpened || !verifyHostEditorTabOrder(window, rootObject))
    {
        qCWarning(applicationLog) << "Enter did not open a keyboard-reachable host editor"
                                  << "editorOpened=" << editorOpened << "focus=" << namedFocusItem(window);
        return false;
    }
    window.resize(QSize{500, 360});
    processWindowEventsFor(std::chrono::milliseconds{150});
    if (!verifyHostEditorTabOrder(window, rootObject))
    {
        return false;
    }
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{150});
    QQuickItem *passphraseRequired = quickItem(rootObject, "hostPassphraseRequired");
    QQuickItem *credential = quickItem(rootObject, "hostCredential");
    if (!focusItem(window, passphraseRequired, QStringLiteral("hostPassphraseRequired")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    const bool checkboxChanged =
        passphraseRequired->property("checked").toBool() && credential != nullptr && credential->isVisible();
    if (!checkboxChanged)
    {
        qCWarning(applicationLog) << "Space did not toggle the private-key passphrase checkbox";
        return false;
    }

    const qsizetype initialTabCount = controller.terminalTabs().size();
    QQuickItem *newTabAction = quickItem(rootObject, "titleNewTabAction");
    if (!focusItem(window, newTabAction, QStringLiteral("titleNewTabAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool oneTabCreated = controller.terminalTabs().size() == initialTabCount + 1
                               && rootObject->property("currentPage").toString() == QStringLiteral("terminal")
                               && namedFocusItem(window) == QStringLiteral("terminalViewport");
    if (!oneTabCreated)
    {
        qCWarning(applicationLog) << "Enter did not create exactly one focused local terminal"
                                  << "initialTabs=" << initialTabCount
                                  << "finalTabs=" << controller.terminalTabs().size()
                                  << "page=" << rootObject->property("currentPage").toString()
                                  << "focus=" << namedFocusItem(window);
        return false;
    }

    QQuickItem *hostsAction = quickItem(rootObject, "hostsTitleAction");
    if (!focusItem(window, hostsAction, QStringLiteral("hostsTitleAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    const bool navigationPreservedSession = rootObject->property("currentPage").toString() == QStringLiteral("hosts")
                                            && controller.terminalTabs().size() == initialTabCount + 1;
    qCInfo(applicationLog) << "UI keyboard route check"
                           << "settingsTabStops=" << 24 << "hostEditorTabStops=" << 22
                           << "popupKeyboard=" << (popupOpened && popupClosed) << "settingsApplied=" << settingsApplied
                           << "checkboxKeyboard=" << checkboxChanged << "oneTabCreated=" << oneTabCreated
                           << "navigationPreservedSession=" << navigationPreservedSession;
    return navigationPreservedSession;
}

[[nodiscard]] bool terminalRegionHasRenderedContent(const QImage &windowImage,
                                                    const ztermy::ui::TerminalItem &terminalItem)
{
    const qreal scale = windowImage.devicePixelRatio();
    const QPointF sceneTopLeft = terminalItem.mapToScene(QPointF{});
    const QRect terminalRect{
        qRound(sceneTopLeft.x() * scale),
        qRound(sceneTopLeft.y() * scale),
        qRound(terminalItem.width() * scale),
        qRound(terminalItem.height() * scale),
    };
    const QRect boundedRect = terminalRect.intersected(windowImage.rect());
    if (boundedRect.width() < 100 || boundedRect.height() < 100)
    {
        return false;
    }

    QSet<QRgb> colors;
    for (int y = boundedRect.top(); y <= boundedRect.bottom() && colors.size() < 8; y += 4)
    {
        for (int x = boundedRect.left(); x <= boundedRect.right() && colors.size() < 8; x += 4)
        {
            colors.insert(windowImage.pixel(x, y));
        }
    }
    return colors.size() >= 8;
}

[[nodiscard]] bool runTerminalRenderRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                                 ztermy::ui::TerminalItem &terminalItem, const QString &outputDirectory)
{
    window.resize(QSize{1120, 800});
    window.show();
    window.requestActivate();
    processWindowEventsFor(std::chrono::milliseconds{250});

    if (controller.startLocalTerminal().isEmpty())
    {
        qCWarning(applicationLog) << "Terminal render smoke could not start the local terminal";
        return false;
    }

    QElapsedTimer startupTimer;
    startupTimer.start();
    while (startupTimer.elapsed() < 5'000
           && (controller.terminalTabs().isEmpty()
               || !controller.terminalTabs().front().toMap().value(QStringLiteral("running")).toBool()))
    {
        processWindowEventsFor(std::chrono::milliseconds{20});
    }
    if (controller.terminalTabs().isEmpty()
        || !controller.terminalTabs().front().toMap().value(QStringLiteral("running")).toBool())
    {
        qCWarning(applicationLog) << "Terminal render smoke did not reach a running local session";
        return false;
    }
    processWindowEventsFor(std::chrono::milliseconds{250});

    std::uint64_t frameSwaps = 0;
    const QMetaObject::Connection frameConnection =
        QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&frameSwaps] {
            ++frameSwaps;
        });

    std::uint64_t heartbeatTicks = 0;
    qint64 maximumHeartbeatGapMilliseconds = 0;
    QElapsedTimer heartbeatGap;
    heartbeatGap.start();
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
        maximumHeartbeatGapMilliseconds = std::max(maximumHeartbeatGapMilliseconds, heartbeatGap.restart());
        ++heartbeatTicks;
    });
    heartbeat.start();

    constexpr auto completionMarker = "ZTERMY_RENDER_GATE_DONE";
    terminalItem.inputGenerated(QByteArrayLiteral("1..20000 | ForEach-Object { \"ztermy render line $_\" }; "
                                                  "Write-Output ('ZTERMY_RENDER_' + 'GATE_DONE')\r"));
    QElapsedTimer completionTimer;
    completionTimer.start();
    qint64 nextSearchMilliseconds = 100;
    bool compactResizeApplied = false;
    bool regularResizeRestored = false;
    while (completionTimer.elapsed() < 20'000 && controller.terminalSearchTotal() == 0)
    {
        processWindowEventsFor(std::chrono::milliseconds{20});
        const qint64 elapsedMilliseconds = completionTimer.elapsed();
        if (!compactResizeApplied && elapsedMilliseconds >= 250)
        {
            window.resize(QSize{780, 520});
            compactResizeApplied = true;
        }
        if (!regularResizeRestored && elapsedMilliseconds >= 500)
        {
            window.resize(QSize{1120, 800});
            regularResizeRestored = true;
        }
        if (elapsedMilliseconds >= nextSearchMilliseconds)
        {
            controller.searchTerminal(QString::fromLatin1(completionMarker), false, true);
            nextSearchMilliseconds += 100;
        }
    }
    const qint64 completionMilliseconds = completionTimer.elapsed();
    processWindowEventsFor(std::chrono::milliseconds{250});
    heartbeat.stop();
    QObject::disconnect(frameConnection);

    QDir().mkpath(outputDirectory);
    const QString capturePath = QDir(outputDirectory).filePath(QStringLiteral("terminal-render-complete.png"));
    const QImage capture = window.grabWindow();
    const bool captureSaved = capture.save(capturePath);
    const bool terminalRendered = terminalRegionHasRenderedContent(capture, terminalItem);
    const bool completed = controller.terminalSearchTotal() > 0;
    const bool responsive = heartbeatTicks >= 20 && maximumHeartbeatGapMilliseconds <= 250;
    const bool progressiveFrames = frameSwaps >= 5;
    const bool resizeCompleted = compactResizeApplied && regularResizeRestored && window.size() == QSize{1120, 800};

    qCInfo(applicationLog) << "Terminal render runtime check"
                           << "completed=" << completed << "completionMs=" << completionMilliseconds
                           << "heartbeatTicks=" << heartbeatTicks
                           << "maxHeartbeatGapMs=" << maximumHeartbeatGapMilliseconds << "frameSwaps=" << frameSwaps
                           << "resizeCompleted=" << resizeCompleted << "captureSaved=" << captureSaved
                           << "terminalRendered=" << terminalRendered << "capture=" << capturePath;
    return completed && responsive && progressiveFrames && resizeCompleted && captureSaved && terminalRendered;
}

} // namespace

// Qt framework entry points are exception-opaque. Let unexpected failures reach
// the process crash-diagnostics boundary instead of swallowing them here.
// NOLINTNEXTLINE(bugprone-exception-escape)
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
    const bool uiKeyboardSmoke = QCoreApplication::arguments().contains(QStringLiteral("--ui-keyboard-smoke"));
    const bool terminalRenderSmoke = QCoreApplication::arguments().contains(QStringLiteral("--terminal-render-smoke"));
    const bool windowAppearanceSmoke =
        QCoreApplication::arguments().contains(QStringLiteral("--window-appearance-smoke"));
    const bool windowResizeSmoke = QCoreApplication::arguments().contains(QStringLiteral("--window-resize-smoke"));
    const bool windowDpiSmoke = QCoreApplication::arguments().contains(QStringLiteral("--window-dpi-smoke"));
    if ((uiLayoutSmoke || uiKeyboardSmoke) && !applyUiLayoutSmokeTheme(appController, QStringLiteral("dark")))
    {
        qCCritical(applicationLog) << "Could not prepare the UI runtime smoke settings";
        return EXIT_FAILURE;
    }
    if (uiLayoutSmoke
        && !appController.saveHostProfile(QStringLiteral("ui-layout-smoke-profile"), QStringLiteral("Layout test host"),
                                          QStringLiteral("192.0.2.10"), 22, QStringLiteral("developer"),
                                          QStringLiteral("private-key"), QStringLiteral("C:/test/id_ed25519"), false,
                                          QStringLiteral("Test fixtures")))
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
    if (windowAppearanceSmoke)
    {
        const bool passed = runWindowAppearanceRuntimeSmoke(window);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "Window appearance runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Window appearance runtime smoke test completed";
        return EXIT_SUCCESS;
    }
    if (windowResizeSmoke)
    {
        const bool passed = runWindowResizeRuntimeSmoke(window);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "Window resize runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Window resize runtime smoke test completed";
        return EXIT_SUCCESS;
    }
    if (windowDpiSmoke)
    {
        const bool passed = runWindowDpiRuntimeSmoke(window, paths->dataDirectory);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "Window DPI runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Window DPI runtime smoke test completed";
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
    if (uiKeyboardSmoke)
    {
        const bool passed = runUiKeyboardRuntimeSmoke(window, appController);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "UI keyboard runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "UI keyboard runtime smoke test completed";
        return EXIT_SUCCESS;
    }
    if (terminalRenderSmoke)
    {
        const bool passed = runTerminalRenderRuntimeSmoke(window, appController, *terminalItem, paths->dataDirectory);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "Terminal render runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Terminal render runtime smoke test completed";
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
