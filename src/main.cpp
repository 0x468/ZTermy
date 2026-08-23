#include "application/AppController.h"
#include "application/FontCatalog.h"
#include "application/LocalizationManager.h"
#include "application/ai/AiConversationModel.h"
#include "application/diagnostics/DiagnosticReporter.h"
#include "core/config/ApplicationPaths.h"
#include "core/logging/Logging.h"
#include "platform/windows/CrashDiagnostics.h"
#include "platform/windows/NativeWindow.h"
#include "ui/icons/SvgIconImageProvider.h"
#include "ui/terminal/TerminalItem.h"
#include "ztermy_version.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaType>
#include <QMimeData>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QVariantMap>

#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>

Q_LOGGING_CATEGORY(applicationLog, "ztermy.application")

QT_BEGIN_NAMESPACE
Q_GUI_EXPORT void qt_handleKeyEvent(QWindow *window, QEvent::Type type, int key, Qt::KeyboardModifiers modifiers,
                                    const QString &text = {}, bool autorepeat = false, ushort count = 1);
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers modifiers, int timestamp);
QT_END_NAMESPACE

namespace
{

constexpr auto kPerformanceBackdropEnvironment = "ZTERMY_PERFORMANCE_BACKDROP";
constexpr DWORD kSystemBackdropTypeAttribute = 38;

[[nodiscard]] bool rawArgumentPresent(const int argc, char *const *argv, const std::string_view expected) noexcept
{
    for (int index = 1; index < argc; ++index)
    {
        if (argv[index] != nullptr && std::string_view{argv[index]} == expected)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool validPerformanceBackdrop(const QString &value)
{
    return value == QStringLiteral("acrylic") || value == QStringLiteral("mica") || value == QStringLiteral("micaAlt")
           || value == QStringLiteral("transparent") || value == QStringLiteral("opaque");
}

[[nodiscard]] QString requestedPerformanceBackdrop()
{
    const QString requested = qEnvironmentVariable(kPerformanceBackdropEnvironment).trimmed();
    return requested.isEmpty() ? QStringLiteral("acrylic") : requested;
}

[[nodiscard]] QString settingsBackdropForPerformance(const QString &requested)
{
    return requested == QStringLiteral("opaque") ? QStringLiteral("transparent") : requested;
}

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

[[nodiscard]] std::unique_ptr<QMimeData> cloneMimeData(const QMimeData *source)
{
    auto clone = std::make_unique<QMimeData>();
    if (source == nullptr)
    {
        return clone;
    }
    for (const QString &format : source->formats())
    {
        clone->setData(format, source->data(format));
    }
    return clone;
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

[[nodiscard]] bool verifyWindowAppearance(ztermy::NativeWindow &window, const QString &backdropPreference,
                                          const bool darkMode, const int expectedBackdrop)
{
    if (!window.applyAppearance(backdropPreference, darkMode))
    {
        qCWarning(applicationLog) << "Window appearance request was rejected"
                                  << "backdrop=" << backdropPreference << "darkMode=" << darkMode;
        return false;
    }
    processWindowEventsFor(std::chrono::milliseconds{150});

    const auto windowHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    int appliedDarkMode = -1;
    int appliedCornerPreference = -1;
    int appliedBackdrop = -1;
    constexpr DWORD useImmersiveDarkModeAttribute = 20;
    constexpr DWORD windowCornerPreferenceAttribute = 33;
    const bool darkModeRead = queryDwmIntAttribute(windowHandle, useImmersiveDarkModeAttribute, &appliedDarkMode);
    const bool cornerPreferenceRead =
        queryDwmIntAttribute(windowHandle, windowCornerPreferenceAttribute, &appliedCornerPreference);
    const bool backdropRead = queryDwmIntAttribute(windowHandle, kSystemBackdropTypeAttribute, &appliedBackdrop);
    const bool windowRemainsOpaque = qAbs(window.opacity() - 1.0) < 0.001;
    const bool darkModeMatches = darkModeRead && appliedDarkMode == static_cast<int>(darkMode);
    constexpr int roundCornerPreference = 2;
    const bool cornerPreferenceMatches = cornerPreferenceRead && appliedCornerPreference == roundCornerPreference;
    const bool backdropMatches = backdropRead && appliedBackdrop == expectedBackdrop;
    qCInfo(applicationLog) << "Window appearance runtime check"
                           << "windowOpacity=" << window.opacity() << "windowRemainsOpaque=" << windowRemainsOpaque
                           << "darkModeRead=" << darkModeRead << "darkMode=" << appliedDarkMode
                           << "darkModeMatches=" << darkModeMatches << "cornerPreferenceRead=" << cornerPreferenceRead
                           << "cornerPreference=" << appliedCornerPreference
                           << "cornerPreferenceMatches=" << cornerPreferenceMatches << "backdropRead=" << backdropRead
                           << "backdrop=" << appliedBackdrop << "backdropMatches=" << backdropMatches;
    return windowRemainsOpaque && darkModeMatches && cornerPreferenceMatches && backdropMatches;
}

[[nodiscard]] bool runWindowAppearanceRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller)
{
    window.resize(QSize{1120, 800});
    window.show();
    processWindowEventsFor(std::chrono::milliseconds{250});

    const bool defaultAlphaBuffer = QQuickWindow::hasDefaultAlphaBuffer();
    const int surfaceAlphaBits = window.format().alphaBufferSize();
    const bool translucentSurfaceCapable = defaultAlphaBuffer && surfaceAlphaBits > 0 && window.color().alpha() == 0;
    constexpr int transparentBackdrop = 1;
    constexpr int micaBackdrop = 2;
    constexpr int acrylicBackdrop = 3;
    constexpr int micaAltBackdrop = 4;
    const auto saveAppearance = [&controller](const QString &theme, const qreal backdropOpacity,
                                              const QString &backdrop) {
        return controller.saveApplicationSettings(
            theme, backdropOpacity, backdrop, controller.accentPreference(), controller.customAccent(),
            controller.uiFontFamily(), controller.terminalFontFamily(), controller.terminalFontSize(),
            controller.showAllTerminalFonts(), controller.terminalLigatures(), controller.terminalBackgroundOpacity(),
            controller.cursorPreference(), controller.cursorBlink(), controller.copyOnSelect(),
            controller.confirmMultilinePaste(), controller.languagePreference(), controller.sftpShowHiddenFiles(),
            controller.sftpConfirmDelete());
    };
    const auto surfaceAlpha = [&window](const char *propertyName) {
        QQuickItem *rootObject = window.rootObject();
        return rootObject == nullptr ? -1 : rootObject->property(propertyName).value<QColor>().alpha();
    };
    const bool darkAcrylicSaved = saveAppearance(QStringLiteral("dark"), 0.55, QStringLiteral("acrylic"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool darkAcrylic = verifyWindowAppearance(window, QStringLiteral("acrylic"), true, acrylicBackdrop);
    const int acrylicRootAlpha = surfaceAlpha("backgroundColor");
    const int acrylicContentAlpha = surfaceAlpha("contentColor");
    const int acrylicChromeAlpha = surfaceAlpha("chromeColor");
    const int acrylicElevatedAlpha = surfaceAlpha("elevatedColor");
    const int acrylicControlAlpha = surfaceAlpha("controlColor");
    const int acrylicFieldAlpha = surfaceAlpha("fieldColor");
    const bool acrylicSurfaceContract = acrylicRootAlpha == 0 && acrylicContentAlpha > 0 && acrylicContentAlpha < 255
                                        && acrylicChromeAlpha == acrylicContentAlpha
                                        && acrylicElevatedAlpha > acrylicContentAlpha
                                        && acrylicControlAlpha > acrylicElevatedAlpha
                                        && acrylicFieldAlpha > acrylicControlAlpha && acrylicFieldAlpha < 255;
    const bool invalidBackdropRejected =
        !window.applyAppearance(QStringLiteral("invalid"), false) && qAbs(window.opacity() - 1.0) < 0.001;
    const bool invalidBackdropOpacityRejected =
        !saveAppearance(QStringLiteral("dark"), -0.1, QStringLiteral("acrylic"));

    const bool transparentSaved = saveAppearance(QStringLiteral("dark"), 0.55, QStringLiteral("transparent"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool transparent = verifyWindowAppearance(window, QStringLiteral("transparent"), true, transparentBackdrop);
    const int transparentContentAlpha = surfaceAlpha("contentColor");
    const bool transparentSurfaceContract = surfaceAlpha("backgroundColor") == 0 && transparentContentAlpha > 0
                                            && transparentContentAlpha < 255
                                            && surfaceAlpha("chromeColor") == transparentContentAlpha
                                            && surfaceAlpha("panelColor") == transparentContentAlpha
                                            && surfaceAlpha("workspaceColor") == transparentContentAlpha;

    const bool transparentOpaqueSaved = saveAppearance(QStringLiteral("dark"), 1.0, QStringLiteral("transparent"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool transparentOpaque =
        verifyWindowAppearance(window, QStringLiteral("transparent"), true, transparentBackdrop);
    const bool transparentOpaqueSurfaceContract =
        surfaceAlpha("backgroundColor") == 0 && surfaceAlpha("contentColor") == 255
        && surfaceAlpha("chromeColor") == 255 && surfaceAlpha("panelColor") == 255
        && surfaceAlpha("workspaceColor") == 255 && surfaceAlpha("elevatedColor") == 255
        && surfaceAlpha("controlColor") == 255 && surfaceAlpha("fieldColor") == 255;

    const bool transparentClearSaved = saveAppearance(QStringLiteral("dark"), 0.0, QStringLiteral("transparent"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool transparentClear =
        verifyWindowAppearance(window, QStringLiteral("transparent"), true, transparentBackdrop);
    const int transparentClearElevatedAlpha = surfaceAlpha("elevatedColor");
    const int transparentClearControlAlpha = surfaceAlpha("controlColor");
    const int transparentClearFieldAlpha = surfaceAlpha("fieldColor");
    const bool transparentClearSurfaceContract =
        surfaceAlpha("backgroundColor") == 0 && surfaceAlpha("contentColor") == 0 && surfaceAlpha("chromeColor") == 0
        && surfaceAlpha("panelColor") == 0 && surfaceAlpha("workspaceColor") == 0 && transparentClearElevatedAlpha > 0
        && transparentClearControlAlpha > transparentClearElevatedAlpha
        && transparentClearFieldAlpha > transparentClearControlAlpha && transparentClearFieldAlpha < 255;

    const bool lightMicaSaved = saveAppearance(QStringLiteral("light"), 0.1, QStringLiteral("mica"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool lightMica = verifyWindowAppearance(window, QStringLiteral("mica"), false, micaBackdrop);
    const int micaRootAlpha = surfaceAlpha("backgroundColor");
    const int micaContentAlpha = surfaceAlpha("contentColor");
    const int micaChromeAlpha = surfaceAlpha("chromeColor");
    const bool micaSurfaceContract =
        micaRootAlpha == 0 && micaContentAlpha > 0 && micaContentAlpha < 255 && micaChromeAlpha > 0;

    const bool darkMicaAltSaved = saveAppearance(QStringLiteral("dark"), 0.9, QStringLiteral("micaAlt"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool darkMicaAlt = verifyWindowAppearance(window, QStringLiteral("micaAlt"), true, micaAltBackdrop);
    const int micaAltContentAlpha = surfaceAlpha("contentColor");
    const int micaAltChromeAlpha = surfaceAlpha("chromeColor");
    const bool micaAltSurfaceContract = surfaceAlpha("backgroundColor") == 0 && micaAltContentAlpha > micaContentAlpha
                                        && micaAltChromeAlpha > micaChromeAlpha;
    const bool adjustableSurfacesConsistent =
        acrylicContentAlpha == transparentContentAlpha && acrylicChromeAlpha == transparentContentAlpha;

    const bool restoredSaved = saveAppearance(QStringLiteral("dark"), 1.0, QStringLiteral("acrylic"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool restored = verifyWindowAppearance(window, QStringLiteral("acrylic"), true, acrylicBackdrop);

    qCInfo(applicationLog) << "Window appearance runtime summary"
                           << "defaultAlphaBuffer=" << defaultAlphaBuffer << "surfaceAlphaBits=" << surfaceAlphaBits
                           << "transparentClearColor=" << (window.color().alpha() == 0)
                           << "translucentSurfaceCapable=" << translucentSurfaceCapable
                           << "darkAcrylicSaved=" << darkAcrylicSaved << "darkAcrylic=" << darkAcrylic
                           << "acrylicRootAlpha=" << acrylicRootAlpha << "acrylicContentAlpha=" << acrylicContentAlpha
                           << "acrylicChromeAlpha=" << acrylicChromeAlpha
                           << "acrylicElevatedAlpha=" << acrylicElevatedAlpha
                           << "acrylicControlAlpha=" << acrylicControlAlpha << "acrylicFieldAlpha=" << acrylicFieldAlpha
                           << "acrylicSurfaceContract=" << acrylicSurfaceContract
                           << "invalidBackdropRejected=" << invalidBackdropRejected
                           << "invalidBackdropOpacityRejected=" << invalidBackdropOpacityRejected
                           << "transparentSaved=" << transparentSaved << "transparent=" << transparent
                           << "transparentContentAlpha=" << transparentContentAlpha
                           << "transparentSurfaceContract=" << transparentSurfaceContract
                           << "transparentOpaqueSaved=" << transparentOpaqueSaved
                           << "transparentOpaque=" << transparentOpaque
                           << "transparentOpaqueSurfaceContract=" << transparentOpaqueSurfaceContract
                           << "transparentClearSaved=" << transparentClearSaved
                           << "transparentClear=" << transparentClear
                           << "transparentClearElevatedAlpha=" << transparentClearElevatedAlpha
                           << "transparentClearControlAlpha=" << transparentClearControlAlpha
                           << "transparentClearFieldAlpha=" << transparentClearFieldAlpha
                           << "transparentClearSurfaceContract=" << transparentClearSurfaceContract
                           << "lightMica=" << lightMica << "lightMicaSaved=" << lightMicaSaved
                           << "micaRootAlpha=" << micaRootAlpha << "micaContentAlpha=" << micaContentAlpha
                           << "micaChromeAlpha=" << micaChromeAlpha << "micaSurfaceContract=" << micaSurfaceContract
                           << "darkMicaAltSaved=" << darkMicaAltSaved << "darkMicaAlt=" << darkMicaAlt
                           << "micaAltContentAlpha=" << micaAltContentAlpha
                           << "micaAltChromeAlpha=" << micaAltChromeAlpha
                           << "micaAltSurfaceContract=" << micaAltSurfaceContract
                           << "adjustableSurfacesConsistent=" << adjustableSurfacesConsistent
                           << "restoredSaved=" << restoredSaved << "restored=" << restored;
    return translucentSurfaceCapable && darkAcrylicSaved && darkAcrylic && acrylicSurfaceContract
           && invalidBackdropRejected && invalidBackdropOpacityRejected && transparentSaved && transparent
           && transparentSurfaceContract && transparentOpaqueSaved && transparentOpaque
           && transparentOpaqueSurfaceContract && transparentClearSaved && transparentClear
           && transparentClearSurfaceContract && lightMicaSaved && lightMica && micaSurfaceContract && darkMicaAltSaved
           && darkMicaAlt && micaAltSurfaceContract && adjustableSurfacesConsistent && restoredSaved && restored;
}

[[nodiscard]] bool captureLayout(ztermy::NativeWindow &window, const QString &outputDirectory, const QString &name)
{
    const QString path = QDir(outputDirectory).filePath(name + QStringLiteral(".png"));
    window.requestUpdate();
    processWindowEventsFor(std::chrono::milliseconds{50});
    static_cast<void>(window.grabWindow());
    processWindowEventsFor(std::chrono::milliseconds{50});
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
    auto *hostCommandRow = rootObject->findChild<QObject *>(QStringLiteral("hostCommandRow"));
    auto *quickConnectTarget = rootObject->findChild<QObject *>(QStringLiteral("quickConnectTarget"));
    auto *quickConnectAction = rootObject->findChild<QObject *>(QStringLiteral("quickConnectAction"));
    auto *newHostAction = rootObject->findChild<QObject *>(QStringLiteral("hostNew"));
    const QString breakpointName = compact ? QStringLiteral("compact") : QStringLiteral("regular");
    const QString capturePrefix = themeName + QStringLiteral("-") + breakpointName;
    const bool hostCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-hosts"));
    const qreal hostPaneWidth = hostPane == nullptr ? 0.0 : hostPane->property("width").toReal();
    const qreal hostContentWidth = hostContent == nullptr ? 0.0 : hostContent->property("width").toReal();
    const bool hostMatches =
        hostPane != nullptr && hostContent != nullptr && hostEditorGrid != nullptr && hostCommandRow != nullptr
        && quickConnectTarget != nullptr && quickConnectAction != nullptr && newHostAction != nullptr
        && hostPane->property("compactLayout").toBool() == compact && hostEditorGrid->property("columns").toInt() >= 1
        && hostEditorGrid->property("columns").toInt() <= 2 && hostPane->property("profileCardColumns").toInt() >= 1
        && hostPane->property("profileCardColumns").toInt() <= 4 && hostContentWidth > 0.0
        && hostContentWidth <= hostPaneWidth && hostCommandRow->property("width").toReal() > 0.0
        && hostCommandRow->property("width").toReal() <= hostContentWidth
        && quickConnectTarget->property("width").toReal() > 0.0 && quickConnectAction->property("width").toReal() > 0.0
        && newHostAction->property("width").toReal() > 0.0;

    rootObject->setProperty("currentPage", QStringLiteral("settings"));
    processWindowEventsFor(std::chrono::milliseconds{100});
    auto *settingsPane = rootObject->findChild<QObject *>(QStringLiteral("settingsPane"));
    auto *settingsCategoryRail = rootObject->findChild<QObject *>(QStringLiteral("settingsCategoryRail"));
    auto *appearanceGrid = rootObject->findChild<QObject *>(QStringLiteral("settingsAppearanceGrid"));
    auto *terminalGrid = rootObject->findChild<QObject *>(QStringLiteral("settingsTerminalGrid"));
    const bool settingsCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-settings"));
    bool applicationCaptured = false;
    bool applicationMatches = false;
    bool aboutCaptured = false;
    bool aboutMatches = false;
    bool securityCaptured = false;
    bool securityMatches = false;
    bool shortcutsCaptured = false;
    bool shortcutsMatch = false;
    bool sftpCaptured = false;
    bool sftpMatches = false;
    bool aiSettingsCaptured = false;
    bool aiSettingsMatch = false;
    if (settingsPane != nullptr)
    {
        settingsPane->setProperty("currentCategory", QStringLiteral("application"));
        processWindowEventsFor(std::chrono::milliseconds{500});
        auto *windowBehavior = rootObject->findChild<QObject *>(QStringLiteral("settingsWindowBehaviorCard"));
        auto *closeToTray = rootObject->findChild<QObject *>(QStringLiteral("settingsCloseToTraySwitch"));
        auto *settingsReset = rootObject->findChild<QObject *>(QStringLiteral("settingsReset"));
        auto *settingsDiscard = rootObject->findChild<QObject *>(QStringLiteral("settingsDiscard"));
        auto *settingsApply = rootObject->findChild<QObject *>(QStringLiteral("settingsApply"));
        applicationCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-application"));
        applicationMatches = windowBehavior != nullptr && windowBehavior->property("visible").toBool()
                             && closeToTray != nullptr && closeToTray->property("visible").toBool()
                             && settingsReset != nullptr && settingsReset->property("visible").toBool()
                             && settingsDiscard != nullptr && settingsDiscard->property("visible").toBool()
                             && settingsApply != nullptr && settingsApply->property("visible").toBool();
        settingsPane->setProperty("currentCategory", QStringLiteral("about"));
        processWindowEventsFor(std::chrono::milliseconds{500});
        auto *brandLockup = rootObject->findChild<QObject *>(QStringLiteral("settingsApplicationBrandLockup"));
        auto *releaseIdentity = rootObject->findChild<QObject *>(QStringLiteral("settingsReleaseIdentityCard"));
        auto *diagnosticsCard = rootObject->findChild<QObject *>(QStringLiteral("settingsDiagnosticsCard"));
        auto *buildInfo = rootObject->findChild<QObject *>(QStringLiteral("settingsApplicationBuildInfo"));
        aboutCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-about"));
        aboutMatches = brandLockup != nullptr && brandLockup->property("visible").toBool()
                       && brandLockup->property("width").toReal() > 0.0
                       && brandLockup->property("height").toReal() > 0.0 && releaseIdentity != nullptr
                       && releaseIdentity->property("visible").toBool()
                       && releaseIdentity->property("codename").toString() == QStringLiteral("糸")
                       && releaseIdentity->property("verse").toString() == QStringLiteral("剪不断，理还乱，是离愁")
                       && diagnosticsCard != nullptr && diagnosticsCard->property("visible").toBool()
                       && diagnosticsCard->property("width").toReal() > 0.0 && buildInfo != nullptr
                       && buildInfo->property("visible").toBool()
                       && buildInfo->property("text").toString().contains(QCoreApplication::applicationVersion());
        settingsPane->setProperty("currentCategory", QStringLiteral("security"));
        processWindowEventsFor(std::chrono::milliseconds{200});
        auto *credentialStorage = rootObject->findChild<QObject *>(QStringLiteral("settingsCredentialStorage"));
        auto *portablePassword = rootObject->findChild<QObject *>(QStringLiteral("settingsPortableVaultPassword"));
        securityCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-security"));
        securityMatches = credentialStorage != nullptr && portablePassword != nullptr
                          && credentialStorage->property("visible").toBool()
                          && portablePassword->property("visible").toBool();
        settingsPane->setProperty("currentCategory", QStringLiteral("shortcuts"));
        processWindowEventsFor(std::chrono::milliseconds{200});
        auto *shortcutSearch = rootObject->findChild<QObject *>(QStringLiteral("shortcutSearch"));
        shortcutsCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-shortcuts"));
        shortcutsMatch = shortcutSearch != nullptr && shortcutSearch->property("visible").toBool();
        if (!shortcutsMatch)
        {
            qCWarning(applicationLog) << "Shortcuts layout lookup failed"
                                      << "search=" << (shortcutSearch != nullptr) << "searchVisible="
                                      << (shortcutSearch != nullptr && shortcutSearch->property("visible").toBool())
                                      << "category=" << settingsPane->property("currentCategory").toString();
        }
        settingsPane->setProperty("currentCategory", QStringLiteral("sftp"));
        processWindowEventsFor(std::chrono::milliseconds{200});
        auto *sftpGrid = rootObject->findChild<QObject *>(QStringLiteral("settingsSftpGrid"));
        auto *sftpShowHidden = rootObject->findChild<QObject *>(QStringLiteral("settingsSftpShowHidden"));
        auto *sftpConfirmDelete = rootObject->findChild<QObject *>(QStringLiteral("settingsSftpConfirmDelete"));
        sftpCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-sftp-settings"));
        sftpMatches = sftpGrid != nullptr && sftpGrid->property("visible").toBool() && sftpShowHidden != nullptr
                      && sftpConfirmDelete != nullptr;
        settingsPane->setProperty("currentCategory", QStringLiteral("ai"));
        processWindowEventsFor(std::chrono::milliseconds{400});
        auto *aiProvider = rootObject->findChild<QObject *>(QStringLiteral("settingsAiProvider"));
        aiSettingsCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-ai-settings"));
        aiSettingsMatch = aiProvider != nullptr && aiProvider->property("visible").toBool();
        settingsPane->setProperty("currentCategory", QStringLiteral("appearance"));
    }

    if (hostPane == nullptr || hostContent == nullptr || hostEditorGrid == nullptr || settingsPane == nullptr
        || settingsCategoryRail == nullptr || appearanceGrid == nullptr || terminalGrid == nullptr)
    {
        qCWarning(applicationLog) << "UI layout smoke object lookup failed"
                                  << "hostPane=" << (hostPane != nullptr) << "hostContent=" << (hostContent != nullptr)
                                  << "hostEditorGrid=" << (hostEditorGrid != nullptr)
                                  << "settingsPane=" << (settingsPane != nullptr)
                                  << "settingsCategoryRail=" << (settingsCategoryRail != nullptr)
                                  << "appearanceGrid=" << (appearanceGrid != nullptr)
                                  << "terminalGrid=" << (terminalGrid != nullptr);
        return false;
    }

    const bool settingsMatch =
        settingsPane->property("compactLayout").toBool() == compact
        && settingsCategoryRail->property("width").toReal() > 0.0
        && settingsCategoryRail->property("width").toReal() < settingsPane->property("width").toReal()
        && appearanceGrid->property("columns").toInt() == (compact ? 1 : 2)
        && terminalGrid->property("columns").toInt() == (compact ? 1 : 2);

    qCInfo(applicationLog) << "UI layout breakpoint check"
                           << "theme=" << themeName << "size=" << size << "compact=" << compact
                           << "hostPaneWidth=" << hostPaneWidth << "hostContentWidth=" << hostContentWidth
                           << "hostMatches=" << hostMatches << "settingsMatch=" << settingsMatch
                           << "applicationMatches=" << applicationMatches << "aboutMatches=" << aboutMatches
                           << "securityMatches=" << securityMatches << "shortcutsMatch=" << shortcutsMatch
                           << "sftpMatches=" << sftpMatches << "aiSettingsMatch=" << aiSettingsMatch;
    return hostMatches && settingsMatch && applicationMatches && aboutMatches && securityMatches && shortcutsMatch
           && sftpMatches && aiSettingsMatch && hostCaptured && settingsCaptured && applicationCaptured && aboutCaptured
           && securityCaptured && shortcutsCaptured && sftpCaptured && aiSettingsCaptured;
}

[[nodiscard]] QQuickItem *quickItem(QQuickItem *rootObject, const char *objectName);
[[nodiscard]] QQuickItem *visualQuickItem(QQuickItem *rootObject, const char *objectName);
[[nodiscard]] bool verifyAccessibleButton(QQuickItem *rootObject, const char *objectName, const char *expectedName);
[[nodiscard]] bool verifyAccessibleToggle(QQuickItem *rootObject, const char *objectName, const char *expectedName);

[[nodiscard]] bool applyUiLayoutSmokeTheme(ztermy::AppController &controller, const QString &theme,
                                           const QString &language, const QString &backdrop = QStringLiteral("acrylic"))
{
    return controller.saveApplicationSettings(theme, 1.0, backdrop, QStringLiteral("ztermy"), QStringLiteral("#22C55E"),
                                              {}, QStringLiteral("Cascadia Mono"), 14, false, true, 1.0,
                                              QStringLiteral("terminal"), true, false, true, language, false, true);
}

[[nodiscard]] bool runUiLayoutRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                           const QString &outputDirectory)
{
    window.show();
    processWindowEventsFor(std::chrono::milliseconds{250});
    QQuickItem *initialRootObject = window.rootObject();
    auto *titleBrandIcon = initialRootObject == nullptr
                               ? nullptr
                               : initialRootObject->findChild<QQuickItem *>(QStringLiteral("titleBrandIcon"));
    const QVariant titleBrandTile = titleBrandIcon == nullptr ? QVariant{} : titleBrandIcon->property("tileColor");
    const QVariant titleBrandRibbon = titleBrandIcon == nullptr ? QVariant{} : titleBrandIcon->property("ribbonColor");
    const QVariant titleBrandPrompt = titleBrandIcon == nullptr ? QVariant{} : titleBrandIcon->property("promptColor");
    const qreal titleBrandPromptStroke =
        titleBrandIcon == nullptr ? 0.0 : titleBrandIcon->property("promptStrokeWidth").toReal();
    const bool titleBrandPalettePassed = titleBrandIcon != nullptr && titleBrandTile.isValid()
                                         && titleBrandRibbon.isValid() && titleBrandPrompt.isValid()
                                         && titleBrandTile != titleBrandRibbon && titleBrandPrompt != titleBrandRibbon
                                         && titleBrandPromptStroke >= 0.8;
    const bool darkCompactPassed =
        verifyUiLayoutBreakpoint(window, QSize{500, 360}, true, QStringLiteral("dark"), outputDirectory);
    const bool darkRegularPassed =
        verifyUiLayoutBreakpoint(window, QSize{1120, 800}, false, QStringLiteral("dark"), outputDirectory);
    if (!darkCompactPassed || !darkRegularPassed
        || !applyUiLayoutSmokeTheme(controller, QStringLiteral("light"), QStringLiteral("en")))
    {
        return false;
    }

    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool lightCompactPassed =
        verifyUiLayoutBreakpoint(window, QSize{500, 360}, true, QStringLiteral("light"), outputDirectory);
    const bool lightRegularPassed =
        verifyUiLayoutBreakpoint(window, QSize{1120, 800}, false, QStringLiteral("light"), outputDirectory);
    const bool chineseSaved = applyUiLayoutSmokeTheme(controller, QStringLiteral("dark"), QStringLiteral("zh_CN"));
    processWindowEventsFor(std::chrono::milliseconds{250});
    QQuickItem *rootObject = window.rootObject();
    auto *settingsPane =
        rootObject == nullptr ? nullptr : rootObject->findChild<QObject *>(QStringLiteral("settingsPane"));
    if (rootObject != nullptr)
    {
        rootObject->setProperty("currentPage", QStringLiteral("settings"));
    }
    if (settingsPane != nullptr)
    {
        settingsPane->setProperty("currentCategory", QStringLiteral("shortcuts"));
    }
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{250});
    auto *shortcutSearch =
        rootObject == nullptr ? nullptr : rootObject->findChild<QObject *>(QStringLiteral("shortcutSearch"));
    const bool chineseShortcuts =
        chineseSaved && shortcutSearch != nullptr
        && shortcutSearch->property("placeholderText").toString() == QStringLiteral("搜索操作和快捷键")
        && captureLayout(window, outputDirectory, QStringLiteral("zh-cn-regular-shortcuts"));

    auto *commandPalette =
        rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QStringLiteral("commandPalette"));
    auto *commandPaletteSearch =
        rootObject == nullptr ? nullptr : rootObject->findChild<QObject *>(QStringLiteral("commandPaletteSearch"));
    auto *commandPaletteList =
        rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QStringLiteral("commandPaletteList"));
    const bool paletteOpened =
        commandPalette != nullptr && QMetaObject::invokeMethod(commandPalette, "open", Qt::DirectConnection);
    processWindowEventsFor(std::chrono::milliseconds{250});
    QAccessibleInterface *paletteListInterface =
        commandPaletteList == nullptr ? nullptr : QAccessible::queryAccessibleInterface(commandPaletteList);
    const QVariantList localizedActions = controller.actions();
    const bool chinesePalette =
        paletteOpened && commandPaletteSearch != nullptr
        && commandPaletteSearch->property("placeholderText").toString() == QStringLiteral("输入操作名称或快捷键")
        && paletteListInterface != nullptr
        && paletteListInterface->text(QAccessible::Name) == QStringLiteral("可用命令") && !localizedActions.isEmpty()
        && localizedActions.constFirst().toMap().value(QStringLiteral("label")).toString() == QStringLiteral("命令面板")
        && captureLayout(window, outputDirectory, QStringLiteral("zh-cn-command-palette"));
    if (commandPalette != nullptr)
    {
        static_cast<void>(QMetaObject::invokeMethod(commandPalette, "close", Qt::DirectConnection));
    }
    const bool aiProviderCleared =
        controller.saveAiProviderSettings(QStringLiteral("openai-responses"), QStringLiteral("https://api.openai.com"),
                                          QStringLiteral("/v1/responses"), {}, true, QStringLiteral("ask"));
    const bool englishDark = applyUiLayoutSmokeTheme(controller, QStringLiteral("dark"), QStringLiteral("en"));
    const QString aiTerminalId = englishDark && aiProviderCleared ? controller.startLocalTerminal() : QString{};
    if (rootObject != nullptr)
    {
        rootObject->setProperty("currentPage", QStringLiteral("terminal"));
    }
    const bool aiWorkbenchOpened = !aiTerminalId.isEmpty() && controller.toggleTerminalWorkbench(QStringLiteral("ai"));
    processWindowEventsFor(std::chrono::milliseconds{300});
    auto *aiAssistantPane =
        rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QStringLiteral("aiAssistantPane"));
    auto *aiPromptEditor =
        rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QStringLiteral("aiPromptEditor"));
    auto *aiProviderSetupCard = quickItem(rootObject, "aiProviderSetupCard");
    auto *aiProviderSetupButton = quickItem(rootObject, "aiProviderSetupButton");
    auto *aiComposerPanel = quickItem(rootObject, "aiComposerPanel");
    QAccessibleInterface *aiProviderSetupInterface =
        aiProviderSetupCard == nullptr ? nullptr : QAccessible::queryAccessibleInterface(aiProviderSetupCard);
    const bool aiProviderSetupRegular =
        aiProviderSetupCard != nullptr && aiProviderSetupCard->isVisible() && aiPromptEditor != nullptr
        && !aiPromptEditor->isEnabled() && aiComposerPanel != nullptr && !aiComposerPanel->isVisible()
        && aiProviderSetupInterface != nullptr && aiProviderSetupInterface->role() == QAccessible::Pane
        && aiProviderSetupInterface->text(QAccessible::Name) == QStringLiteral("Set up the terminal assistant")
        && verifyAccessibleButton(rootObject, "aiProviderSetupButton", "Open AI provider settings")
        && captureLayout(window, outputDirectory, QStringLiteral("dark-regular-ai-provider-setup"));
    window.resize(QSize{500, 360});
    processWindowEventsFor(std::chrono::milliseconds{200});
    const QPointF compactSetupTopLeft = aiProviderSetupCard == nullptr || aiAssistantPane == nullptr
                                            ? QPointF{}
                                            : aiProviderSetupCard->mapToItem(aiAssistantPane, QPointF{});
    const qreal compactSetupRight =
        aiProviderSetupCard == nullptr ? 0.0 : compactSetupTopLeft.x() + aiProviderSetupCard->width();
    const bool aiProviderSetupCompact =
        aiProviderSetupCard != nullptr && aiProviderSetupCard->isVisible() && aiAssistantPane != nullptr
        && compactSetupTopLeft.x() >= -0.5 && compactSetupRight <= aiAssistantPane->width() + 0.5
        && captureLayout(window, outputDirectory, QStringLiteral("dark-compact-ai-provider-setup"));
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{200});
    const bool aiProviderSetupInvoked =
        aiProviderSetupButton != nullptr
        && QMetaObject::invokeMethod(aiProviderSetupButton, "click", Qt::DirectConnection);
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool aiSettingsOpened = aiProviderSetupInvoked && rootObject != nullptr
                                  && rootObject->property("currentPage").toString() == QStringLiteral("settings")
                                  && settingsPane != nullptr
                                  && settingsPane->property("currentCategory").toString() == QStringLiteral("ai");
    if (rootObject != nullptr)
    {
        rootObject->setProperty("currentPage", QStringLiteral("terminal"));
    }
    const bool aiProviderConfigured = controller.saveAiProviderSettings(
        QStringLiteral("ollama"), QStringLiteral("http://127.0.0.1:11434"), QStringLiteral("/api/chat"),
        QStringLiteral("qwen3"), true, QStringLiteral("ask"));
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool aiProviderSetupDismissed = aiProviderConfigured && aiProviderSetupCard != nullptr
                                          && !aiProviderSetupCard->isVisible() && aiPromptEditor != nullptr
                                          && aiPromptEditor->isEnabled() && aiComposerPanel != nullptr
                                          && aiComposerPanel->isVisible();
    const bool aiProviderOnboardingPassed =
        aiProviderSetupRegular && aiProviderSetupCompact && aiSettingsOpened && aiProviderSetupDismissed;
    QFile aiProviderOnboardingArtifact{
        QDir(outputDirectory).filePath(QStringLiteral("ai-provider-onboarding-contract.txt"))};
    if (aiProviderOnboardingArtifact.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream artifact{&aiProviderOnboardingArtifact};
        artifact << "setupRegular=" << aiProviderSetupRegular << '\n';
        artifact << "setupCompact=" << aiProviderSetupCompact << '\n';
        artifact << "settingsOpened=" << aiSettingsOpened << '\n';
        artifact << "setupDismissed=" << aiProviderSetupDismissed << '\n';
        artifact << "setupRole="
                 << (aiProviderSetupInterface == nullptr ? -1 : static_cast<int>(aiProviderSetupInterface->role()))
                 << '\n';
        artifact << "setupName="
                 << (aiProviderSetupInterface == nullptr ? QString{}
                                                         : aiProviderSetupInterface->text(QAccessible::Name))
                 << '\n';
    }
    const QVariantList initialAiContextItems = controller.activeAiContextItems();
    const QString firstAiContextId =
        initialAiContextItems.isEmpty()
            ? QString{}
            : initialAiContextItems.constFirst().toMap().value(QStringLiteral("id")).toString();
    const bool contextPinned =
        !firstAiContextId.isEmpty() && controller.setAiContextItemPinned(firstAiContextId, true)
        && std::ranges::any_of(controller.activeAiContextItems(), [&firstAiContextId](const QVariant &item) {
               const QVariantMap map = item.toMap();
               return map.value(QStringLiteral("id")).toString() == firstAiContextId
                      && map.value(QStringLiteral("pinned")).toBool();
           });
    const bool contextRemoved =
        contextPinned && controller.removeAiContextItem(firstAiContextId)
        && std::ranges::none_of(controller.activeAiContextItems(), [&firstAiContextId](const QVariant &item) {
               return item.toMap().value(QStringLiteral("id")).toString() == firstAiContextId;
           });
    controller.resetAiContextItems();
    const bool contextRestored =
        contextRemoved
        && std::ranges::any_of(controller.activeAiContextItems(), [&firstAiContextId](const QVariant &item) {
               return item.toMap().value(QStringLiteral("id")).toString() == firstAiContextId;
           });
    QClipboard *clipboard = QGuiApplication::clipboard();
    std::unique_ptr<QMimeData> savedClipboard = cloneMimeData(clipboard == nullptr ? nullptr : clipboard->mimeData());
    QImage clipboardImage(32, 24, QImage::Format_ARGB32_Premultiplied);
    clipboardImage.fill(QColor(QStringLiteral("#2D7FF9")));
    if (clipboard != nullptr)
    {
        clipboard->setImage(clipboardImage);
    }
    const auto pasteIntoAiEditor = [&window, aiPromptEditor] {
        if (aiPromptEditor == nullptr)
        {
            return;
        }
        aiPromptEditor->forceActiveFocus(Qt::TabFocusReason);
        processWindowEventsFor(std::chrono::milliseconds{40});
        qt_handleKeyEvent(&window, QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        qt_handleKeyEvent(&window, QEvent::KeyRelease, Qt::Key_V, Qt::ControlModifier);
    };
    pasteIntoAiEditor();
    const bool clipboardImageAttached = processWindowEventsUntil(
        [&controller] {
            return std::ranges::any_of(controller.activeAiContextItems(), [](const QVariant &item) {
                const QVariantMap value = item.toMap();
                return value.value(QStringLiteral("kind")).toString() == QStringLiteral("image")
                       && value.value(QStringLiteral("title")).toString() == QStringLiteral("clipboard-image.png");
            });
        },
        std::chrono::milliseconds{2'000});
    QString clipboardImageId;
    if (clipboardImageAttached)
    {
        const QVariantList contextItems = controller.activeAiContextItems();
        const auto item = std::ranges::find_if(contextItems, [](const QVariant &candidate) {
            const QVariantMap value = candidate.toMap();
            return value.value(QStringLiteral("kind")).toString() == QStringLiteral("image")
                   && value.value(QStringLiteral("title")).toString() == QStringLiteral("clipboard-image.png");
        });
        if (item != contextItems.cend())
        {
            clipboardImageId = item->toMap().value(QStringLiteral("id")).toString();
        }
    }
    const bool clipboardImageRemoved = !clipboardImageId.isEmpty() && controller.removeAiContextItem(clipboardImageId);

    QFile clipboardFile{QDir(outputDirectory).filePath(QStringLiteral("clipboard-context.txt"))};
    const bool clipboardFileWritten = clipboardFile.open(QIODevice::WriteOnly | QIODevice::Text)
                                      && clipboardFile.write("clipboard file context\n") == qint64{23};
    clipboardFile.close();
    if (clipboard != nullptr && clipboardFileWritten)
    {
        auto fileMimeData = std::make_unique<QMimeData>();
        fileMimeData->setUrls({QUrl::fromLocalFile(clipboardFile.fileName())});
        clipboard->setMimeData(fileMimeData.release());
    }
    pasteIntoAiEditor();
    const bool clipboardFileAttached = processWindowEventsUntil(
        [&controller] {
            return std::ranges::any_of(controller.activeAiContextItems(), [](const QVariant &item) {
                const QVariantMap value = item.toMap();
                return value.value(QStringLiteral("kind")).toString() == QStringLiteral("attachment")
                       && value.value(QStringLiteral("title")).toString() == QStringLiteral("clipboard-context.txt");
            });
        },
        std::chrono::milliseconds{2'000});
    QString clipboardFileId;
    if (clipboardFileAttached)
    {
        const QVariantList contextItems = controller.activeAiContextItems();
        const auto item = std::ranges::find_if(contextItems, [](const QVariant &candidate) {
            const QVariantMap value = candidate.toMap();
            return value.value(QStringLiteral("kind")).toString() == QStringLiteral("attachment")
                   && value.value(QStringLiteral("title")).toString() == QStringLiteral("clipboard-context.txt");
        });
        if (item != contextItems.cend())
        {
            clipboardFileId = item->toMap().value(QStringLiteral("id")).toString();
        }
    }
    const bool clipboardFileRemoved = !clipboardFileId.isEmpty() && controller.removeAiContextItem(clipboardFileId);

    constexpr auto clipboardTextFixture = "clipboard-text-fixture";
    if (aiPromptEditor != nullptr)
    {
        aiPromptEditor->setProperty("text", QString{});
    }
    if (clipboard != nullptr)
    {
        clipboard->setText(QString::fromLatin1(clipboardTextFixture));
        processWindowEventsFor(std::chrono::milliseconds{40});
    }
    pasteIntoAiEditor();
    processWindowEventsFor(std::chrono::milliseconds{40});
    const bool clipboardTextPasted =
        aiPromptEditor != nullptr
        && aiPromptEditor->property("text").toString() == QString::fromLatin1(clipboardTextFixture);
    if (aiPromptEditor != nullptr)
    {
        aiPromptEditor->setProperty("text", QString{});
    }
    if (clipboard != nullptr)
    {
        clipboard->setMimeData(savedClipboard.release());
    }
    const bool clipboardPastePassed = clipboard != nullptr && aiPromptEditor != nullptr && clipboardImageAttached
                                      && clipboardImageRemoved && clipboardFileWritten && clipboardFileAttached
                                      && clipboardFileRemoved && clipboardTextPasted;
    QFile clipboardArtifact{QDir(outputDirectory).filePath(QStringLiteral("ai-clipboard-paste-contract.txt"))};
    if (clipboardArtifact.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream artifact{&clipboardArtifact};
        artifact << "imageAttached=" << clipboardImageAttached << '\n';
        artifact << "imageRemoved=" << clipboardImageRemoved << '\n';
        artifact << "fileAttached=" << clipboardFileAttached << '\n';
        artifact << "fileRemoved=" << clipboardFileRemoved << '\n';
        artifact << "plainTextPasted=" << clipboardTextPasted << '\n';
    }
    if (aiAssistantPane != nullptr)
    {
        aiAssistantPane->setProperty("contextExpanded", true);
    }
    if (aiPromptEditor != nullptr)
    {
        aiPromptEditor->forceActiveFocus(Qt::TabFocusReason);
        processWindowEventsFor(std::chrono::milliseconds{40});
    }
    auto *aiConversation = qobject_cast<ztermy::ai::AiConversationModel *>(controller.activeAiConversation());
    bool aiMarkdownFixturePrepared = false;
    if (aiConversation != nullptr)
    {
        static_cast<void>(aiConversation->appendUserMessage(
            QStringLiteral("Summarize the terminal state."), {},
            std::vector<ztermy::ai::AiContextAttachmentSummary>{
                {.title = "Terminal command: Get-Location", .kind = "command", .quality = "rich"},
                {.title = "deployment-notes.md", .kind = "file", .quality = "none", .truncated = true}}));
        const std::uint64_t markdownMessageId = aiConversation->beginAssistantMessage();
        const bool markdownAdded = aiConversation->appendAssistantDelta(
            markdownMessageId,
            QStringLiteral("## Terminal summary\n\n- Service is **ready**\n- Review the table before continuing\n\n"
                           "| Item | State |\n| --- | --- |\n| Shell | PowerShell |\n| Session | Local |\n\n"
                           "```powershell\nGet-Location\n```"));
        const bool toolActivityAdded = aiConversation->upsertAssistantToolActivity(
            markdownMessageId, QStringLiteral("layout-fixture-tool"), QStringLiteral("read_terminal_frame"),
            QStringLiteral("Read the visible terminal frame"), QStringLiteral("succeeded"), QStringLiteral("ok"), false,
            false);
        const bool toolDetailsAdded = aiConversation->setAssistantToolDetails(
            markdownMessageId, QStringLiteral("layout-fixture-tool"), QStringLiteral("{\n  \"after_revision\": 12\n}"),
            QStringLiteral("{\n  \"ok\": true,\n  \"revision\": 13\n}"));
        const bool secondToolAdded = aiConversation->upsertAssistantToolActivity(
            markdownMessageId, QStringLiteral("layout-fixture-tool-2"), QStringLiteral("read_terminal_info"),
            QStringLiteral("Read current shell metadata"), QStringLiteral("failed"), QStringLiteral("timeout"), false,
            false);
        aiMarkdownFixturePrepared = markdownAdded && toolActivityAdded && toolDetailsAdded && secondToolAdded
                                    && aiConversation->completeAssistantMessage(markdownMessageId);
    }
    processWindowEventsFor(std::chrono::milliseconds{500});
    QQuickItem *aiContextToggle = quickItem(rootObject, "aiContextToggle");
    QAccessibleInterface *aiContextInterface =
        aiContextToggle == nullptr ? nullptr : QAccessible::queryAccessibleInterface(aiContextToggle);
    const bool aiLauncherAccessible =
        verifyAccessibleToggle(rootObject, "terminalAiAssistantButton", "Terminal AI assistant");
    const bool aiToolbarAccessible = verifyAccessibleToggle(rootObject, "terminalAiAction", "AI assistant");
    const bool aiHistoryAccessible =
        verifyAccessibleToggle(rootObject, "aiHistoryToggle", "Show AI conversation history");
    const bool aiNewConversationAccessible =
        verifyAccessibleButton(rootObject, "aiNewConversationButton", "Start a new AI conversation");
    const bool aiMoreAccessible =
        verifyAccessibleButton(rootObject, "aiConversationMoreButton", "More conversation actions");
    const bool aiSendAccessible = verifyAccessibleButton(rootObject, "aiSendButton", "Send");
    const bool aiToolGroupAccessible = verifyAccessibleButton(rootObject, "aiToolGroupToggle", "Expand · Used 2 tools");
    const bool aiToolEvidenceAccessible =
        verifyAccessibleButton(rootObject, "aiToolEvidenceNotice",
                               "Some tool results were unavailable. This answer may be based on partial evidence.");
    QQuickItem *aiMessageContextAttachments = visualQuickItem(rootObject, "aiMessageContextAttachment");
    QAccessibleInterface *aiMessageContextInterface =
        aiMessageContextAttachments == nullptr ? nullptr
                                               : QAccessible::queryAccessibleInterface(aiMessageContextAttachments);
    const bool aiMessageContextAccessible =
        aiMessageContextInterface != nullptr && aiMessageContextInterface->role() == QAccessible::StaticText
        && aiMessageContextInterface->text(QAccessible::Name)
               == QStringLiteral("Attached context: Terminal command: Get-Location");
    const bool aiContextAccessible =
        aiContextInterface != nullptr && aiContextInterface->role() == QAccessible::Button
        && aiContextInterface->text(QAccessible::Name)
               == QStringLiteral("Request context · %1 item(s)").arg(controller.activeAiContextItems().size());
    const bool aiAccessibilityPassed = aiLauncherAccessible && aiToolbarAccessible && aiHistoryAccessible
                                       && aiNewConversationAccessible && aiMoreAccessible && aiSendAccessible
                                       && aiContextAccessible && aiToolGroupAccessible && aiToolEvidenceAccessible
                                       && aiMessageContextAccessible;
    QAccessibleInterface *aiPaneInterface =
        aiAssistantPane == nullptr ? nullptr : QAccessible::queryAccessibleInterface(aiAssistantPane);
    QAccessibleInterface *aiPromptInterface =
        aiPromptEditor == nullptr ? nullptr : QAccessible::queryAccessibleInterface(aiPromptEditor);
    const bool aiSemanticRolesPassed =
        aiPaneInterface != nullptr && aiPaneInterface->role() == QAccessible::Pane
        && aiPaneInterface->text(QAccessible::Name) == QStringLiteral("Terminal AI assistant")
        && aiPromptInterface != nullptr && aiPromptInterface->role() == QAccessible::EditableText
        && aiPromptInterface->text(QAccessible::Name) == QStringLiteral("AI message");
    QFile aiAccessibilityArtifact{QDir(outputDirectory).filePath(QStringLiteral("ai-accessibility-contract.txt"))};
    if (aiAccessibilityArtifact.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream artifact{&aiAccessibilityArtifact};
        artifact << "buttons=" << aiAccessibilityPassed << '\n';
        artifact << "launcher=" << aiLauncherAccessible << '\n';
        artifact << "toolbar=" << aiToolbarAccessible << '\n';
        artifact << "history=" << aiHistoryAccessible << '\n';
        artifact << "newConversation=" << aiNewConversationAccessible << '\n';
        artifact << "more=" << aiMoreAccessible << '\n';
        artifact << "send=" << aiSendAccessible << '\n';
        artifact << "toolGroup=" << aiToolGroupAccessible << '\n';
        artifact << "toolEvidence=" << aiToolEvidenceAccessible << '\n';
        artifact << "messageContext=" << aiMessageContextAccessible << '\n';
        artifact << "messageContextRole="
                 << (aiMessageContextInterface == nullptr ? -1 : static_cast<int>(aiMessageContextInterface->role()))
                 << '\n';
        artifact << "messageContextName="
                 << (aiMessageContextInterface == nullptr ? QString{}
                                                          : aiMessageContextInterface->text(QAccessible::Name))
                 << '\n';
        artifact << "context=" << aiContextAccessible << '\n';
        artifact << "semanticRoles=" << aiSemanticRolesPassed << '\n';
        artifact << "contextRole="
                 << (aiContextInterface == nullptr ? -1 : static_cast<int>(aiContextInterface->role())) << '\n';
        artifact << "contextName="
                 << (aiContextInterface == nullptr ? QString{} : aiContextInterface->text(QAccessible::Name)) << '\n';
        artifact << "paneRole=" << (aiPaneInterface == nullptr ? -1 : static_cast<int>(aiPaneInterface->role()))
                 << '\n';
        artifact << "paneName=" << (aiPaneInterface == nullptr ? QString{} : aiPaneInterface->text(QAccessible::Name))
                 << '\n';
        artifact << "promptRole=" << (aiPromptInterface == nullptr ? -1 : static_cast<int>(aiPromptInterface->role()))
                 << '\n';
        artifact << "promptName="
                 << (aiPromptInterface == nullptr ? QString{} : aiPromptInterface->text(QAccessible::Name)) << '\n';
    }
    const bool aiDarkCaptured = aiWorkbenchOpened && contextRestored && aiMarkdownFixturePrepared
                                && aiAssistantPane != nullptr && aiAssistantPane->isVisible()
                                && aiPromptEditor != nullptr && aiAccessibilityPassed && aiSemanticRolesPassed
                                && captureLayout(window, outputDirectory, QStringLiteral("dark-regular-ai-assistant"));
    auto *aiApprovalCard = quickItem(rootObject, "aiToolApprovalCard");
    auto *aiApprovalCommandViewport = quickItem(rootObject, "aiApprovalCommandViewport");
    auto *aiApprovalCommandText = quickItem(rootObject, "aiApprovalCommandText");
    if (aiApprovalCommandText != nullptr)
    {
        aiApprovalCommandText->setProperty(
            "text", QStringLiteral("cat > ~/exercises/README.md << 'ENDOFFILE'\n")
                        + QStringLiteral("Long approval command content must scroll without painting ").repeated(64)
                        + QStringLiteral("\nENDOFFILE"));
    }
    if (aiApprovalCard != nullptr)
    {
        aiApprovalCard->setVisible(true);
    }
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool longApprovalBounded =
        aiApprovalCard != nullptr && aiApprovalCard->isVisible() && aiApprovalCommandViewport != nullptr
        && aiApprovalCommandViewport->clip() && aiApprovalCommandViewport->height() <= 120.5
        && aiApprovalCommandViewport->property("contentHeight").toReal() > aiApprovalCommandViewport->height()
        && aiApprovalCommandText != nullptr && aiApprovalCommandText->height() > aiApprovalCommandViewport->height()
        && captureLayout(window, outputDirectory, QStringLiteral("dark-regular-ai-long-approval"));
    if (aiApprovalCard != nullptr)
    {
        aiApprovalCard->setVisible(false);
    }
    window.resize(QSize{500, 360});
    processWindowEventsFor(std::chrono::milliseconds{250});
    auto *aiPromptHorizontalScrollBar = quickItem(rootObject, "aiPromptHorizontalScrollBar");
    const bool compactPromptHasNoHorizontalScroll =
        aiPromptHorizontalScrollBar != nullptr && !aiPromptHorizontalScrollBar->isVisible();
    constexpr std::array compactAiActionNames{
        "aiHistoryToggle", "aiNewConversationButton", "aiConversationMoreButton", "aiContextToggle",
        "aiSendButton",    "aiAttachmentDropArea",    "aiCompactionNotice"};
    bool compactAiActionsInsidePanel = aiAssistantPane != nullptr;
    QFile compactAiArtifact{QDir(outputDirectory).filePath(QStringLiteral("compact-ai-layout-contract.txt"))};
    const bool compactAiArtifactOpened = compactAiArtifact.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream compactAiStream{&compactAiArtifact};
    for (const char *objectName : compactAiActionNames)
    {
        auto *item = visualQuickItem(rootObject, objectName);
        const QPointF topLeft =
            item == nullptr || aiAssistantPane == nullptr ? QPointF{} : item->mapToItem(aiAssistantPane, QPointF{});
        const qreal right = item == nullptr ? 0.0 : topLeft.x() + item->width();
        constexpr qreal tolerance = 0.5;
        const bool inside = item != nullptr && aiAssistantPane != nullptr && topLeft.x() >= -tolerance
                            && right <= aiAssistantPane->width() + tolerance;
        compactAiActionsInsidePanel = compactAiActionsInsidePanel && inside;
        if (compactAiArtifactOpened)
        {
            compactAiStream << objectName << ": found=" << (item != nullptr) << ", x=" << topLeft.x()
                            << ", width=" << (item == nullptr ? 0.0 : item->width()) << ", right=" << right
                            << ", inside=" << inside << '\n';
        }
    }
    if (compactAiArtifactOpened)
    {
        compactAiStream << "paneWidth=" << (aiAssistantPane == nullptr ? 0.0 : aiAssistantPane->width()) << '\n';
        compactAiStream << "promptHorizontalScrollVisible="
                        << (aiPromptHorizontalScrollBar != nullptr && aiPromptHorizontalScrollBar->isVisible()) << '\n';
    }
    const bool aiCompactCaptured =
        aiAssistantPane != nullptr && aiAssistantPane->isVisible() && aiAssistantPane->width() > 0.0
        && aiAssistantPane->width() <= window.width() && compactAiActionsInsidePanel
        && compactPromptHasNoHorizontalScroll
        && captureLayout(window, outputDirectory, QStringLiteral("dark-compact-ai-assistant"));
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool aiLightTheme = applyUiLayoutSmokeTheme(controller, QStringLiteral("light"), QStringLiteral("en"));
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool aiLightCaptured =
        aiLightTheme && aiAssistantPane != nullptr && aiAssistantPane->isVisible()
        && captureLayout(window, outputDirectory, QStringLiteral("light-regular-ai-assistant"));
    const bool restoredDark = applyUiLayoutSmokeTheme(controller, QStringLiteral("dark"), QStringLiteral("en"));

    controller.clearAiConversation();
    QTcpServer providerFailureServer;
    const bool providerFailureServerListening = providerFailureServer.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&providerFailureServer, &QTcpServer::newConnection, &controller, [&providerFailureServer] {
        while (QTcpSocket *socket = providerFailureServer.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                QByteArray request = socket->property("requestBuffer").toByteArray();
                request += socket->readAll();
                socket->setProperty("requestBuffer", request);
                if (!request.contains("\r\n\r\n") || socket->property("responseSent").toBool())
                {
                    return;
                }
                socket->setProperty("responseSent", true);
                const QByteArray body =
                    R"({"error":{"message":"Invalid API key","type":"authentication_error","code":"invalid_api_key"}})";
                QByteArray response = "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nConnection: "
                                      "close\r\nContent-Length: ";
                response += QByteArray::number(body.size());
                response += "\r\n\r\n";
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });
    const QString providerFailureEndpoint =
        QStringLiteral("http://127.0.0.1:%1").arg(providerFailureServer.serverPort());
    const bool providerFailureConfigured =
        providerFailureServerListening
        && controller.saveAiProviderSettings(QStringLiteral("ollama"), providerFailureEndpoint,
                                             QStringLiteral("/api/chat"), QStringLiteral("qwen3"), false,
                                             QStringLiteral("ask"));
    const bool providerFailureStarted =
        providerFailureConfigured && controller.sendAiMessage(QStringLiteral("Inspect this terminal."));
    const bool providerFailureReached = providerFailureStarted
                                        && processWindowEventsUntil(
                                            [&controller] {
                                                return controller.activeAiState() == QStringLiteral("error");
                                            },
                                            std::chrono::seconds{5});
    processWindowEventsFor(std::chrono::milliseconds{150});
    auto *aiGlobalErrorStatus = visualQuickItem(rootObject, "aiGlobalErrorStatus");
    auto *aiErrorSettingsAction = visualQuickItem(rootObject, "aiErrorSettingsAction");
    auto *aiErrorRetryAction = visualQuickItem(rootObject, "aiErrorRetryAction");
    auto *aiErrorNewConversationAction = visualQuickItem(rootObject, "aiErrorNewConversationAction");
    const QVariantMap providerFailureRecovery = controller.activeAiErrorRecovery();
    const bool providerFailureRegular =
        providerFailureReached
        && providerFailureRecovery.value(QStringLiteral("code")).toString() == QStringLiteral("authentication")
        && providerFailureRecovery.value(QStringLiteral("messageAnchored")).toBool() && aiGlobalErrorStatus != nullptr
        && !aiGlobalErrorStatus->isVisible() && aiErrorSettingsAction != nullptr && aiErrorSettingsAction->isVisible()
        && aiErrorRetryAction != nullptr && aiErrorRetryAction->isVisible() && aiErrorNewConversationAction != nullptr
        && !aiErrorNewConversationAction->isVisible()
        && verifyAccessibleButton(rootObject, "aiErrorSettingsAction", "Open AI settings to fix the provider")
        && verifyAccessibleButton(rootObject, "aiErrorRetryAction", "Retry the failed assistant response")
        && captureLayout(window, outputDirectory, QStringLiteral("dark-regular-ai-provider-error"));
    window.resize(QSize{500, 360});
    processWindowEventsFor(std::chrono::milliseconds{200});
    const QPointF recoverySettingsTopLeft = aiErrorSettingsAction == nullptr || aiAssistantPane == nullptr
                                                ? QPointF{}
                                                : aiErrorSettingsAction->mapToItem(aiAssistantPane, QPointF{});
    const qreal recoverySettingsRight =
        aiErrorSettingsAction == nullptr ? 0.0 : recoverySettingsTopLeft.x() + aiErrorSettingsAction->width();
    const bool providerFailureCompact =
        aiErrorSettingsAction != nullptr && aiErrorSettingsAction->isVisible() && aiAssistantPane != nullptr
        && recoverySettingsTopLeft.x() >= -0.5 && recoverySettingsRight <= aiAssistantPane->width() + 0.5
        && captureLayout(window, outputDirectory, QStringLiteral("dark-compact-ai-provider-error"));
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool providerFailureSettingsInvoked =
        aiErrorSettingsAction != nullptr
        && QMetaObject::invokeMethod(aiErrorSettingsAction, "click", Qt::DirectConnection);
    processWindowEventsFor(std::chrono::milliseconds{100});
    const bool providerFailureSettingsOpened =
        providerFailureSettingsInvoked && rootObject != nullptr
        && rootObject->property("currentPage").toString() == QStringLiteral("settings") && settingsPane != nullptr
        && settingsPane->property("currentCategory").toString() == QStringLiteral("ai");
    if (rootObject != nullptr)
    {
        rootObject->setProperty("currentPage", QStringLiteral("terminal"));
    }
    const bool providerFailureRecoveryPassed =
        providerFailureRegular && providerFailureCompact && providerFailureSettingsOpened;
    QFile providerFailureArtifact{
        QDir(outputDirectory).filePath(QStringLiteral("ai-provider-failure-recovery-contract.txt"))};
    if (providerFailureArtifact.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream artifact{&providerFailureArtifact};
        artifact << "regular=" << providerFailureRegular << '\n';
        artifact << "compact=" << providerFailureCompact << '\n';
        artifact << "settingsOpened=" << providerFailureSettingsOpened << '\n';
        artifact << "code=" << providerFailureRecovery.value(QStringLiteral("code")).toString() << '\n';
        artifact << "globalErrorVisible=" << (aiGlobalErrorStatus != nullptr && aiGlobalErrorStatus->isVisible())
                 << '\n';
    }
    providerFailureServer.close();
    controller.clearAiConversation();
    if (!aiTerminalId.isEmpty())
    {
        static_cast<void>(controller.closeTerminalTab(aiTerminalId));
    }
    return titleBrandPalettePassed && lightCompactPassed && lightRegularPassed && chineseShortcuts && chinesePalette
           && aiWorkbenchOpened && aiProviderOnboardingPassed && aiDarkCaptured && aiCompactCaptured && aiLightCaptured
           && longApprovalBounded && clipboardPastePassed && providerFailureRecoveryPassed && restoredDark;
}

[[nodiscard]] QQuickItem *quickItem(QQuickItem *rootObject, const char *objectName)
{
    return rootObject == nullptr ? nullptr : rootObject->findChild<QQuickItem *>(QString::fromLatin1(objectName));
}

[[nodiscard]] QQuickItem *visualQuickItem(QQuickItem *rootObject, const char *objectName)
{
    if (rootObject == nullptr)
    {
        return nullptr;
    }
    const QString expectedName = QString::fromLatin1(objectName);
    std::vector<QQuickItem *> pending{rootObject};
    QQuickItem *fallback = nullptr;
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        QQuickItem *candidate = pending[index];
        if (candidate->objectName() == expectedName)
        {
            fallback = fallback == nullptr ? candidate : fallback;
            if (candidate->isVisible())
            {
                return candidate;
            }
        }
        const QList<QQuickItem *> children = candidate->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return fallback;
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

[[nodiscard]] bool terminalViewportHasFocus(const ztermy::NativeWindow &window)
{
    const QString focusName = namedFocusItem(window);
    return focusName == QStringLiteral("terminalViewport") || focusName.startsWith(QStringLiteral("terminalViewport-"));
}

[[nodiscard]] QQuickItem *terminalViewportItem(QQuickItem *rootObject)
{
    if (rootObject == nullptr)
    {
        return nullptr;
    }
    std::vector<QQuickItem *> pending{rootObject};
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        QQuickItem *candidate = pending[index];
        const QString name = candidate->objectName();
        if (name == QStringLiteral("terminalViewport") || name.startsWith(QStringLiteral("terminalViewport-")))
        {
            return candidate;
        }
        const QList<QQuickItem *> children = candidate->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return nullptr;
}

void sendKey(ztermy::NativeWindow &window, const Qt::Key key, const Qt::KeyboardModifiers modifiers = {})
{
    qt_handleKeyEvent(&window, QEvent::KeyPress, key, modifiers);
    QCoreApplication::processEvents();
    qt_handleKeyEvent(&window, QEvent::KeyRelease, key, modifiers);
    processWindowEventsFor(std::chrono::milliseconds{40});
}

void sendText(ztermy::NativeWindow &window, const QStringView text)
{
    for (const QChar character : text)
    {
        qt_handleKeyEvent(&window, QEvent::KeyPress, Qt::Key_unknown, {}, QString{character});
        QCoreApplication::processEvents();
        qt_handleKeyEvent(&window, QEvent::KeyRelease, Qt::Key_unknown, {}, QString{character});
    }
    processWindowEventsFor(std::chrono::milliseconds{40});
}

void sendMouseClick(ztermy::NativeWindow &window, QQuickItem &item, const QPointF itemPosition)
{
    static int timestamp = 1;
    const QPointF local = item.mapToScene(itemPosition);
    const QPointF global = window.mapToGlobal(local.toPoint());
    qt_handleMouseEvent(&window, local, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress, {},
                        timestamp++);
    QCoreApplication::processEvents();
    qt_handleMouseEvent(&window, local, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease, {},
                        timestamp++);
    processWindowEventsFor(std::chrono::milliseconds{80});
}

void sendMouseMove(ztermy::NativeWindow &window, QQuickItem &item, const QPointF itemPosition)
{
    static int timestamp = 10'000;
    const QPointF local = item.mapToScene(itemPosition);
    const QPointF global = window.mapToGlobal(local.toPoint());
    qt_handleMouseEvent(&window, local, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, {}, timestamp++);
    processWindowEventsFor(std::chrono::milliseconds{250});
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
    QQuickItem *item = visualQuickItem(rootObject, objectName);
    QAccessibleInterface *interface = item == nullptr ? nullptr : QAccessible::queryAccessibleInterface(item);
    const QString expected = QString::fromUtf8(expectedName);
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

[[nodiscard]] bool verifyAccessibleToggle(QQuickItem *rootObject, const char *objectName, const char *expectedName)
{
    QQuickItem *item = visualQuickItem(rootObject, objectName);
    QAccessibleInterface *interface = item == nullptr ? nullptr : QAccessible::queryAccessibleInterface(item);
    const QString expected = QString::fromUtf8(expectedName);
    const bool toggleRole = interface != nullptr
                            && (interface->role() == QAccessible::Button || interface->role() == QAccessible::CheckBox);
    if (!toggleRole || interface->text(QAccessible::Name) != expected)
    {
        qCWarning(applicationLog) << "Accessible toggle contract mismatch"
                                  << "object=" << objectName << "expectedName=" << expected
                                  << "hasInterface=" << (interface != nullptr)
                                  << "actualRole=" << (interface == nullptr ? -1 : static_cast<int>(interface->role()))
                                  << "actualName="
                                  << (interface == nullptr ? QString{} : interface->text(QAccessible::Name));
        return false;
    }
    return true;
}

[[nodiscard]] bool verifyFontPickerKeyboard(ztermy::NativeWindow &window, QQuickItem *rootObject,
                                            const char *pickerObjectName, const char *searchObjectName)
{
    QQuickItem *picker = quickItem(rootObject, pickerObjectName);
    if (!focusItem(window, picker, QString::fromLatin1(pickerObjectName)))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    const bool searchFocused = processWindowEventsUntil(
        [&window, searchObjectName] {
            return namedFocusItem(window) == QString::fromLatin1(searchObjectName);
        },
        std::chrono::seconds{1});
    sendKey(window, Qt::Key_Escape);
    const bool pickerFocusRestored = processWindowEventsUntil(
        [&window, pickerObjectName] {
            return namedFocusItem(window) == QString::fromLatin1(pickerObjectName);
        },
        std::chrono::seconds{1});
    if (!searchFocused || !pickerFocusRestored)
    {
        qCWarning(applicationLog) << "Font picker keyboard route failed"
                                  << "picker=" << pickerObjectName << "search=" << searchObjectName
                                  << "searchFocused=" << searchFocused << "focusRestored=" << pickerFocusRestored
                                  << "actualFocus=" << namedFocusItem(window);
    }
    return searchFocused && pickerFocusRestored;
}

[[nodiscard]] bool verifySettingsTabOrder(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                          QQuickItem *rootObject)
{
    const auto verifyOrder = [&window, rootObject](const auto &order) {
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
                qCWarning(applicationLog)
                    << "Settings Tab order mismatch"
                    << "index=" << index << "expected=" << expectedName << "actual=" << actualName;
                return false;
            }
        }
        return true;
    };

    QQuickItem *appearanceCategory = quickItem(rootObject, "settingsAppearanceCategory");
    if (!focusItem(window, appearanceCategory, QStringLiteral("settingsAppearanceCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    auto *settingsPane = rootObject->findChild<QObject *>(QStringLiteral("settingsPane"));
    if (settingsPane == nullptr || settingsPane->property("currentCategory").toString() != QStringLiteral("appearance"))
    {
        qCWarning(applicationLog) << "Appearance settings category did not activate from the keyboard";
        return false;
    }
    constexpr std::array appearanceOrder{
        "settingsLanguage", "settingsUiFont", "settingsTheme",   "settingsAccent", "settingsBackdrop",
        "settingsOpacity",  "settingsReset",  "settingsDiscard", "settingsApply",
    };
    if (!verifyOrder(appearanceOrder))
    {
        return false;
    }

    QQuickItem *sftpCategory = quickItem(rootObject, "settingsSftpCategory");
    if (!focusItem(window, sftpCategory, QStringLiteral("settingsSftpCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("sftp"))
    {
        qCWarning(applicationLog) << "SFTP settings category did not activate from the keyboard";
        return false;
    }
    constexpr std::array sftpOrder{
        "settingsSftpShowHidden", "settingsSftpConfirmDelete", "settingsReset", "settingsDiscard", "settingsApply",
    };
    if (!verifyOrder(sftpOrder))
    {
        return false;
    }

    QQuickItem *securityCategory = quickItem(rootObject, "settingsSecurityCategory");
    if (!focusItem(window, securityCategory, QStringLiteral("settingsSecurityCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("security"))
    {
        qCWarning(applicationLog) << "Security settings category did not activate from the keyboard";
        return false;
    }
    QQuickItem *portablePassword = quickItem(rootObject, "settingsPortableVaultPassword");
    QQuickItem *portablePasswordConfirm = quickItem(rootObject, "settingsPortableVaultPasswordConfirm");
    if (portablePassword == nullptr || portablePasswordConfirm == nullptr
        || !portablePassword->setProperty("text", QStringLiteral("keyboard-vault-password"))
        || !portablePasswordConfirm->setProperty("text", QStringLiteral("keyboard-vault-password")))
    {
        return false;
    }
    constexpr std::array securityOrder{
        "settingsCredentialStorage",     "settingsCredentialRemoveSource",
        "settingsPortableVaultPassword", "settingsPortableVaultPasswordConfirm",
        "settingsPortableVaultAction",   "settingsCredentialCleanupStorage",
        "settingsRemoveAllCredentials",
    };
    if (!verifyOrder(securityOrder))
    {
        return false;
    }

    QQuickItem *terminalCategory = quickItem(rootObject, "settingsTerminalCategory");
    if (!focusItem(window, terminalCategory, QStringLiteral("settingsTerminalCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("terminal"))
    {
        qCWarning(applicationLog) << "Terminal settings category did not activate from the keyboard";
        return false;
    }
    std::vector<const char *> terminalOrder{
        "settingsFontFamily",   "settingsShowAllTerminalFonts",
        "settingsFontSize",     "settingsTerminalOpacity",
        "settingsCursor",       "settingsCursorBlink",
        "settingsCopyOnSelect", "settingsMultilinePaste",
        "settingsReset",        "settingsDiscard",
        "settingsApply",
    };
    QQuickItem *terminalLigatures = quickItem(rootObject, "settingsTerminalLigatures");
    if (terminalLigatures != nullptr && terminalLigatures->isEnabled())
    {
        terminalOrder.insert(terminalOrder.begin() + 3, "settingsTerminalLigatures");
    }
    if (!verifyOrder(terminalOrder))
    {
        return false;
    }

    QQuickItem *shortcutsCategory = quickItem(rootObject, "settingsShortcutsCategory");
    if (!focusItem(window, shortcutsCategory, QStringLiteral("settingsShortcutsCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("shortcuts"))
    {
        qCWarning(applicationLog) << "Shortcuts settings category did not activate from the keyboard";
        return false;
    }

    QQuickItem *shortcutRecorder = visualQuickItem(rootObject, "shortcutRecorder_application.commandPalette");
    if (!focusItem(window, shortcutRecorder, QStringLiteral("shortcutRecorder_application.commandPalette")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    const bool recordingStarted = settingsPane->property("shortcutRecording").toBool();
    sendKey(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    const bool conflictRejected = settingsPane->property("shortcutRecording").toBool();
    sendKey(window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
    const auto commandPaletteShortcut = [&controller]() {
        const QVariantList actions = controller.actions();
        for (const QVariant &entry : actions)
        {
            const QVariantMap action = entry.toMap();
            if (action.value(QStringLiteral("id")).toString() == QStringLiteral("application.commandPalette"))
            {
                return action.value(QStringLiteral("shortcut")).toString();
            }
        }
        return QString{};
    };
    const bool shortcutRecorded = !settingsPane->property("shortcutRecording").toBool()
                                  && commandPaletteShortcut() == QStringLiteral("Ctrl+Alt+P");
    const bool shortcutReset = controller.resetActionShortcut(QStringLiteral("application.commandPalette"))
                               && commandPaletteShortcut() == QStringLiteral("Ctrl+Shift+P");
    if (!recordingStarted || !conflictRejected || !shortcutRecorded || !shortcutReset)
    {
        qCWarning(applicationLog) << "Shortcut recorder keyboard route failed"
                                  << "recordingStarted=" << recordingStarted << "conflictRejected=" << conflictRejected
                                  << "shortcutRecorded=" << shortcutRecorded << "shortcutReset=" << shortcutReset
                                  << "effectiveShortcut=" << commandPaletteShortcut();
        return false;
    }
    if (!focusItem(window, terminalCategory, QStringLiteral("settingsTerminalCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (settingsPane->property("currentCategory").toString() != QStringLiteral("terminal"))
    {
        qCWarning(applicationLog) << "Terminal settings category was not restored after shortcut recording";
        return false;
    }
    return true;
}

[[nodiscard]] bool verifyHostEditorTabOrder(ztermy::NativeWindow &window, QQuickItem *rootObject)
{
    constexpr std::array<const char *, 12> passwordOrder{
        "hostName",
        "hostGroup",
        "hostAddress",
        "hostPort",
        "hostUsername",
        "hostAuthentication",
        "hostCredential",
        "hostCredentialReveal",
        "hostRememberCredential",
        "hostCancel",
        "hostSave",
        "hostConnect",
    };
    const auto &order = passwordOrder;

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

[[nodiscard]] bool verifyQuickConnectTabOrder(ztermy::NativeWindow &window, QQuickItem *rootObject)
{
    constexpr std::array order{
        "quickAuthentication", "quickCredential", "quickSaveProfile", "quickConnectCancel", "quickConnectConfirm",
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
            qCWarning(applicationLog) << "Quick connect Tab order mismatch"
                                      << "index=" << index << "expected=" << expectedName << "actual=" << actualName;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool runUiKeyboardRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                             const QString &outputDirectory)
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
        std::pair{"hostsTitleAction", "Hosts"},
        std::pair{"titleNewTabAction", "Open new terminal menu"},
        std::pair{"minimizeCaptionButton", "Minimize"},
        std::pair{"maximizeCaptionButton", "Maximize"},
        std::pair{"closeCaptionButton", "Close"},
        std::pair{"sideHostsAction", "Hosts"},
        std::pair{"commandPaletteAction", "Open command palette"},
        std::pair{"settingsShortcutAction", "Open Settings"},
        std::pair{"hostLocalTerminal", "Open local terminal"},
        std::pair{"terminalFindAction", "Find in terminal"},
    };
    for (const auto &[objectName, expectedName] : accessibleButtons)
    {
        if (!verifyAccessibleButton(rootObject, objectName, expectedName))
        {
            return false;
        }
    }

    sendKey(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    QQuickItem *commandPalette = quickItem(rootObject, "commandPalette");
    QQuickItem *commandPaletteSearch = quickItem(rootObject, "commandPaletteSearch");
    QQuickItem *commandPaletteList = quickItem(rootObject, "commandPaletteList");
    const bool paletteOpened = processWindowEventsUntil(
        [&window] {
            return namedFocusItem(window) == QStringLiteral("commandPaletteSearch");
        },
        std::chrono::seconds{1});
    QAccessibleInterface *paletteInterface =
        commandPalette == nullptr ? nullptr : QAccessible::queryAccessibleInterface(commandPalette);
    QAccessibleInterface *paletteListInterface =
        commandPaletteList == nullptr ? nullptr : QAccessible::queryAccessibleInterface(commandPaletteList);
    const bool paletteAccessible =
        paletteInterface != nullptr && paletteInterface->role() == QAccessible::Dialog
        && paletteInterface->text(QAccessible::Name) == QStringLiteral("Command palette")
        && paletteListInterface != nullptr && paletteListInterface->role() == QAccessible::List
        && paletteListInterface->text(QAccessible::Name) == QStringLiteral("Available commands");
    const bool paletteCaptured = captureLayout(window, outputDirectory, QStringLiteral("command-palette"));
    sendText(window, u"hosts");
    const bool paletteFiltered = commandPaletteList != nullptr && commandPaletteList->property("count").toInt() > 0;
    sendKey(window, Qt::Key_Return);
    const bool paletteExecuted = commandPalette != nullptr && !commandPalette->isVisible()
                                 && rootObject->property("currentPage").toString() == QStringLiteral("hosts");
    if (!paletteOpened || !paletteAccessible || !paletteCaptured || !paletteFiltered || !paletteExecuted
        || commandPaletteSearch == nullptr)
    {
        qCWarning(applicationLog) << "Command palette keyboard route failed"
                                  << "opened=" << paletteOpened << "captured=" << paletteCaptured
                                  << "accessible=" << paletteAccessible << "filtered=" << paletteFiltered
                                  << "executed=" << paletteExecuted << "focus=" << namedFocusItem(window);
        return false;
    }

    rootObject->setProperty("currentPage", QStringLiteral("hosts"));
    processWindowEventsFor(std::chrono::milliseconds{100});
    QQuickItem *settingsAction = quickItem(rootObject, "settingsShortcutAction");
    if (!focusItem(window, settingsAction, QStringLiteral("settingsShortcutAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    if (rootObject->property("currentPage").toString() != QStringLiteral("settings")
        || !rootObject->property("settingsTabOpen").toBool()
        || !verifyAccessibleButton(rootObject, "settingsTitleAction", "Activate Settings")
        || !verifyAccessibleButton(rootObject, "settingsTitleCloseAction", "Close Settings")
        || !verifyAccessibleButton(rootObject, "settingsApplicationCategory", "Application settings")
        || !verifyAccessibleButton(rootObject, "settingsAppearanceCategory", "Appearance settings")
        || !verifyAccessibleButton(rootObject, "settingsTerminalCategory", "Terminal settings")
        || !verifyAccessibleButton(rootObject, "settingsShortcutsCategory", "Shortcuts settings")
        || !verifyAccessibleButton(rootObject, "settingsSftpCategory", "SFTP settings")
        || !verifyAccessibleButton(rootObject, "settingsAboutCategory", "About settings"))
    {
        qCWarning(applicationLog) << "Space did not open the singleton Settings work tab";
        return false;
    }

    auto *settingsPane = rootObject->findChild<QObject *>(QStringLiteral("settingsPane"));
    if (settingsPane == nullptr
        || settingsPane->property("currentCategory").toString() != QStringLiteral("application"))
    {
        qCWarning(applicationLog) << "Settings did not open on the Application category";
        return false;
    }
    QQuickItem *appearanceCategory = quickItem(rootObject, "settingsAppearanceCategory");
    if (!focusItem(window, appearanceCategory, QStringLiteral("settingsAppearanceCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);

    if (!verifyFontPickerKeyboard(window, rootObject, "settingsUiFont", "settingsUiFontSearch"))
    {
        return false;
    }
    if (!verifySettingsTabOrder(window, controller, rootObject))
    {
        return false;
    }
    if (!verifyFontPickerKeyboard(window, rootObject, "settingsFontFamily", "settingsTerminalFontSearch"))
    {
        return false;
    }
    window.resize(QSize{500, 360});
    processWindowEventsFor(std::chrono::milliseconds{150});
    if (!verifySettingsTabOrder(window, controller, rootObject))
    {
        return false;
    }
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{150});

    QQuickItem *theme = quickItem(rootObject, "settingsTheme");
    QQuickItem *accent = quickItem(rootObject, "settingsAccent");
    QQuickItem *opacity = quickItem(rootObject, "settingsOpacity");
    QQuickItem *fontSize = quickItem(rootObject, "settingsFontSize");
    QQuickItem *cursorBlink = quickItem(rootObject, "settingsCursorBlink");
    QQuickItem *copyOnSelect = quickItem(rootObject, "settingsCopyOnSelect");
    QQuickItem *multilinePaste = quickItem(rootObject, "settingsMultilinePaste");
    QQuickItem *apply = quickItem(rootObject, "settingsApply");
    if (theme == nullptr || accent == nullptr || opacity == nullptr || fontSize == nullptr || cursorBlink == nullptr
        || copyOnSelect == nullptr || multilinePaste == nullptr || apply == nullptr)
    {
        qCWarning(applicationLog) << "Settings keyboard smoke object lookup failed";
        return false;
    }

    if (!focusItem(window, appearanceCategory, QStringLiteral("settingsAppearanceCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);

    if (!focusItem(window, theme, QStringLiteral("settingsTheme")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Down, Qt::AltModifier);
    processWindowEventsFor(std::chrono::milliseconds{220});
    auto *themePopup = theme->property("popup").value<QObject *>();
    const bool popupOpened = themePopup != nullptr && themePopup->property("opened").toBool();
    sendKey(window, Qt::Key_Escape);
    const bool popupClosed = themePopup != nullptr && !themePopup->property("opened").toBool();
    processWindowEventsFor(std::chrono::milliseconds{160});
    sendKey(window, Qt::Key_Down);

    if (!focusItem(window, accent, QStringLiteral("settingsAccent")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Down);

    if (!opacity->isVisible() || !opacity->isEnabled())
    {
        qCWarning(applicationLog) << "Window opacity control unexpectedly unavailable"
                                  << "category="
                                  << (settingsPane == nullptr ? QStringLiteral("<missing>")
                                                              : settingsPane->property("currentCategory").toString())
                                  << "backdropIndex="
                                  << (quickItem(rootObject, "settingsBackdrop") == nullptr
                                          ? -1
                                          : quickItem(rootObject, "settingsBackdrop")->property("currentIndex").toInt())
                                  << "visible=" << opacity->isVisible() << "enabled=" << opacity->isEnabled();
        return false;
    }
    if (!focusItem(window, opacity, QStringLiteral("settingsOpacity")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Left);

    QQuickItem *terminalCategory = quickItem(rootObject, "settingsTerminalCategory");
    if (!focusItem(window, terminalCategory, QStringLiteral("settingsTerminalCategory")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);

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

    const bool livePreviewMatches =
        rootObject->property("appearancePreviewActive").toBool()
        && rootObject->property("previewThemePreference").toString() == QStringLiteral("light")
        && rootObject->property("previewBackdropPreference").toString() == QStringLiteral("acrylic")
        && rootObject->property("previewAccentPreference").toString() == QStringLiteral("system")
        && qAbs(rootObject->property("previewBackdropOpacity").toReal() - 0.95) < 0.001;
    const bool draftMatches = popupOpened && popupClosed && theme->property("currentIndex").toInt() == 2
                              && accent->property("currentIndex").toInt() == 1
                              && qAbs(opacity->property("value").toReal() - 0.95) < 0.001
                              && fontSize->property("value").toInt() == 15 && switchesChanged && livePreviewMatches;
    if (!draftMatches)
    {
        qCWarning(applicationLog) << "Settings keyboard edits did not produce the expected draft"
                                  << "popupOpened=" << popupOpened << "popupClosed=" << popupClosed
                                  << "themeIndex=" << theme->property("currentIndex").toInt()
                                  << "accentIndex=" << accent->property("currentIndex").toInt()
                                  << "opacity=" << opacity->property("value").toReal()
                                  << "fontSize=" << fontSize->property("value").toInt()
                                  << "switchesChanged=" << switchesChanged
                                  << "livePreviewMatches=" << livePreviewMatches;
        return false;
    }

    if (!focusItem(window, apply, QStringLiteral("settingsApply")))
    {
        return false;
    }

    sendKey(window, Qt::Key_Return);
    QQuickItem *settingsStatusMessage = quickItem(rootObject, "settingsStatusMessage");
    const bool settingsApplied =
        controller.themePreference() == QStringLiteral("light") && qAbs(controller.backdropOpacity() - 0.95) < 0.001
        && controller.accentPreference() == QStringLiteral("system") && controller.terminalFontSize() == 15
        && !controller.cursorBlink() && controller.copyOnSelect() && !controller.confirmMultilinePaste()
        && settingsStatusMessage != nullptr && settingsStatusMessage->isVisible();
    if (!settingsApplied)
    {
        qCWarning(applicationLog) << "Enter did not apply the keyboard-edited settings";
        return false;
    }
    const bool settingsStatusDismissed = processWindowEventsUntil(
        [settingsStatusMessage] {
            return settingsStatusMessage != nullptr && !settingsStatusMessage->isVisible();
        },
        std::chrono::seconds{5});
    if (!settingsStatusDismissed)
    {
        qCWarning(applicationLog) << "Settings success feedback did not dismiss automatically";
        return false;
    }

    QQuickItem *settingsCloseAction = quickItem(rootObject, "settingsTitleCloseAction");
    if (!focusItem(window, settingsCloseAction, QStringLiteral("settingsTitleCloseAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Space);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (rootObject->property("appearancePreviewActive").toBool() || rootObject->property("settingsTabOpen").toBool()
        || rootObject->property("currentPage").toString() != QStringLiteral("hosts"))
    {
        qCWarning(applicationLog) << "Closing Settings did not restore the previous workspace"
                                  << "page=" << rootObject->property("currentPage").toString()
                                  << "tabOpen=" << rootObject->property("settingsTabOpen").toBool();
        return false;
    }
    QQuickItem *newHost = quickItem(rootObject, "hostNew");
    QQuickItem *quickConnectTarget = quickItem(rootObject, "quickConnectTarget");
    QQuickItem *quickConnectAction = quickItem(rootObject, "quickConnectAction");
    QQuickItem *localTerminalAction = quickItem(rootObject, "hostLocalTerminal");
    if (!verifyAccessibleButton(rootObject, "hostNew", "Create a new SSH host profile")
        || !verifyAccessibleButton(rootObject, "quickConnectAction", "Configure quick SSH connection")
        || quickConnectTarget == nullptr || localTerminalAction == nullptr
        || !focusItem(window, quickConnectTarget, QStringLiteral("quickConnectTarget")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Tab);
    if (namedFocusItem(window) != QStringLiteral("quickConnectAction"))
    {
        qCWarning(applicationLog) << "Host page Tab order did not reach Quick connect action"
                                  << "actual=" << namedFocusItem(window);
        return false;
    }
    sendKey(window, Qt::Key_Tab);
    if (namedFocusItem(window) != QStringLiteral("hostLocalTerminal"))
    {
        qCWarning(applicationLog) << "Host page Tab order did not reach local terminal after Quick connect"
                                  << "actual=" << namedFocusItem(window);
        return false;
    }
    sendKey(window, Qt::Key_Tab);
    if (namedFocusItem(window) != QStringLiteral("hostNew"))
    {
        qCWarning(applicationLog) << "Host page Tab order did not reach New host after local terminal"
                                  << "actual=" << namedFocusItem(window);
        return false;
    }

    if (quickConnectTarget == nullptr || quickConnectAction == nullptr
        || !quickConnectTarget->setProperty("text", QStringLiteral("tester@example.invalid:2222"))
        || !focusItem(window, quickConnectAction, QStringLiteral("quickConnectAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    processWindowEventsFor(std::chrono::milliseconds{100});
    auto *quickConnectDialog = rootObject->findChild<QObject *>(QStringLiteral("quickConnectDialog"));
    QQuickItem *quickCredential = quickItem(rootObject, "quickCredential");
    const bool quickConnectDialogOpened =
        quickConnectDialog != nullptr && quickConnectDialog->property("visible").toBool() && quickCredential != nullptr
        && quickCredential->setProperty("text", QStringLiteral("keyboard-smoke-secret"))
        && namedFocusItem(window) == QStringLiteral("quickAuthentication")
        && verifyAccessibleButton(rootObject, "quickConnectCancel", "Cancel quick SSH connection")
        && verifyAccessibleButton(rootObject, "quickConnectConfirm", "Start quick SSH connection");
    if (!quickConnectDialogOpened || !verifyQuickConnectTabOrder(window, rootObject))
    {
        qCWarning(applicationLog) << "Quick connect dialog keyboard route failed"
                                  << "opened=" << quickConnectDialogOpened << "focus=" << namedFocusItem(window);
        return false;
    }
    window.resize(QSize{500, 360});
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool compactQuickConnectFits = quickConnectDialog->property("width").toReal() <= 452.0
                                         && quickConnectDialog->property("height").toReal() <= 312.0
                                         && verifyQuickConnectTabOrder(window, rootObject);
    window.resize(QSize{1120, 800});
    processWindowEventsFor(std::chrono::milliseconds{150});
    sendKey(window, Qt::Key_Escape);
    processWindowEventsFor(std::chrono::milliseconds{180});
    if (!compactQuickConnectFits || namedFocusItem(window) != QStringLiteral("quickConnectAction"))
    {
        qCWarning(applicationLog) << "Quick connect compact layout or focus restoration failed"
                                  << "compactFits=" << compactQuickConnectFits
                                  << "dialogWidth=" << quickConnectDialog->property("width").toReal()
                                  << "dialogHeight=" << quickConnectDialog->property("height").toReal()
                                  << "focus=" << namedFocusItem(window);
        return false;
    }
    quickConnectTarget->setProperty("text", QString{});

    if (!focusItem(window, newHost, QStringLiteral("hostNew")))
    {
        return false;
    }

    sendKey(window, Qt::Key_Return);
    QQuickItem *hostPane = quickItem(rootObject, "hostConnectionPane");
    const bool editorOpened = hostPane != nullptr && hostPane->property("editorExpanded").toBool()
                              && namedFocusItem(window) == QStringLiteral("hostName");
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool editorCaptured = captureLayout(window, outputDirectory, QStringLiteral("new-host-editor"));
    QQuickItem *hostMasterScroll = quickItem(rootObject, "hostMasterScroll");
    QQuickItem *hostDetailPane = quickItem(rootObject, "hostDetailPane");
    QQuickItem *hostDetailDismissRegion = quickItem(rootObject, "hostDetailDismissRegion");
    const bool editorUsesSplitLayout =
        hostPane != nullptr && hostMasterScroll != nullptr && hostDetailPane != nullptr
        && hostDetailDismissRegion != nullptr && hostDetailPane->isVisible() && hostMasterScroll->width() > 300.0
        && hostDetailPane->width() >= 400.0
        && qAbs(hostMasterScroll->width() + hostDetailPane->width() - hostPane->width()) <= 1.0
        && qAbs(hostDetailDismissRegion->width() - hostMasterScroll->width()) <= 1.0;
    if (!editorOpened || !editorCaptured || !editorUsesSplitLayout || !verifyHostEditorTabOrder(window, rootObject))
    {
        qCWarning(applicationLog) << "Enter did not open a keyboard-reachable host editor"
                                  << "editorOpened=" << editorOpened << "editorCaptured=" << editorCaptured
                                  << "splitLayout=" << editorUsesSplitLayout << "focus=" << namedFocusItem(window);
        return false;
    }
    QQuickItem *hostAddress = quickItem(rootObject, "hostAddress");
    QQuickItem *hostName = quickItem(rootObject, "hostName");
    QQuickItem *hostGroup = quickItem(rootObject, "hostGroup");
    const bool groupSelectionInvoked =
        hostGroup != nullptr && hostGroup->property("count").toInt() > 0
        && QMetaObject::invokeMethod(hostGroup, "activated", Qt::DirectConnection, Q_ARG(int, 0));
    processWindowEventsFor(std::chrono::milliseconds{50});
    const bool groupSelectionApplied =
        groupSelectionInvoked && hostGroup->property("text").toString() == QStringLiteral("Test fixtures");
    if (!groupSelectionApplied)
    {
        qCWarning(applicationLog) << "Selecting a profile group did not populate the editable field"
                                  << "invoked=" << groupSelectionInvoked << "text="
                                  << (hostGroup == nullptr ? QStringLiteral("<missing>")
                                                           : hostGroup->property("text").toString());
        return false;
    }
    if (!controller.deleteHostProfile(QStringLiteral("ui-layout-smoke-profile")))
    {
        qCWarning(applicationLog) << "Could not remove the profile-group selection fixture";
        return false;
    }
    constexpr auto generatedName = u"selection.example.test";
    if (!focusItem(window, hostAddress, QStringLiteral("hostAddress")))
    {
        return false;
    }
    sendText(window, generatedName);
    const bool profileNameAutoFilled =
        hostName != nullptr && hostName->property("text").toString() == QStringView{generatedName};
    if (hostName != nullptr)
    {
        sendMouseClick(window, *hostName, QPointF{hostName->width() - 12.0, hostName->height() / 2.0});
    }
    const bool profileNameSelectionStable =
        hostName != nullptr && namedFocusItem(window) == QStringLiteral("hostName")
        && hostName->property("selectedText").toString() == QStringView{generatedName}
        && hostName->property("selectionStart").toInt() == 0
        && hostName->property("selectionEnd").toInt() == QStringView{generatedName}.size();
    if (!profileNameAutoFilled || !profileNameSelectionStable)
    {
        qCWarning(applicationLog) << "Auto-generated profile name did not retain full selection after pointer release"
                                  << "autoFilled=" << profileNameAutoFilled
                                  << "stableSelection=" << profileNameSelectionStable << "selectedText="
                                  << (hostName == nullptr ? QStringLiteral("<missing>")
                                                          : hostName->property("selectedText").toString());
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
    QQuickItem *hostAuthentication = quickItem(rootObject, "hostAuthentication");
    QQuickItem *passphraseRequired = quickItem(rootObject, "hostPassphraseRequired");
    QQuickItem *credential = quickItem(rootObject, "hostCredential");
    if (hostAuthentication == nullptr || !hostAuthentication->setProperty("currentIndex", 0))
    {
        return false;
    }
    processWindowEventsFor(std::chrono::milliseconds{100});
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

    sendMouseClick(window, *hostDetailDismissRegion,
                   QPointF{hostDetailDismissRegion->width() / 2.0, hostDetailDismissRegion->height() / 2.0});
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool editorDismissed = !hostPane->property("editorExpanded").toBool() && hostDetailPane->width() <= 0.5
                                 && hostMasterScroll->width() >= hostPane->width() - 9.0;
    if (!editorDismissed)
    {
        qCWarning(applicationLog) << "Host detail pane did not release the master layout"
                                  << "detailWidth=" << hostDetailPane->width()
                                  << "masterWidth=" << hostMasterScroll->width() << "paneWidth=" << hostPane->width();
        return false;
    }

    const bool savedCredentialProfileCreated = controller.saveHostProfile(
        QString{}, QStringLiteral("Keyboard smoke host"), QStringLiteral("example.invalid"), 22,
        QStringLiteral("tester"), QStringLiteral("password"), QString{}, false, QStringLiteral("Tests"));
    processWindowEventsFor(std::chrono::milliseconds{500});
    const QVariantList savedCredentialProfiles = controller.hostProfiles();
    QQuickItem *savedHostCard = visualQuickItem(rootObject, "savedHostCard");
    const QString savedHostAccessibleName =
        savedHostCard == nullptr ? QString{} : savedHostCard->property("accessibleName").toString();
    const bool savedHostFocused =
        savedHostCard != nullptr && focusItem(window, savedHostCard, QStringLiteral("savedHostCard"));
    if (!savedCredentialProfileCreated || savedCredentialProfiles.size() != 1
        || savedHostAccessibleName != QStringLiteral("Connect to Keyboard smoke host") || !savedHostFocused)
    {
        qCWarning(applicationLog) << "Saved credential dialog smoke setup failed"
                                  << "created=" << savedCredentialProfileCreated
                                  << "profileCount=" << savedCredentialProfiles.size()
                                  << "cardFound=" << (savedHostCard != nullptr)
                                  << "accessibleName=" << savedHostAccessibleName << "focused=" << savedHostFocused
                                  << "filteredProfileCount="
                                  << (hostPane == nullptr ? -1 : hostPane->property("filteredProfileCount").toInt())
                                  << "commandText=" << quickConnectTarget->property("text").toString();
        return false;
    }

    QQuickItem *savedHostMoreAction = visualQuickItem(rootObject, "savedHostMoreAction");
    if (savedHostCard == nullptr || savedHostMoreAction == nullptr
        || !focusItem(window, savedHostMoreAction, QStringLiteral("savedHostMoreAction")))
    {
        qCWarning(applicationLog) << "Saved host menu smoke setup failed"
                                  << "cardFound=" << (savedHostCard != nullptr)
                                  << "actionFound=" << (savedHostMoreAction != nullptr) << "actionVisible="
                                  << (savedHostMoreAction != nullptr && savedHostMoreAction->isVisible())
                                  << "focus=" << namedFocusItem(window);
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool savedHostMenuOpened = processWindowEventsUntil(
        [savedHostMoreAction] {
            return savedHostMoreAction->property("menuVisible").toBool();
        },
        std::chrono::seconds{1});
    sendKey(window, Qt::Key_Escape);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (!savedHostMenuOpened || !focusItem(window, savedHostCard, QStringLiteral("savedHostCard")))
    {
        qCWarning(applicationLog) << "Saved host compact menu route failed" << "opened=" << savedHostMenuOpened;
        return false;
    }

    sendKey(window, Qt::Key_Return);
    processWindowEventsFor(std::chrono::milliseconds{100});
    const bool savedCredentialDialogOpened = namedFocusItem(window) == QStringLiteral("savedCredentialField");
    sendKey(window, Qt::Key_Escape);
    processWindowEventsFor(std::chrono::milliseconds{180});
    const bool savedCredentialDialogKeyboard =
        savedCredentialDialogOpened && namedFocusItem(window) == QStringLiteral("savedHostCard");
    const QString savedCredentialProfileId =
        savedCredentialProfiles.constFirst().toMap().value(QStringLiteral("id")).toString();
    const bool savedCredentialProfileDeleted = controller.deleteHostProfile(savedCredentialProfileId);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (!savedCredentialDialogKeyboard || !savedCredentialProfileDeleted)
    {
        qCWarning(applicationLog) << "Saved credential dialog keyboard route failed"
                                  << "opened=" << savedCredentialDialogOpened
                                  << "restoredFocus=" << namedFocusItem(window)
                                  << "profileDeleted=" << savedCredentialProfileDeleted;
        return false;
    }

    const qsizetype initialTabCount = controller.terminalTabs().size();
    QQuickItem *newTabAction = quickItem(rootObject, "titleNewTabAction");
    if (!focusItem(window, newTabAction, QStringLiteral("titleNewTabAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    QQuickItem *newLocalTerminalMenuAction = nullptr;
    const bool newTerminalMenuOpened = processWindowEventsUntil(
        [&] {
            newLocalTerminalMenuAction = quickItem(rootObject, "newLocalTerminalMenuAction");
            return newLocalTerminalMenuAction != nullptr && newLocalTerminalMenuAction->isVisible();
        },
        std::chrono::seconds{1});
    if (!newTerminalMenuOpened
        || !focusItem(window, newLocalTerminalMenuAction, QStringLiteral("newLocalTerminalMenuAction")))
    {
        qCWarning(applicationLog) << "New terminal menu did not open from the title-bar add button";
        return false;
    }
    sendKey(window, Qt::Key_Return);
    processWindowEventsFor(std::chrono::milliseconds{250});
    const bool oneTabCreated = controller.terminalTabs().size() == initialTabCount + 1
                               && rootObject->property("currentPage").toString() == QStringLiteral("terminal")
                               && terminalViewportHasFocus(window);
    if (!oneTabCreated)
    {
        qCWarning(applicationLog) << "Enter did not create exactly one focused local terminal"
                                  << "initialTabs=" << initialTabCount
                                  << "finalTabs=" << controller.terminalTabs().size()
                                  << "page=" << rootObject->property("currentPage").toString()
                                  << "focus=" << namedFocusItem(window);
        return false;
    }

    sendKey(window, Qt::Key_F, Qt::ControlModifier);
    const bool controlFindPreservedForTerminal =
        !rootObject->property("terminalSearchVisible").toBool() && terminalViewportHasFocus(window);
    const QVariantMap unbindFind = controller.setActionShortcut(QStringLiteral("terminal.find"), QString{});
    sendKey(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    const bool unboundFindPreservedForTerminal = unbindFind.value(QStringLiteral("valid")).toBool()
                                                 && !rootObject->property("terminalSearchVisible").toBool()
                                                 && terminalViewportHasFocus(window);
    const bool findShortcutReset = controller.resetActionShortcut(QStringLiteral("terminal.find"));
    sendKey(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    const bool terminalSearchOpened = findShortcutReset && rootObject->property("terminalSearchVisible").toBool()
                                      && namedFocusItem(window) == QStringLiteral("terminalSearchQuery");
    sendKey(window, Qt::Key_Escape);
    const bool terminalSearchKeyboard =
        controlFindPreservedForTerminal && unboundFindPreservedForTerminal && terminalSearchOpened
        && !rootObject->property("terminalSearchVisible").toBool() && terminalViewportHasFocus(window);
    if (!terminalSearchKeyboard)
    {
        qCWarning(applicationLog) << "Terminal search shortcut routing failed"
                                  << "ctrlFPreserved=" << controlFindPreservedForTerminal
                                  << "unboundCtrlShiftFPreserved=" << unboundFindPreservedForTerminal
                                  << "ctrlShiftFOpened=" << terminalSearchOpened << "focus=" << namedFocusItem(window);
        return false;
    }

    QQuickItem *terminalFindAction = quickItem(rootObject, "terminalFindAction");
    if (!focusItem(window, terminalFindAction, QStringLiteral("terminalFindAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool terminalFindActionOpened = rootObject->property("terminalSearchVisible").toBool()
                                          && namedFocusItem(window) == QStringLiteral("terminalSearchQuery");
    const bool terminalFindCaptured =
        terminalFindActionOpened && captureLayout(window, outputDirectory, QStringLiteral("terminal-find"));
    if (!focusItem(window, terminalFindAction, QStringLiteral("terminalFindAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool terminalFindActionClosed =
        !rootObject->property("terminalSearchVisible").toBool() && terminalViewportHasFocus(window);
    if (!terminalFindActionOpened || !terminalFindCaptured || !terminalFindActionClosed
        || !terminalViewportHasFocus(window))
    {
        qCWarning(applicationLog) << "Terminal Find action keyboard routing failed"
                                  << "opened=" << terminalFindActionOpened << "focus=" << namedFocusItem(window);
        return false;
    }

    auto *keywordHighlightPopover = rootObject->findChild<QObject *>(QStringLiteral("keywordHighlightPopover"));
    QQuickItem *keywordHighlightAction = quickItem(rootObject, "terminalKeywordHighlightAction");
    QQuickItem *keywordHighlightCloseAction = quickItem(rootObject, "keywordHighlightCloseAction");
    QQuickItem *keywordOutsideTarget = terminalViewportItem(rootObject);
    const auto keywordTerminalFixture = [](const int ruleCount) {
        QVariantList rules;
        rules.reserve(ruleCount);
        for (int index = 0; index < ruleCount; ++index)
        {
            const QString pattern = index == 0   ? QStringLiteral("docker")
                                    : index == 1 ? QStringLiteral("root")
                                                 : QStringLiteral("rule-%1").arg(index + 1);
            rules.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("rule-id-%1").arg(index + 1)},
                                     {QStringLiteral("pattern"), pattern},
                                     {QStringLiteral("foreground"), QStringLiteral("#FFFFFF")},
                                     {QStringLiteral("background"), QStringLiteral("#D13438")},
                                     {QStringLiteral("enabled"), true},
                                     {QStringLiteral("caseSensitive"), false}});
        }
        return QVariantMap{{QStringLiteral("kind"), QStringLiteral("ssh")},
                           {QStringLiteral("keywordHighlightEnabled"), true},
                           {QStringLiteral("keywordHighlightRules"), rules}};
    };
    const auto keywordPopoverVisible = [keywordHighlightPopover] {
        return keywordHighlightPopover != nullptr && keywordHighlightPopover->property("visible").toBool();
    };
    const auto clickKeywordHighlightAction = [&window, keywordHighlightAction] {
        if (keywordHighlightAction == nullptr)
        {
            return false;
        }
        sendMouseClick(window, *keywordHighlightAction,
                       QPointF{keywordHighlightAction->width() / 2.0, keywordHighlightAction->height() / 2.0});
        return true;
    };
    if (keywordHighlightPopover == nullptr || keywordHighlightAction == nullptr
        || !keywordHighlightPopover->setProperty("terminalTab", keywordTerminalFixture(2)))
    {
        qCWarning(applicationLog) << "Keyword-highlight runtime fixture could not be prepared";
        return false;
    }
    keywordHighlightAction->setVisible(true);
    keywordHighlightAction->setEnabled(true);
    processWindowEventsFor(std::chrono::milliseconds{100});
    const bool keywordPopoverOpened =
        clickKeywordHighlightAction() && processWindowEventsUntil(keywordPopoverVisible, std::chrono::seconds{1});
    processWindowEventsFor(std::chrono::milliseconds{100});
    QQuickItem *keywordRulesScrollView = quickItem(rootObject, "keywordRulesScrollView");
    QQuickItem *keywordRulesColumn = quickItem(rootObject, "keywordRulesColumn");
    const bool twoRuleViewportFits = keywordRulesScrollView != nullptr && keywordRulesColumn != nullptr
                                     && keywordRulesScrollView->height() >= keywordRulesColumn->height() + 3.5;
    const bool keywordPopoverCaptured =
        keywordPopoverOpened
        && captureLayout(window, outputDirectory, QStringLiteral("terminal-keyword-popover-two-rules"));
    const bool keywordToggledClosed = clickKeywordHighlightAction()
                                      && processWindowEventsUntil(
                                          [&keywordPopoverVisible] {
                                              return !keywordPopoverVisible();
                                          },
                                          std::chrono::seconds{1});

    const bool keywordReopenedForButton =
        clickKeywordHighlightAction() && processWindowEventsUntil(keywordPopoverVisible, std::chrono::seconds{1});
    const bool keywordCloseFocused =
        keywordReopenedForButton
        && focusItem(window, keywordHighlightCloseAction, QStringLiteral("keywordHighlightCloseAction"));
    if (keywordCloseFocused)
    {
        sendKey(window, Qt::Key_Return);
    }
    const bool keywordClosedByButton = keywordCloseFocused
                                       && processWindowEventsUntil(
                                           [&keywordPopoverVisible] {
                                               return !keywordPopoverVisible();
                                           },
                                           std::chrono::seconds{1});
    const bool keywordReopenedForOutside =
        clickKeywordHighlightAction() && processWindowEventsUntil(keywordPopoverVisible, std::chrono::seconds{1});
    if (keywordReopenedForOutside && keywordOutsideTarget != nullptr)
    {
        sendMouseClick(window, *keywordOutsideTarget, QPointF{20.0, keywordOutsideTarget->height() / 2.0});
    }
    const bool keywordClosedOutside = keywordReopenedForOutside && keywordOutsideTarget != nullptr
                                      && processWindowEventsUntil(
                                          [&keywordPopoverVisible] {
                                              return !keywordPopoverVisible();
                                          },
                                          std::chrono::seconds{1});
    const bool keywordReopenedForEscape =
        clickKeywordHighlightAction() && processWindowEventsUntil(keywordPopoverVisible, std::chrono::seconds{1});
    if (keywordReopenedForEscape)
    {
        sendKey(window, Qt::Key_Escape);
    }
    const bool keywordClosedByEscape = keywordReopenedForEscape
                                       && processWindowEventsUntil(
                                           [&keywordPopoverVisible] {
                                               return !keywordPopoverVisible();
                                           },
                                           std::chrono::seconds{1});
    const bool manyRuleFixtureApplied = keywordHighlightPopover->setProperty("terminalTab", keywordTerminalFixture(8));
    const bool keywordReopenedForScroll = manyRuleFixtureApplied && clickKeywordHighlightAction()
                                          && processWindowEventsUntil(keywordPopoverVisible, std::chrono::seconds{1});
    processWindowEventsFor(std::chrono::milliseconds{100});
    QQuickItem *keywordRulesScrollBar = quickItem(rootObject, "keywordRulesScrollBar");
    const qreal maximumRulesHeight = keywordHighlightPopover->property("maximumRulesHeight").toReal();
    const bool manyRulesScroll =
        keywordReopenedForScroll && keywordRulesScrollView != nullptr && keywordRulesColumn != nullptr
        && keywordRulesScrollBar != nullptr && keywordRulesScrollView->height() <= maximumRulesHeight + 0.5
        && keywordRulesColumn->height() > keywordRulesScrollView->property("availableHeight").toReal()
        && keywordRulesScrollBar->isVisible()
        && captureLayout(window, outputDirectory, QStringLiteral("terminal-keyword-popover-scroll"));
    if (keywordReopenedForScroll)
    {
        sendKey(window, Qt::Key_Escape);
    }
    if (!keywordPopoverCaptured || !twoRuleViewportFits || !keywordToggledClosed || !keywordClosedByButton
        || !keywordClosedOutside || !keywordClosedByEscape || !manyRulesScroll)
    {
        qCWarning(applicationLog) << "Keyword-highlight popover close contract failed"
                                  << "opened=" << keywordPopoverOpened << "captured=" << keywordPopoverCaptured
                                  << "toggle=" << keywordToggledClosed << "button=" << keywordClosedByButton
                                  << "outside=" << keywordClosedOutside << "escape=" << keywordClosedByEscape
                                  << "twoRuleFits=" << twoRuleViewportFits << "manyRuleScroll=" << manyRulesScroll;
        return false;
    }

    const auto activeTerminalState = [&controller]() {
        const QVariantList tabs = controller.terminalTabs();
        return tabs.isEmpty() ? QVariantMap{} : tabs.constFirst().toMap();
    };
    QQuickItem *terminalMoreAction = quickItem(rootObject, "terminalMoreAction");
    if (!focusItem(window, terminalMoreAction, QStringLiteral("terminalMoreAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    QQuickItem *terminalKeywordMenuAction = quickItem(rootObject, "terminalKeywordMenuAction");
    QQuickItem *terminalFollowDirectoryMenuAction = quickItem(rootObject, "terminalFollowDirectoryMenuAction");
    QQuickItem *terminalPauseRecordingMenuAction = quickItem(rootObject, "terminalPauseRecordingMenuAction");
    QQuickItem *terminalReviewRecordingMenuAction = quickItem(rootObject, "terminalReviewRecordingMenuAction");
    const bool terminalMoreMenuCompacted =
        terminalKeywordMenuAction != nullptr && !terminalKeywordMenuAction->isVisible()
        && qFuzzyIsNull(terminalKeywordMenuAction->height()) && terminalPauseRecordingMenuAction != nullptr
        && !terminalPauseRecordingMenuAction->isVisible() && qFuzzyIsNull(terminalPauseRecordingMenuAction->height())
        && terminalReviewRecordingMenuAction != nullptr && !terminalReviewRecordingMenuAction->isVisible()
        && qFuzzyIsNull(terminalReviewRecordingMenuAction->height()) && terminalFollowDirectoryMenuAction != nullptr
        && !terminalFollowDirectoryMenuAction->property("checkable").toBool()
        && captureLayout(window, outputDirectory, QStringLiteral("terminal-more-menu"));
    if (!terminalMoreMenuCompacted)
    {
        qCWarning(applicationLog)
            << "Terminal More menu retained hidden rows or an unwanted checkable indent"
            << "keywordHeight=" << (terminalKeywordMenuAction == nullptr ? -1.0 : terminalKeywordMenuAction->height())
            << "pauseHeight="
            << (terminalPauseRecordingMenuAction == nullptr ? -1.0 : terminalPauseRecordingMenuAction->height())
            << "reviewHeight="
            << (terminalReviewRecordingMenuAction == nullptr ? -1.0 : terminalReviewRecordingMenuAction->height())
            << "followCheckable="
            << (terminalFollowDirectoryMenuAction == nullptr
                    ? true
                    : terminalFollowDirectoryMenuAction->property("checkable").toBool());
        return false;
    }
    QQuickItem *terminalHistoryMenuAction = quickItem(rootObject, "terminalHistoryMenuAction");
    if (!focusItem(window, terminalHistoryMenuAction, QStringLiteral("terminalHistoryMenuAction")))
    {
        return false;
    }
    sendKey(window, Qt::Key_Return);
    QQuickItem *terminalWorkbench = quickItem(rootObject, "terminalWorkbench");
    const bool historyWorkbenchOpened = processWindowEventsUntil(
        [&] {
            const QVariantMap state = activeTerminalState();
            return terminalWorkbench != nullptr && terminalWorkbench->isVisible()
                   && state.value(QStringLiteral("workbenchOpen")).toBool()
                   && state.value(QStringLiteral("workbenchPage")).toString() == QStringLiteral("history")
                   && state.value(QStringLiteral("workbenchSide")).toString() == QStringLiteral("left");
        },
        std::chrono::seconds{1});
    const bool historyWorkbenchCaptured =
        historyWorkbenchOpened && captureLayout(window, outputDirectory, QStringLiteral("terminal-history-workbench"));
    QQuickItem *moveTerminalWorkbenchButton = quickItem(rootObject, "moveTerminalWorkbenchButton");
    if (!historyWorkbenchOpened || !historyWorkbenchCaptured
        || !focusItem(window, moveTerminalWorkbenchButton, QStringLiteral("moveTerminalWorkbenchButton")))
    {
        const QVariantMap state = activeTerminalState();
        qCWarning(applicationLog) << "Terminal history workbench did not open through its keyboard action"
                                  << "moreActionFound=" << (terminalMoreAction != nullptr)
                                  << "historyMenuActionFound=" << (terminalHistoryMenuAction != nullptr)
                                  << "workbenchFound=" << (terminalWorkbench != nullptr) << "workbenchVisible="
                                  << (terminalWorkbench != nullptr && terminalWorkbench->isVisible())
                                  << "workbenchOpen=" << state.value(QStringLiteral("workbenchOpen")).toBool()
                                  << "workbenchPage=" << state.value(QStringLiteral("workbenchPage")).toString()
                                  << "workbenchSide=" << state.value(QStringLiteral("workbenchSide")).toString()
                                  << "moveButtonFound=" << (moveTerminalWorkbenchButton != nullptr)
                                  << "focus=" << namedFocusItem(window);
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool workbenchMovedRight = processWindowEventsUntil(
        [&] {
            return activeTerminalState().value(QStringLiteral("workbenchSide")).toString() == QStringLiteral("right");
        },
        std::chrono::seconds{1});
    QQuickItem *terminalWorkbenchResizeHandle = quickItem(rootObject, "terminalWorkbenchResizeHandle");
    QQuickItem *closeTerminalWorkbenchButton = quickItem(rootObject, "closeTerminalWorkbenchButton");
    if (!workbenchMovedRight || terminalWorkbenchResizeHandle == nullptr || terminalWorkbenchResizeHandle->width() > 8.0
        || qAbs(terminalWorkbenchResizeHandle->x()) > 0.01
        || !focusItem(window, closeTerminalWorkbenchButton, QStringLiteral("closeTerminalWorkbenchButton")))
    {
        qCWarning(applicationLog)
            << "Terminal workbench did not move with a bounded right-side resize handle"
            << "handleFound=" << (terminalWorkbenchResizeHandle != nullptr) << "handleWidth="
            << (terminalWorkbenchResizeHandle == nullptr ? -1.0 : terminalWorkbenchResizeHandle->width())
            << "handleX=" << (terminalWorkbenchResizeHandle == nullptr ? -1.0 : terminalWorkbenchResizeHandle->x());
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool workbenchClosed = processWindowEventsUntil(
        [&] {
            return !activeTerminalState().value(QStringLiteral("workbenchOpen")).toBool()
                   && terminalViewportHasFocus(window);
        },
        std::chrono::seconds{1});

    QQuickItem *terminalScriptsAction = quickItem(rootObject, "terminalScriptsAction");
    if (!workbenchClosed || !focusItem(window, terminalScriptsAction, QStringLiteral("terminalScriptsAction")))
    {
        qCWarning(applicationLog) << "Terminal workbench close did not restore terminal focus";
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool scriptsWorkbenchOpened = processWindowEventsUntil(
        [&] {
            const QVariantMap state = activeTerminalState();
            return state.value(QStringLiteral("workbenchOpen")).toBool()
                   && state.value(QStringLiteral("workbenchPage")).toString() == QStringLiteral("scripts");
        },
        std::chrono::seconds{1});
    const bool snippetsWorkbenchCaptured =
        scriptsWorkbenchOpened && captureLayout(window, outputDirectory, QStringLiteral("terminal-snippets-workbench"));
    if (!scriptsWorkbenchOpened || !snippetsWorkbenchCaptured
        || !focusItem(window, closeTerminalWorkbenchButton, QStringLiteral("closeTerminalWorkbenchButton")))
    {
        qCWarning(applicationLog) << "Scripts workbench did not open through its keyboard action";
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool scriptsWorkbenchClosed = processWindowEventsUntil(
        [&] {
            return !activeTerminalState().value(QStringLiteral("workbenchOpen")).toBool()
                   && terminalViewportHasFocus(window);
        },
        std::chrono::seconds{1});

    QQuickItem *terminalComposerAction = quickItem(rootObject, "terminalComposerAction");
    if (!scriptsWorkbenchClosed || !focusItem(window, terminalComposerAction, QStringLiteral("terminalComposerAction")))
    {
        qCWarning(applicationLog) << "Scripts workbench close did not restore terminal focus";
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool composerOpened = processWindowEventsUntil(
        [&] {
            return activeTerminalState().value(QStringLiteral("composerOpen")).toBool()
                   && namedFocusItem(window) == QStringLiteral("terminalComposerInput");
        },
        std::chrono::seconds{1});
    const bool composerCaptured =
        composerOpened && captureLayout(window, outputDirectory, QStringLiteral("terminal-composer"));
    QQuickItem *terminalComposerInput = quickItem(rootObject, "terminalComposerInput");
    const bool composerLineBreak =
        composerOpened && terminalComposerInput != nullptr
        && terminalComposerInput->setProperty("text", QStringLiteral("Write-Output first line"));
    if (composerLineBreak)
    {
        sendKey(window, Qt::Key_Return, Qt::ShiftModifier);
    }
    const bool composerShiftEnter =
        composerLineBreak && terminalComposerInput->property("text").toString().contains(QLatin1Char('\n'));
    const bool composerCommandPrepared =
        composerShiftEnter
        && terminalComposerInput->setProperty("text", QStringLiteral("Write-Output ztermy-composer-smoke"));
    if (composerCommandPrepared)
    {
        sendKey(window, Qt::Key_Return);
    }
    const QVariantList composerHistory = controller.terminalHistory();
    const bool composerEnter = composerCommandPrepared && terminalComposerInput->property("text").toString().isEmpty()
                               && !composerHistory.isEmpty()
                               && composerHistory.constFirst().toMap().value(QStringLiteral("command")).toString()
                                      == QStringLiteral("Write-Output ztermy-composer-smoke");
    sendKey(window, Qt::Key_Escape);
    const bool composerKeyboard = composerOpened && composerCaptured && composerShiftEnter && composerEnter
                                  && processWindowEventsUntil(
                                      [&] {
                                          return !activeTerminalState().value(QStringLiteral("composerOpen")).toBool()
                                                 && terminalViewportHasFocus(window);
                                      },
                                      std::chrono::seconds{1});
    if (!composerKeyboard)
    {
        qCWarning(applicationLog) << "Terminal Composer keyboard focus route failed"
                                  << "opened=" << composerOpened << "shiftEnter=" << composerShiftEnter
                                  << "enter=" << composerEnter << "focus=" << namedFocusItem(window);
        return false;
    }

    while (controller.terminalTabs().size() < 8)
    {
        if (controller.startLocalTerminal().isEmpty())
        {
            qCWarning(applicationLog) << "Unable to create terminal tabs for overflow smoke";
            return false;
        }
    }
    processWindowEventsFor(std::chrono::milliseconds{350});
    QQuickItem *titleTerminalTabs = quickItem(rootObject, "titleTerminalTabs");
    const bool activeOverflowTabVisible =
        titleTerminalTabs != nullptr
        && titleTerminalTabs->property("currentIndex").toInt() == controller.terminalTabs().size() - 1
        && titleTerminalTabs->property("contentX").toReal() > 0.0;
    if (!activeOverflowTabVisible)
    {
        qCWarning(applicationLog)
            << "Active overflow tab was not scrolled into view"
            << "tabCount=" << controller.terminalTabs().size() << "currentIndex="
            << (titleTerminalTabs == nullptr ? -1 : titleTerminalTabs->property("currentIndex").toInt())
            << "contentX=" << (titleTerminalTabs == nullptr ? -1.0 : titleTerminalTabs->property("contentX").toReal());
        return false;
    }

    const QVariantList overflowTabs = controller.terminalTabs();
    for (qsizetype index = overflowTabs.size() - 1; index > 0; --index)
    {
        controller.closeTerminalTab(overflowTabs.at(index).toMap().value(QStringLiteral("id")).toString());
    }
    controller.activateTerminalTab(overflowTabs.first().toMap().value(QStringLiteral("id")).toString());
    processWindowEventsFor(std::chrono::milliseconds{250});

    QQuickItem *terminalViewport = terminalViewportItem(rootObject);
    const bool dialogOpened = terminalViewport != nullptr
                              && QMetaObject::invokeMethod(terminalViewport, "multilinePasteConfirmationRequested",
                                                           Qt::DirectConnection, Q_ARG(int, 2));
    processWindowEventsFor(std::chrono::milliseconds{100});
    const bool safeDialogFocus = dialogOpened && namedFocusItem(window) == QStringLiteral("multilinePasteReject");
    sendKey(window, Qt::Key_Right);
    const bool arrowMovedDialogFocus = namedFocusItem(window) == QStringLiteral("multilinePasteAccept");
    sendKey(window, Qt::Key_Escape);
    processWindowEventsFor(std::chrono::milliseconds{180});
    const bool dialogKeyboard = safeDialogFocus && arrowMovedDialogFocus && terminalViewportHasFocus(window);
    if (!dialogKeyboard)
    {
        qCWarning(applicationLog) << "Multiline-paste dialog keyboard route failed"
                                  << "opened=" << dialogOpened << "safeFocus=" << safeDialogFocus
                                  << "arrowMovedFocus=" << arrowMovedDialogFocus
                                  << "restoredFocus=" << namedFocusItem(window);
        return false;
    }

    QQuickItem *hostsAction = quickItem(rootObject, "hostsTitleAction");
    if (!focusItem(window, hostsAction, QStringLiteral("hostsTitleAction")))
    {
        return false;
    }
    QQuickItem *tabStrip = quickItem(rootObject, "titleTerminalTabs");
    QQuickItem *newTabContainer = quickItem(rootObject, "titleNewTabContainer");
    const bool singleTabLayout = tabStrip != nullptr && newTabContainer != nullptr && tabStrip->width() > 0.0
                                 && qAbs(newTabContainer->x() - (tabStrip->x() + tabStrip->width())) < 0.5;
    sendMouseMove(window, *hostsAction, QPointF{hostsAction->width() / 2.0, hostsAction->height() / 2.0});
    const bool titleHoverStable = hostsAction->property("hovered").toBool();
    QQuickItem *minimizeCaptionButton = quickItem(rootObject, "minimizeCaptionButton");
    bool lightCaptionHoverVisible = false;
    if (minimizeCaptionButton != nullptr)
    {
        sendMouseMove(window, *minimizeCaptionButton,
                      QPointF{minimizeCaptionButton->width() / 2.0, minimizeCaptionButton->height() / 2.0});
        const auto hoverColor = minimizeCaptionButton->property("surfaceColor").value<QColor>();
        const auto chromeColor = rootObject->property("chromeColor").value<QColor>();
        const int maximumChannelDelta =
            std::max({qAbs(hoverColor.red() - chromeColor.red()), qAbs(hoverColor.green() - chromeColor.green()),
                      qAbs(hoverColor.blue() - chromeColor.blue())});
        lightCaptionHoverVisible =
            minimizeCaptionButton->property("hovered").toBool() && hoverColor.alpha() > 0 && maximumChannelDelta >= 16;
    }
    sendKey(window, Qt::Key_Space);
    const bool navigationPreservedSession = rootObject->property("currentPage").toString() == QStringLiteral("hosts")
                                            && controller.terminalTabs().size() == initialTabCount + 1;
    rootObject->setProperty("currentPage", QStringLiteral("terminal"));
    while (!controller.terminalTabs().isEmpty())
    {
        controller.closeTerminalTab(
            controller.terminalTabs().constFirst().toMap().value(QStringLiteral("id")).toString());
        processWindowEventsFor(std::chrono::milliseconds{50});
    }
    processWindowEventsFor(std::chrono::milliseconds{250});
    QQuickItem *hostsContainer = hostsAction->parentItem();
    const bool emptyTabLayout = tabStrip != nullptr && qFuzzyIsNull(tabStrip->width()) && newTabContainer != nullptr
                                && hostsContainer != nullptr
                                && qAbs(newTabContainer->x() - (hostsContainer->x() + hostsContainer->width())) < 0.5;
    const bool lastTabReturnedToHosts = rootObject->property("currentPage").toString() == QStringLiteral("hosts")
                                        && namedFocusItem(window) == QStringLiteral("hostsTitleAction");
    qCInfo(applicationLog) << "UI keyboard route check"
                           << "settingsTabStops=" << 24 << "hostEditorTabStops=" << 22
                           << "popupKeyboard=" << (popupOpened && popupClosed) << "settingsApplied=" << settingsApplied
                           << "checkboxKeyboard=" << checkboxChanged << "oneTabCreated=" << oneTabCreated
                           << "terminalSearchKeyboard=" << terminalSearchKeyboard << "dialogKeyboard=" << dialogKeyboard
                           << "navigationPreservedSession=" << navigationPreservedSession
                           << "singleTabLayout=" << singleTabLayout << "emptyTabLayout=" << emptyTabLayout
                           << "titleHoverStable=" << titleHoverStable
                           << "lightCaptionHoverVisible=" << lightCaptionHoverVisible
                           << "lastTabReturnedToHosts=" << lastTabReturnedToHosts;
    return navigationPreservedSession && singleTabLayout && emptyTabLayout && titleHoverStable
           && lightCaptionHoverVisible && lastTabReturnedToHosts;
}

[[nodiscard]] bool runRealHostUiRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                             const QString &outputDirectory)
{
    const QString host = QString::fromUtf8(qgetenv("ZTERMY_TEST_SSH_HOST")).trimmed();
    const QString username = QString::fromUtf8(qgetenv("ZTERMY_TEST_SSH_USERNAME")).trimmed();
    const QString privateKeyPath = QString::fromUtf8(qgetenv("ZTERMY_TEST_SSH_PRIVATE_KEY")).trimmed();
    const QString expectedFingerprint = QString::fromUtf8(qgetenv("ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT")).trimmed();
    const QString deniedSftpPath = QString::fromUtf8(qgetenv("ZTERMY_TEST_SFTP_DENIED_PATH")).trimmed();
    bool portValid = false;
    const int configuredPort = QString::fromUtf8(qgetenv("ZTERMY_TEST_SSH_PORT")).toInt(&portValid);
    const int port = portValid ? configuredPort : 22;
    if (host.isEmpty() || username.isEmpty() || privateKeyPath.isEmpty() || expectedFingerprint.isEmpty()
        || !QFileInfo::exists(privateKeyPath) || port <= 0 || port > 65535)
    {
        qCWarning(applicationLog) << "Real-host UI smoke requires valid ZTERMY_TEST_SSH_HOST, ZTERMY_TEST_SSH_USERNAME,"
                                     " ZTERMY_TEST_SSH_PRIVATE_KEY, and ZTERMY_TEST_SSH_EXPECTED_FINGERPRINT values";
        return false;
    }

    window.resize(QSize{1120, 800});
    window.show();
    window.requestActivate();
    processWindowEventsFor(std::chrono::milliseconds{250});

    constexpr auto profileId = "real-host-ui-smoke-profile";
    if (!controller.saveHostProfile(QString::fromLatin1(profileId), QStringLiteral("Real host UI smoke"), host, port,
                                    username, QStringLiteral("private-key"), privateKeyPath, false,
                                    QStringLiteral("Runtime smoke"))
        || !controller.connectHostProfile(QString::fromLatin1(profileId), {}))
    {
        qCWarning(applicationLog) << "Real-host UI smoke could not create and connect its isolated profile";
        return false;
    }

    const auto activeTabState = [&controller]() -> QVariantMap {
        const QVariantList tabs = controller.terminalTabs();
        return tabs.isEmpty() ? QVariantMap{} : tabs.constFirst().toMap();
    };
    const bool reachedHostIdentity = processWindowEventsUntil(
        [&] {
            return controller.hostKeyPromptVisible() || activeTabState().value(QStringLiteral("running")).toBool();
        },
        std::chrono::seconds{10});
    if (!reachedHostIdentity)
    {
        qCWarning(applicationLog) << "Real-host UI smoke did not reach host-key confirmation or a connected session";
        return false;
    }
    if (controller.hostKeyPromptVisible())
    {
        if (controller.hostKeyChangedWarning() || controller.hostKeyFingerprint() != expectedFingerprint)
        {
            qCWarning(applicationLog) << "Real-host UI smoke rejected an unexpected host identity"
                                      << "algorithm=" << controller.hostKeyAlgorithm()
                                      << "fingerprint=" << controller.hostKeyFingerprint();
            controller.rejectHostKey();
            return false;
        }
        controller.acceptHostKey(true);
    }

    const bool connected = processWindowEventsUntil(
        [&] {
            return activeTabState().value(QStringLiteral("running")).toBool();
        },
        std::chrono::seconds{10});
    QQuickItem *rootObject = window.rootObject();
    if (rootObject == nullptr || !rootObject->setProperty("currentPage", QStringLiteral("terminal")))
    {
        qCWarning(applicationLog) << "Real-host UI smoke could not present the connected terminal workspace";
        return false;
    }
    processWindowEventsFor(std::chrono::milliseconds{100});
    QQuickItem *sftpAction = nullptr;
    const bool sftpActionReady = processWindowEventsUntil(
        [&] {
            sftpAction = visualQuickItem(rootObject, "terminalSftpAction");
            return sftpAction != nullptr && sftpAction->isVisible() && sftpAction->isEnabled();
        },
        std::chrono::seconds{2});
    if (!connected || !sftpActionReady)
    {
        qCWarning(applicationLog) << "Real-host UI smoke could not activate the connected terminal SFTP action"
                                  << "connected=" << connected;
        return false;
    }
    const bool sftpActionInvoked = QMetaObject::invokeMethod(sftpAction, "click", Qt::DirectConnection);
    processWindowEventsFor(std::chrono::milliseconds{100});
    if (!sftpActionInvoked)
    {
        qCWarning(applicationLog) << "Real-host UI smoke could not invoke the connected terminal SFTP action";
        return false;
    }

    bool unexpectedSftpHostIdentity = false;
    const bool sftpReady = processWindowEventsUntil(
        [&] {
            if (controller.hostKeyPromptVisible())
            {
                if (controller.hostKeyChangedWarning() || controller.hostKeyFingerprint() != expectedFingerprint)
                {
                    unexpectedSftpHostIdentity = true;
                    return true;
                }
                controller.acceptHostKey(true);
            }
            return controller.activeSftpState() == QStringLiteral("ready");
        },
        std::chrono::seconds{10});
    auto *directoryModel = qobject_cast<QAbstractItemModel *>(controller.activeSftpDirectoryModel());
    const QString homePath = controller.activeSftpHomePath();
    const QString currentPath = controller.activeSftpPath();
    const int directoryEntryCount = directoryModel == nullptr ? -1 : directoryModel->rowCount();
    if (!sftpReady || unexpectedSftpHostIdentity || directoryModel == nullptr || directoryEntryCount <= 0
        || homePath.isEmpty() || currentPath != homePath)
    {
        qCWarning(applicationLog) << "Real-host UI smoke did not load the remote home directory"
                                  << "state=" << controller.activeSftpState()
                                  << "error=" << controller.activeSftpError() << "home=" << homePath
                                  << "path=" << currentPath << "rows=" << directoryEntryCount;
        return false;
    }

    QQuickItem *browser = visualQuickItem(rootObject, "sftpBrowser");
    QQuickItem *pathField = visualQuickItem(rootObject, "sftpPathField");
    QQuickItem *bookmarkButton = visualQuickItem(rootObject, "sftpBookmarkButton");
    QQuickItem *copyPathButton = visualQuickItem(rootObject, "sftpCopyPathButton");
    QQuickItem *fileList = visualQuickItem(rootObject, "sftpFileList");
    QQuickItem *moreActionsButton = visualQuickItem(rootObject, "sftpMoreActionsButton");
    QQuickItem *refreshButton = visualQuickItem(rootObject, "sftpRefreshButton");
    QQuickItem *newFileButton = visualQuickItem(rootObject, "sftpNewFileButton");
    if (browser == nullptr || !focusItem(window, pathField, QStringLiteral("sftpPathField"))
        || !focusItem(window, bookmarkButton, QStringLiteral("sftpBookmarkButton")))
    {
        qCWarning(applicationLog) << "Real-host UI smoke could not traverse the SFTP path controls";
        return false;
    }
    sendKey(window, Qt::Key_Space);
    const bool pathBookmarked = processWindowEventsUntil(
        [&] {
            return controller.activeSftpPathBookmarked() && controller.bookmarkedSftpPaths().contains(currentPath);
        },
        std::chrono::seconds{1});
    if (!pathBookmarked || !focusItem(window, copyPathButton, QStringLiteral("sftpCopyPathButton")))
    {
        qCWarning(applicationLog) << "Real-host UI smoke could not use the SFTP bookmark control";
        return false;
    }
    sendKey(window, Qt::Key_Space);
    const bool pathCopied = processWindowEventsUntil(
        [&] {
            return QGuiApplication::clipboard()->text() == currentPath;
        },
        std::chrono::seconds{1});
    fileList->setProperty("currentIndex", 0);
    const bool fileListFocused = focusItem(window, fileList, QStringLiteral("sftpFileList"));
    sendKey(window, Qt::Key_Down);
    const bool fileListKeyboard = fileListFocused && fileList->property("currentIndex").toInt() >= 0;

    controller.setTerminalWorkbenchWidth(360);
    const bool narrowToolbar = processWindowEventsUntil(
        [&] {
            return browser->width() < 520 && moreActionsButton != nullptr && moreActionsButton->isVisible()
                   && refreshButton != nullptr && !refreshButton->isVisible();
        },
        std::chrono::seconds{2});
    const bool narrowCaptured =
        narrowToolbar && captureLayout(window, outputDirectory, QStringLiteral("real-host-sftp-narrow"));
    controller.setTerminalWorkbenchWidth(560);
    const bool wideToolbar = processWindowEventsUntil(
        [&] {
            return browser->width() >= 520 && moreActionsButton != nullptr && moreActionsButton->isVisible()
                   && refreshButton != nullptr && refreshButton->isVisible() && newFileButton != nullptr
                   && newFileButton->isVisible();
        },
        std::chrono::seconds{2});
    const bool newFileKeyboard = wideToolbar && focusItem(window, newFileButton, QStringLiteral("sftpNewFileButton"));
    const bool wideCaptured =
        wideToolbar && captureLayout(window, outputDirectory, QStringLiteral("real-host-sftp-wide"));

    const bool sortConfigured = controller.setSftpSort(QStringLiteral("size"), false)
                                && controller.activeSftpSortColumn() == QStringLiteral("size")
                                && !controller.activeSftpSortAscending();
    const bool columnsConfigured = controller.setSftpVisibleColumns(true, true, true)
                                   && controller.activeSftpShowModifiedColumn() && controller.activeSftpShowSizeColumn()
                                   && controller.activeSftpShowTypeColumn();
    const bool directoriesFirstConfigured =
        controller.setSftpDirectoriesFirst(true) && controller.activeSftpDirectoriesFirst();
    processWindowEventsFor(std::chrono::milliseconds{150});
    QQuickItem *nameHeader = visualQuickItem(rootObject, "sftpNameHeader");
    QQuickItem *modifiedHeader = visualQuickItem(rootObject, "sftpModifiedHeader");
    QQuickItem *sizeHeader = visualQuickItem(rootObject, "sftpSizeHeader");
    QQuickItem *typeHeader = visualQuickItem(rootObject, "sftpTypeHeader");
    const bool columnHeadersVisible = nameHeader != nullptr && nameHeader->isVisible() && modifiedHeader != nullptr
                                      && modifiedHeader->isVisible() && sizeHeader != nullptr && sizeHeader->isVisible()
                                      && typeHeader != nullptr && typeHeader->isVisible();
    const bool headerKeyboard = focusItem(window, sizeHeader, QStringLiteral("sftpSizeHeader"));
    if (headerKeyboard)
    {
        sendKey(window, Qt::Key_Space);
    }
    const bool headerSortToggled =
        controller.activeSftpSortColumn() == QStringLiteral("size") && controller.activeSftpSortAscending();
    const bool gb18030Started = controller.setSftpFilenameEncoding(QStringLiteral("gb18030"));
    const bool gb18030Ready = gb18030Started
                              && processWindowEventsUntil(
                                  [&] {
                                      return controller.activeSftpState() == QStringLiteral("ready")
                                             && controller.activeSftpFilenameEncoding() == QStringLiteral("gb18030");
                                  },
                                  std::chrono::seconds{10});
    const bool utf8Restarted = controller.setSftpFilenameEncoding(QStringLiteral("utf-8"));
    const bool utf8Ready = utf8Restarted
                           && processWindowEventsUntil(
                               [&] {
                                   return controller.activeSftpState() == QStringLiteral("ready")
                                          && controller.activeSftpFilenameEncoding() == QStringLiteral("utf-8")
                                          && controller.activeSftpPath() == homePath;
                               },
                               std::chrono::seconds{10});
    directoryModel = qobject_cast<QAbstractItemModel *>(controller.activeSftpDirectoryModel());

    bool deniedPathPreserved = true;
    bool deniedPathRecovered = true;
    if (!deniedSftpPath.isEmpty())
    {
        const bool navigationRequested = controller.navigateSftpDirectory(deniedSftpPath);
        deniedPathPreserved =
            navigationRequested
            && processWindowEventsUntil(
                [&] {
                    return controller.activeSftpState() == QStringLiteral("ready")
                           && !controller.activeSftpError().isEmpty();
                },
                std::chrono::seconds{10})
            && controller.activeSftpPath() == currentPath && directoryModel->rowCount() == directoryEntryCount
            && captureLayout(window, outputDirectory, QStringLiteral("real-host-sftp-permission-error"));
        controller.navigateSftpHome();
        deniedPathRecovered = processWindowEventsUntil(
            [&] {
                return controller.activeSftpState() == QStringLiteral("ready") && controller.activeSftpError().isEmpty()
                       && controller.activeSftpPath() == homePath;
            },
            std::chrono::seconds{10});
    }

    const auto transferById = [&controller](const QString &taskId) -> QVariantMap {
        const QVariantList tasks = controller.transferTasks();
        const auto position = std::ranges::find(tasks, taskId, [](const QVariant &value) {
            return value.toMap().value(QStringLiteral("id")).toString();
        });
        return position == tasks.end() ? QVariantMap{} : position->toMap();
    };
    const auto newestTransfer = [&controller](const QString &direction) -> QVariantMap {
        const QVariantList tasks = controller.transferTasks();
        for (const QVariant &value : std::views::reverse(tasks))
        {
            const QVariantMap task = value.toMap();
            if (task.value(QStringLiteral("direction")).toString() == direction)
            {
                return task;
            }
        }
        return {};
    };

    QDir().mkpath(outputDirectory);
    const QString transferToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString uploadPath =
        QDir(outputDirectory).filePath(QStringLiteral("v2-8-resume-upload-%1.bin").arg(transferToken));
    const QString downloadPath = QDir(outputDirectory).filePath(QStringLiteral("v2-8-resume-download.bin"));
    QFile::remove(uploadPath);
    QFile::remove(downloadPath);
    QFile uploadFile(uploadPath);
    constexpr qint64 transferSmokeBytes = qint64{64} * 1024 * 1024;
    const bool uploadFixtureCreated = uploadFile.open(QIODevice::WriteOnly) && uploadFile.resize(transferSmokeBytes);
    uploadFile.close();
    const bool uploadQueued =
        uploadFixtureCreated && controller.enqueueSftpUpload(QUrl::fromLocalFile(uploadPath).toString());
    QString uploadTaskId;
    const bool uploadRunning =
        uploadQueued
        && processWindowEventsUntil(
            [&] {
                const QVariantMap task = newestTransfer(QStringLiteral("upload"));
                uploadTaskId = task.value(QStringLiteral("id")).toString();
                return !uploadTaskId.isEmpty()
                       && task.value(QStringLiteral("status")).toString() == QStringLiteral("running")
                       && task.value(QStringLiteral("transferredBytes")).toULongLong() > 0;
            },
            std::chrono::seconds{15});
    if (uploadRunning)
    {
        controller.pauseTransfer(uploadTaskId);
    }
    const bool uploadPaused = uploadRunning
                              && processWindowEventsUntil(
                                  [&] {
                                      return transferById(uploadTaskId).value(QStringLiteral("status")).toString()
                                             == QStringLiteral("paused");
                                  },
                                  std::chrono::seconds{10});
    auto *transferCenter = rootObject->findChild<QObject *>(QStringLiteral("transferCenter"));
    const bool transferCenterOpened = uploadPaused && transferCenter != nullptr
                                      && QMetaObject::invokeMethod(transferCenter, "open", Qt::DirectConnection);
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool pausedTransferUi =
        transferCenterOpened && transferCenter->property("hasPausedTasks").toBool()
        && captureLayout(window, outputDirectory, QStringLiteral("real-host-transfer-paused"));
    if (uploadPaused)
    {
        controller.resumeTransfer(uploadTaskId);
    }
    const bool uploadCompleted = uploadPaused
                                 && processWindowEventsUntil(
                                     [&] {
                                         return transferById(uploadTaskId).value(QStringLiteral("status")).toString()
                                                == QStringLiteral("completed");
                                     },
                                     std::chrono::seconds{60});
    const QVariantMap completedUpload = transferById(uploadTaskId);
    const QString remoteTransferPath = completedUpload.value(QStringLiteral("destinationPath")).toString();

    const bool downloadQueued =
        uploadCompleted
        && controller.enqueueSftpDownload(remoteTransferPath, QUrl::fromLocalFile(downloadPath).toString(),
                                          static_cast<qulonglong>(transferSmokeBytes));
    QString downloadTaskId;
    const bool downloadRunning =
        downloadQueued
        && processWindowEventsUntil(
            [&] {
                const QVariantMap task = newestTransfer(QStringLiteral("download"));
                downloadTaskId = task.value(QStringLiteral("id")).toString();
                return !downloadTaskId.isEmpty()
                       && task.value(QStringLiteral("status")).toString() == QStringLiteral("running")
                       && task.value(QStringLiteral("transferredBytes")).toULongLong() > 0;
            },
            std::chrono::seconds{15});
    if (downloadRunning)
    {
        controller.pauseTransfer(downloadTaskId);
    }
    const bool downloadPaused = downloadRunning
                                && processWindowEventsUntil(
                                    [&] {
                                        return transferById(downloadTaskId).value(QStringLiteral("status")).toString()
                                               == QStringLiteral("paused");
                                    },
                                    std::chrono::seconds{10});
    if (downloadPaused)
    {
        controller.resumeTransfer(downloadTaskId);
    }
    const bool downloadCompleted =
        downloadPaused
        && processWindowEventsUntil(
            [&] {
                return transferById(downloadTaskId).value(QStringLiteral("status")).toString()
                       == QStringLiteral("completed");
            },
            std::chrono::seconds{60});
    processWindowEventsFor(std::chrono::milliseconds{150});
    const bool completedTransferUi =
        downloadCompleted && transferCenter != nullptr
        && transferCenter->property("completedDownloadDragAvailable").toBool()
        && captureLayout(window, outputDirectory, QStringLiteral("real-host-transfer-completed"));
    const bool downloadFileValid = QFileInfo(downloadPath).size() == transferSmokeBytes;
    const bool transferPathCopied = downloadCompleted && controller.copyTransferPath(downloadTaskId)
                                    && QGuiApplication::clipboard()->text() == downloadPath;
    const bool remoteTransferRemoved = !remoteTransferPath.isEmpty()
                                       && controller.removeSftpEntry(remoteTransferPath, false)
                                       && processWindowEventsUntil(
                                           [&] {
                                               return controller.activeSftpState() == QStringLiteral("ready");
                                           },
                                           std::chrono::seconds{10});
    controller.clearFinishedTransfers();
    QFile::remove(uploadPath);
    QFile::remove(downloadPath);

    QQuickItem *closeWorkbenchButton = visualQuickItem(rootObject, "closeTerminalWorkbenchButton");
    if (!pathCopied || !pathBookmarked || !fileListKeyboard || !narrowCaptured || !wideCaptured || !newFileKeyboard
        || !sortConfigured || !columnsConfigured || !directoriesFirstConfigured || !columnHeadersVisible
        || !headerKeyboard || !headerSortToggled || !gb18030Ready || !utf8Ready || !deniedPathPreserved
        || !deniedPathRecovered || !pausedTransferUi || !uploadCompleted || !downloadCompleted || !completedTransferUi
        || !downloadFileValid || !transferPathCopied || !remoteTransferRemoved
        || !focusItem(window, closeWorkbenchButton, QStringLiteral("closeTerminalWorkbenchButton")))
    {
        qCWarning(applicationLog) << "Real-host SFTP UI contract failed"
                                  << "pathCopied=" << pathCopied << "pathBookmarked=" << pathBookmarked
                                  << "fileListKeyboard=" << fileListKeyboard << "narrowToolbar=" << narrowToolbar
                                  << "wideToolbar=" << wideToolbar << "newFileKeyboard=" << newFileKeyboard
                                  << "sortConfigured=" << sortConfigured << "columnsConfigured=" << columnsConfigured
                                  << "directoriesFirstConfigured=" << directoriesFirstConfigured
                                  << "columnHeadersVisible=" << columnHeadersVisible
                                  << "headerSortToggled=" << headerSortToggled << "gb18030Ready=" << gb18030Ready
                                  << "utf8Ready=" << utf8Ready << "pausedTransferUi=" << pausedTransferUi
                                  << "uploadCompleted=" << uploadCompleted << "downloadCompleted=" << downloadCompleted
                                  << "completedTransferUi=" << completedTransferUi
                                  << "downloadFileValid=" << downloadFileValid
                                  << "transferPathCopied=" << transferPathCopied
                                  << "remoteTransferRemoved=" << remoteTransferRemoved
                                  << "deniedPathPreserved=" << deniedPathPreserved
                                  << "deniedPathRecovered=" << deniedPathRecovered;
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool workbenchClosed = processWindowEventsUntil(
        [&] {
            return !activeTabState().value(QStringLiteral("workbenchOpen")).toBool()
                   && terminalViewportHasFocus(window);
        },
        std::chrono::seconds{2});
    const QString tabId = activeTabState().value(QStringLiteral("id")).toString();
    if (controller.activeSftpPathBookmarked())
    {
        (void)controller.toggleActiveSftpBookmark();
    }
    controller.closeTerminalTab(tabId);
    const bool sessionClosed = processWindowEventsUntil(
        [&] {
            return controller.terminalTabs().isEmpty();
        },
        std::chrono::seconds{5});
    controller.deleteHostProfile(QString::fromLatin1(profileId));

    qCInfo(applicationLog) << "Real-host SFTP UI runtime check"
                           << "host=" << host << "rows=" << directoryEntryCount << "home=" << homePath
                           << "pathCopied=" << pathCopied << "pathBookmarked=" << pathBookmarked
                           << "fileListKeyboard=" << fileListKeyboard << "narrowToolbar=" << narrowToolbar
                           << "wideToolbar=" << wideToolbar << "newFileKeyboard=" << newFileKeyboard
                           << "sortConfigured=" << sortConfigured << "columnsConfigured=" << columnsConfigured
                           << "directoriesFirstConfigured=" << directoriesFirstConfigured
                           << "columnHeadersVisible=" << columnHeadersVisible
                           << "headerSortToggled=" << headerSortToggled << "gb18030Ready=" << gb18030Ready
                           << "utf8Ready=" << utf8Ready << "pausedTransferUi=" << pausedTransferUi
                           << "uploadCompleted=" << uploadCompleted << "downloadCompleted=" << downloadCompleted
                           << "completedTransferUi=" << completedTransferUi << "downloadFileValid=" << downloadFileValid
                           << "transferPathCopied=" << transferPathCopied
                           << "remoteTransferRemoved=" << remoteTransferRemoved
                           << "deniedPathPreserved=" << deniedPathPreserved
                           << "deniedPathRecovered=" << deniedPathRecovered << "workbenchClosed=" << workbenchClosed
                           << "sessionClosed=" << sessionClosed;
    return workbenchClosed && sessionClosed;
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

[[nodiscard]] bool terminalTabRunning(const ztermy::AppController &controller, const QString &tabId)
{
    const QVariantList tabs = controller.terminalTabs();
    const auto position = std::ranges::find(tabs, tabId, [](const QVariant &tab) {
        return tab.toMap().value(QStringLiteral("id")).toString();
    });
    return position != tabs.end() && position->toMap().value(QStringLiteral("running")).toBool();
}

[[nodiscard]] bool runLifecycleRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller)
{
    window.resize(QSize{1120, 800});
    window.show();
    window.requestActivate();
    processWindowEventsFor(std::chrono::milliseconds{200});

    constexpr int sequentialCycles = 8;
    qint64 maximumCloseMilliseconds = 0;
    for (int cycle = 0; cycle < sequentialCycles; ++cycle)
    {
        const QString tabId = controller.startLocalTerminal();
        if (tabId.isEmpty()
            || !processWindowEventsUntil(
                [&controller, &tabId] {
                    return terminalTabRunning(controller, tabId);
                },
                std::chrono::seconds{5}))
        {
            qCWarning(applicationLog) << "Lifecycle smoke could not start local terminal" << "cycle=" << cycle;
            return false;
        }
        auto *terminalItem = window.findChild<ztermy::ui::TerminalItem *>();
        if (terminalItem == nullptr)
        {
            return false;
        }
        terminalItem->inputGenerated(QByteArrayLiteral("Write-Output ('ZTERMY_LIFECYCLE_' + 'READY')\r"));
        processWindowEventsFor(std::chrono::milliseconds{40});
        QElapsedTimer closeTimer;
        closeTimer.start();
        if (!controller.closeTerminalTab(tabId))
        {
            return false;
        }
        maximumCloseMilliseconds = std::max(maximumCloseMilliseconds, closeTimer.elapsed());
        if (!processWindowEventsUntil(
                [&controller] {
                    return controller.terminalTabs().isEmpty();
                },
                std::chrono::seconds{3}))
        {
            qCWarning(applicationLog) << "Lifecycle smoke retained a closed tab" << "cycle=" << cycle;
            return false;
        }
    }

    constexpr int concurrentTabs = 3;
    QStringList finalTabs;
    for (int index = 0; index < concurrentTabs; ++index)
    {
        const QString tabId = controller.startLocalTerminal();
        if (tabId.isEmpty())
        {
            return false;
        }
        finalTabs.push_back(tabId);
    }
    const bool allRunning = processWindowEventsUntil(
        [&controller, &finalTabs] {
            return std::ranges::all_of(finalTabs, [&controller](const QString &tabId) {
                return terminalTabRunning(controller, tabId);
            });
        },
        std::chrono::seconds{8});
    if (allRunning)
    {
        auto *terminalItem = window.findChild<ztermy::ui::TerminalItem *>();
        if (terminalItem == nullptr)
        {
            return false;
        }
        terminalItem->inputGenerated(QByteArrayLiteral("1..2000 | ForEach-Object { \"ztermy lifecycle line $_\" }\r"));
        processWindowEventsFor(std::chrono::milliseconds{100});
    }
    qCInfo(applicationLog) << "Lifecycle runtime exercise"
                           << "sequentialCycles=" << sequentialCycles << "maximumCloseMs=" << maximumCloseMilliseconds
                           << "concurrentTabs=" << finalTabs.size() << "allRunning=" << allRunning;
    return allRunning && maximumCloseMilliseconds < 3000;
}

[[nodiscard]] bool runTerminalRenderRuntimeSmoke(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                                 const QString &outputDirectory, const bool exerciseSplitWorkspace)
{
    window.resize(QSize{1120, 800});
    window.show();
    window.requestActivate();
    if (exerciseSplitWorkspace)
    {
        processWindowEventsFor(std::chrono::milliseconds{250});
    }
    else
    {
        if (!processWindowEventsUntil(
                [&window] {
                    return window.isExposed();
                },
                std::chrono::seconds{30}))
        {
            qCWarning(applicationLog) << "Performance benchmark requires an interactive desktop with an exposed window";
            return false;
        }
        const QImage warmupFrame = window.grabWindow();
        if (warmupFrame.isNull()
            || !processWindowEventsUntil(
                [&window] {
                    return window.isSceneGraphInitialized();
                },
                std::chrono::seconds{10}))
        {
            qCWarning(applicationLog) << "Performance benchmark could not initialize its rendering surface";
            return false;
        }
        processWindowEventsFor(std::chrono::milliseconds{250});
    }

    if (controller.startLocalTerminal().isEmpty())
    {
        qCWarning(applicationLog) << "Terminal render smoke could not start the local terminal";
        return false;
    }
    if (QQuickItem *rootObject = window.rootObject(); rootObject != nullptr)
    {
        rootObject->setProperty("currentPage", QStringLiteral("terminal"));
    }
    processWindowEventsFor(std::chrono::milliseconds{100});

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
    auto *terminalItem = window.findChild<ztermy::ui::TerminalItem *>();
    if (terminalItem == nullptr)
    {
        qCWarning(applicationLog) << "Terminal render smoke could not find the active terminal viewport";
        return false;
    }
    terminalItem->resetPerformanceMetrics();
    terminalItem->setPerformanceMetricsEnabled(true);

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
    terminalItem->inputGenerated(QByteArrayLiteral("1..20000 | ForEach-Object { \"ztermy render line $_\" }; "
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
    const bool scrollbarExposed = terminalItem->scrollbarVisible() && terminalItem->scrollbarPageRatio() < 1.0
                                  && terminalItem->scrollbarPosition() > 0.9;
    const auto waitForScrollbarPosition = [&](const auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 2000 && !predicate(terminalItem->scrollbarPosition()))
        {
            processWindowEventsFor(std::chrono::milliseconds{20});
        }
        return predicate(terminalItem->scrollbarPosition());
    };
    terminalItem->scrollToFraction(0.0);
    const bool scrollbarReachedHistory = waitForScrollbarPosition([](const qreal position) {
        return position < 0.1;
    });
    terminalItem->scrollToFraction(1.0);
    const bool scrollbarReturnedToBottom = waitForScrollbarPosition([](const qreal position) {
        return position > 0.9;
    });
    const bool scrollbarPassed = scrollbarExposed && scrollbarReachedHistory && scrollbarReturnedToBottom;
    heartbeat.stop();
    QObject::disconnect(frameConnection);

    const ztermy::ui::TerminalRenderMetricsSnapshot renderMetrics = terminalItem->performanceMetrics();
    terminalItem->resetPerformanceMetrics();
    std::uint64_t idleFrameSwaps = 0;
    const QMetaObject::Connection idleFrameConnection =
        QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&idleFrameSwaps] {
            ++idleFrameSwaps;
        });
    constexpr auto idleDuration = std::chrono::milliseconds{2200};
    processWindowEventsFor(idleDuration);
    QObject::disconnect(idleFrameConnection);
    const ztermy::ui::TerminalRenderMetricsSnapshot idleRenderMetrics = terminalItem->performanceMetrics();
    terminalItem->setPerformanceMetricsEnabled(false);

    QDir().mkpath(outputDirectory);
    const QString capturePath = QDir(outputDirectory).filePath(QStringLiteral("terminal-render-complete.png"));
    const QImage capture = window.grabWindow();
    const bool captureSaved = capture.save(capturePath);
    const bool terminalRendered = terminalRegionHasRenderedContent(capture, *terminalItem);
    const bool completed = controller.terminalSearchTotal() > 0;
    const bool responsive = heartbeatTicks >= 20 && maximumHeartbeatGapMilliseconds <= 250;
    const bool progressiveFrames = frameSwaps >= 5;
    const bool resizeCompleted = compactResizeApplied && regularResizeRestored && window.size() == QSize{1120, 800};

    qCInfo(applicationLog) << "Terminal render runtime check"
                           << "completed=" << completed << "completionMs=" << completionMilliseconds
                           << "heartbeatTicks=" << heartbeatTicks
                           << "maxHeartbeatGapMs=" << maximumHeartbeatGapMilliseconds << "frameSwaps=" << frameSwaps
                           << "resizeCompleted=" << resizeCompleted << "captureSaved=" << captureSaved
                           << "terminalRendered=" << terminalRendered << "scrollbarPassed=" << scrollbarPassed
                           << "capture=" << capturePath;
    const bool baselinePassed = completed && responsive && progressiveFrames && resizeCompleted && captureSaved
                                && terminalRendered && scrollbarPassed;
    bool splitWorkspacePassed = false;
    QString splitCapturePath;
    if (!exerciseSplitWorkspace)
    {
        splitWorkspacePassed = true;
    }
    else if (baselinePassed && controller.splitActiveTerminal(QStringLiteral("horizontal"), false))
    {
        const bool secondPaneRunning = processWindowEventsUntil(
            [&controller] {
                const QVariantMap workspace = controller.activeTerminalWorkspace();
                return workspace.value(QStringLiteral("paneCount")).toInt() == 2 && !controller.terminalTabs().isEmpty()
                       && controller.terminalTabs().front().toMap().value(QStringLiteral("running")).toBool();
            },
            std::chrono::seconds{5});
        const QVariantMap workspace = controller.activeTerminalWorkspace();
        const QString splitNodeId =
            workspace.value(QStringLiteral("root")).toMap().value(QStringLiteral("id")).toString();
        const QString activePaneId = workspace.value(QStringLiteral("activePaneId")).toString();
        auto *activePane =
            window.findChild<ztermy::ui::TerminalItem *>(QStringLiteral("terminalViewport-") + activePaneId);
        const bool activePaneFound = activePane != nullptr;
        if (activePane != nullptr)
        {
            processWindowEventsFor(std::chrono::milliseconds{150});
            activePane->requestCurrentSize();
            activePane->inputGenerated(QByteArrayLiteral("Write-Output ztermy-split-ready\r"));
        }
        const bool splitOutputReady =
            activePane != nullptr
            && processWindowEventsUntil(
                [&controller] {
                    controller.searchTerminal(QStringLiteral("ztermy-split-ready"), false, true);
                    return controller.terminalSearchTotal() > 0;
                },
                std::chrono::seconds{5});
        const bool ratioStored = !splitNodeId.isEmpty() && controller.setTerminalSplitRatio(splitNodeId, 0.4);
        processWindowEventsFor(std::chrono::milliseconds{250});
        const QList<ztermy::ui::TerminalItem *> panes = window.findChildren<ztermy::ui::TerminalItem *>();
        const qsizetype usablePaneCount = std::ranges::count_if(panes, [](const ztermy::ui::TerminalItem *pane) {
            return pane != nullptr && pane->isVisible() && pane->width() >= 200 && pane->height() >= 150;
        });
        splitCapturePath = QDir(outputDirectory).filePath(QStringLiteral("terminal-workspace-split.png"));
        const bool splitCaptureSaved = window.grabWindow().save(splitCapturePath);
        const QString focusedBefore =
            controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString();
        const bool focusMoved =
            controller.focusRelativeTerminalPane(-1)
            && controller.activeTerminalWorkspace().value(QStringLiteral("activePaneId")).toString() != focusedBefore;
        const bool paneClosed = controller.closeActiveTerminalPane()
                                && controller.activeTerminalWorkspace().value(QStringLiteral("paneCount")).toInt() == 1;
        splitWorkspacePassed = secondPaneRunning && activePaneFound && splitOutputReady && ratioStored
                               && usablePaneCount == 2 && splitCaptureSaved && focusMoved && paneClosed;
        qCInfo(applicationLog) << "Terminal workspace runtime check"
                               << "secondPaneRunning=" << secondPaneRunning << "activePaneFound=" << activePaneFound
                               << "splitOutputReady=" << splitOutputReady << "ratioStored=" << ratioStored
                               << "usablePaneCount=" << usablePaneCount << "captureSaved=" << splitCaptureSaved
                               << "focusMoved=" << focusMoved << "paneClosed=" << paneClosed
                               << "capture=" << splitCapturePath;
    }

    const auto latencyJson = [](const ztermy::diagnostics::LatencySummary &summary) {
        return QJsonObject{
            {QStringLiteral("samples"), static_cast<qint64>(summary.count)},
            {QStringLiteral("p50UpperBoundUs"), static_cast<qint64>(summary.p50UpperBoundMicroseconds)},
            {QStringLiteral("p95UpperBoundUs"), static_cast<qint64>(summary.p95UpperBoundMicroseconds)},
            {QStringLiteral("p99UpperBoundUs"), static_cast<qint64>(summary.p99UpperBoundMicroseconds)},
            {QStringLiteral("maxUs"), static_cast<qint64>(summary.maxMicroseconds)},
        };
    };
    const auto graphicsApiName = [](const QSGRendererInterface::GraphicsApi api) {
        switch (api)
        {
            case QSGRendererInterface::Software:
                return QStringLiteral("software");
            case QSGRendererInterface::OpenVG:
                return QStringLiteral("openvg");
            case QSGRendererInterface::OpenGL:
                return QStringLiteral("opengl");
            case QSGRendererInterface::Direct3D11:
                return QStringLiteral("direct3d11");
            case QSGRendererInterface::Vulkan:
                return QStringLiteral("vulkan");
            case QSGRendererInterface::Metal:
                return QStringLiteral("metal");
            case QSGRendererInterface::Null:
                return QStringLiteral("null");
            case QSGRendererInterface::Direct3D12:
                return QStringLiteral("direct3d12");
            case QSGRendererInterface::Unknown:
                return QStringLiteral("unknown");
        }
        return QStringLiteral("unknown");
    };
    const QSGRendererInterface *rendererInterface = window.rendererInterface();
    int appliedDwmBackdrop = -1;
    const auto benchmarkWindowHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    static_cast<void>(queryDwmIntAttribute(benchmarkWindowHandle, kSystemBackdropTypeAttribute, &appliedDwmBackdrop));
    const QJsonObject report{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("generatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("environment"),
         QJsonObject{
             {QStringLiteral("applicationVersion"), QCoreApplication::applicationVersion()},
             {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
             {QStringLiteral("os"), QSysInfo::prettyProductName()},
             {QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture()},
#if defined(NDEBUG)
             {QStringLiteral("buildType"), QStringLiteral("release")},
#else
             {QStringLiteral("buildType"), QStringLiteral("debug")},
#endif
             {QStringLiteral("graphicsApi"),
              graphicsApiName(rendererInterface == nullptr ? QSGRendererInterface::Unknown
                                                           : rendererInterface->graphicsApi())},
             {QStringLiteral("requestedRhiBackend"), qEnvironmentVariable("QSG_RHI_BACKEND")},
             {QStringLiteral("preferSoftwareRenderer"),
              qEnvironmentVariableIntValue("QSG_RHI_PREFER_SOFTWARE_RENDERER") != 0},
             {QStringLiteral("devicePixelRatio"), window.effectiveDevicePixelRatio()},
             {QStringLiteral("logicalWidth"), window.width()},
             {QStringLiteral("logicalHeight"), window.height()},
             {QStringLiteral("alphaBufferBits"), window.format().alphaBufferSize()},
             {QStringLiteral("windowColorAlpha"), window.color().alpha()},
             {QStringLiteral("dwmBackdropType"), appliedDwmBackdrop},
             {QStringLiteral("backdrop"), requestedPerformanceBackdrop()},
             {QStringLiteral("terminalBackgroundOpacity"), controller.terminalBackgroundOpacity()},
         }},
        {QStringLiteral("scenario"),
         QJsonObject{
             {QStringLiteral("outputLines"), 20'000},
             {QStringLiteral("completed"), completed},
             {QStringLiteral("responsive"), responsive},
             {QStringLiteral("progressiveFrames"), progressiveFrames},
             {QStringLiteral("terminalRendered"), terminalRendered},
             {QStringLiteral("scrollbarPassed"), scrollbarPassed},
             {QStringLiteral("completionMs"), completionMilliseconds},
             {QStringLiteral("heartbeatTicks"), static_cast<qint64>(heartbeatTicks)},
             {QStringLiteral("maximumHeartbeatGapMs"), maximumHeartbeatGapMilliseconds},
             {QStringLiteral("frameSwaps"), static_cast<qint64>(frameSwaps)},
             {QStringLiteral("idleDurationMs"), static_cast<qint64>(idleDuration.count())},
             {QStringLiteral("idleFrameSwaps"), static_cast<qint64>(idleFrameSwaps)},
             {QStringLiteral("resizeCompleted"), resizeCompleted},
             {QStringLiteral("splitWorkspacePassed"), splitWorkspacePassed},
         }},
        {QStringLiteral("terminalRenderer"),
         QJsonObject{
             {QStringLiteral("paint"), latencyJson(renderMetrics.paintLatency)},
             {QStringLiteral("textureCreate"), latencyJson(renderMetrics.textureLatency)},
             {QStringLiteral("renderedFrames"), static_cast<qint64>(renderMetrics.renderedFrames)},
             {QStringLiteral("fullFrames"), static_cast<qint64>(renderMetrics.fullFrames)},
             {QStringLiteral("partialFrames"), static_cast<qint64>(renderMetrics.partialFrames)},
             {QStringLiteral("cleanFrames"), static_cast<qint64>(renderMetrics.cleanFrames)},
             {QStringLiteral("renderedDamagedRows"), static_cast<qint64>(renderMetrics.renderedDamagedRows)},
             {QStringLiteral("snapshotUpdates"), static_cast<qint64>(renderMetrics.snapshotUpdates)},
             {QStringLiteral("fullSnapshotUpdates"), static_cast<qint64>(renderMetrics.fullSnapshotUpdates)},
             {QStringLiteral("partialSnapshotUpdates"), static_cast<qint64>(renderMetrics.partialSnapshotUpdates)},
             {QStringLiteral("cleanSnapshotUpdates"), static_cast<qint64>(renderMetrics.cleanSnapshotUpdates)},
             {QStringLiteral("snapshotDamagedRows"), static_cast<qint64>(renderMetrics.snapshotDamagedRows)},
             {QStringLiteral("cursorInvalidations"), static_cast<qint64>(renderMetrics.cursorInvalidations)},
             {QStringLiteral("uploadedBytes"), static_cast<qint64>(renderMetrics.uploadedBytes)},
             {QStringLiteral("maximumFramePixels"), static_cast<qint64>(renderMetrics.maximumFramePixels)},
             {QStringLiteral("rowReuseAnalysisFrames"), static_cast<qint64>(renderMetrics.rowReuseAnalysisFrames)},
             {QStringLiteral("rowReuseCandidateRows"), static_cast<qint64>(renderMetrics.rowReuseCandidateRows)},
             {QStringLiteral("rowReuseReusableRows"), static_cast<qint64>(renderMetrics.rowReuseReusableRows)},
             {QStringLiteral("rowReuseRepaintRows"), static_cast<qint64>(renderMetrics.rowReuseRepaintRows)},
             {QStringLiteral("rowReuseShiftedFrames"), static_cast<qint64>(renderMetrics.rowReuseShiftedFrames)},
             {QStringLiteral("imagePreparation"), latencyJson(renderMetrics.imagePreparationLatency)},
             {QStringLiteral("snapshotPreparation"), latencyJson(renderMetrics.snapshotPreparationLatency)},
             {QStringLiteral("backgroundPaint"), latencyJson(renderMetrics.backgroundPaintLatency)},
             {QStringLiteral("textPaint"), latencyJson(renderMetrics.textPaintLatency)},
             {QStringLiteral("overlayPaint"), latencyJson(renderMetrics.overlayPaintLatency)},
         }},
        {QStringLiteral("idleTerminalRenderer"),
         QJsonObject{
             {QStringLiteral("paint"), latencyJson(idleRenderMetrics.paintLatency)},
             {QStringLiteral("textureCreate"), latencyJson(idleRenderMetrics.textureLatency)},
             {QStringLiteral("renderedFrames"), static_cast<qint64>(idleRenderMetrics.renderedFrames)},
             {QStringLiteral("fullFrames"), static_cast<qint64>(idleRenderMetrics.fullFrames)},
             {QStringLiteral("partialFrames"), static_cast<qint64>(idleRenderMetrics.partialFrames)},
             {QStringLiteral("cleanFrames"), static_cast<qint64>(idleRenderMetrics.cleanFrames)},
             {QStringLiteral("renderedDamagedRows"), static_cast<qint64>(idleRenderMetrics.renderedDamagedRows)},
             {QStringLiteral("snapshotUpdates"), static_cast<qint64>(idleRenderMetrics.snapshotUpdates)},
             {QStringLiteral("cursorInvalidations"), static_cast<qint64>(idleRenderMetrics.cursorInvalidations)},
             {QStringLiteral("uploadedBytes"), static_cast<qint64>(idleRenderMetrics.uploadedBytes)},
             {QStringLiteral("maximumFramePixels"), static_cast<qint64>(idleRenderMetrics.maximumFramePixels)},
         }},
    };
    const QString reportPath = QDir(outputDirectory).filePath(QStringLiteral("terminal-performance.json"));
    QSaveFile reportFile(reportPath);
    const QByteArray reportBytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const bool reportSaved = reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                             && reportFile.write(reportBytes) == reportBytes.size() && reportFile.commit();
    qCInfo(applicationLog) << "Terminal performance evidence"
                           << "saved=" << reportSaved << "path=" << reportPath
                           << "paintP95Us=" << renderMetrics.paintLatency.p95UpperBoundMicroseconds
                           << "textureP95Us=" << renderMetrics.textureLatency.p95UpperBoundMicroseconds
                           << "uploadedBytes=" << renderMetrics.uploadedBytes;
    return baselinePassed && splitWorkspacePassed && reportSaved;
}

[[nodiscard]] bool runUiPerformanceBenchmark(ztermy::NativeWindow &window, ztermy::AppController &controller,
                                             const QString &outputDirectory, const qint64 qmlLoadMilliseconds)
{
    window.resize(QSize{1120, 800});
    window.show();
    window.requestActivate();
    if (!processWindowEventsUntil(
            [&window] {
                return window.isExposed() && window.isSceneGraphInitialized();
            },
            std::chrono::seconds{30}))
    {
        qCWarning(applicationLog) << "UI performance benchmark requires an interactive rendering surface";
        return false;
    }
    processWindowEventsFor(std::chrono::milliseconds{250});

    QQuickItem *rootObject = window.rootObject();
    if (rootObject == nullptr)
    {
        return false;
    }
    const auto objectCount = [rootObject] {
        return rootObject->findChildren<QObject *>().size() + 1;
    };
    const auto quickItemCount = [rootObject] {
        return rootObject->findChildren<QQuickItem *>().size() + 1;
    };
    const qsizetype initialObjectCount = objectCount();
    const qsizetype initialQuickItemCount = quickItemCount();

    QElapsedTimer terminalStartup;
    terminalStartup.start();
    if (controller.startLocalTerminal().isEmpty())
    {
        qCWarning(applicationLog) << "UI performance benchmark could not start the local terminal";
        return false;
    }
    rootObject->setProperty("currentPage", QStringLiteral("terminal"));
    const bool terminalReady = processWindowEventsUntil(
        [&controller] {
            return !controller.terminalTabs().isEmpty()
                   && controller.terminalTabs().front().toMap().value(QStringLiteral("running")).toBool();
        },
        std::chrono::seconds{10});
    const qint64 terminalStartupMilliseconds = terminalStartup.elapsed();
    processWindowEventsFor(std::chrono::milliseconds{250});
    const qsizetype closedWorkbenchObjectCount = objectCount();
    const qsizetype closedWorkbenchQuickItemCount = quickItemCount();

    const QString requestedWorkbenchPage = qEnvironmentVariable("ZTERMY_UI_BENCHMARK_PAGE").trimmed();
    const QString workbenchPage = requestedWorkbenchPage.isEmpty() ? QStringLiteral("ai") : requestedWorkbenchPage;
    const bool aiWorkbench = workbenchPage == QStringLiteral("ai");
    QElapsedTimer workbenchOpen;
    workbenchOpen.start();
    const bool workbenchRequested = controller.toggleTerminalWorkbench(workbenchPage);
    const bool workbenchVisible =
        workbenchRequested
        && processWindowEventsUntil(
            [rootObject, aiWorkbench] {
                auto *workbench = rootObject->findChild<QQuickItem *>(QStringLiteral("terminalWorkbench"));
                auto *assistant = rootObject->findChild<QQuickItem *>(QStringLiteral("aiAssistantPane"));
                return workbench != nullptr && workbench->isVisible()
                       && (!aiWorkbench
                           || (assistant != nullptr && assistant->isVisible() && assistant->width() > 0
                               && assistant->height() > 0));
            },
            std::chrono::seconds{10});
    const qint64 workbenchOpenMilliseconds = workbenchOpen.elapsed();
    processWindowEventsFor(std::chrono::milliseconds{250});
    const qsizetype openWorkbenchObjectCount = objectCount();
    const qsizetype openWorkbenchQuickItemCount = quickItemCount();

    auto *conversation =
        aiWorkbench ? qobject_cast<ztermy::ai::AiConversationModel *>(controller.activeAiConversation()) : nullptr;
    if (!terminalReady || !workbenchVisible || (aiWorkbench && conversation == nullptr))
    {
        qCWarning(applicationLog) << "UI performance benchmark could not prepare the AI workbench";
        return false;
    }
    bool streamChunkOverrideValid = false;
    const int requestedStreamChunkCount =
        qEnvironmentVariableIntValue("ZTERMY_UI_BENCHMARK_CHUNKS", &streamChunkOverrideValid);
    const int streamChunkCount = streamChunkOverrideValid ? std::clamp(requestedStreamChunkCount, 0, 2'000) : 240;
    std::uint64_t assistantMessageId = 0;
    if (streamChunkCount > 0 && conversation != nullptr)
    {
        conversation->clear();
        static_cast<void>(
            conversation->appendUserMessage(QStringLiteral("Summarize a representative terminal workload.")));
        assistantMessageId = conversation->beginAssistantMessage();
    }
    constexpr int streamIntervalMilliseconds = 8;
    const QString streamChunk = QStringLiteral(
        "- Inspect **service health**, compare `current` with `expected`, and preserve the diagnostic evidence.\n");
    int chunksDelivered = 0;
    QTimer streamTimer;
    streamTimer.setInterval(streamIntervalMilliseconds);
    streamTimer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&streamTimer, &QTimer::timeout, &window, [&] {
        if (chunksDelivered >= streamChunkCount)
        {
            streamTimer.stop();
            return;
        }
        static_cast<void>(conversation->appendAssistantDelta(assistantMessageId, streamChunk));
        ++chunksDelivered;
    });

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
    heartbeat.setTimerType(Qt::PreciseTimer);
    QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
        maximumHeartbeatGapMilliseconds = std::max(maximumHeartbeatGapMilliseconds, heartbeatGap.restart());
        ++heartbeatTicks;
    });

    QElapsedTimer streamElapsed;
    streamElapsed.start();
    heartbeat.start();
    if (streamChunkCount > 0)
    {
        streamTimer.start();
    }
    const bool streamCompleted = streamChunkCount == 0
                                 || processWindowEventsUntil(
                                     [&] {
                                         return chunksDelivered == streamChunkCount;
                                     },
                                     std::chrono::seconds{15});
    streamTimer.stop();
    if (streamCompleted && streamChunkCount > 0)
    {
        static_cast<void>(conversation->completeAssistantMessage(assistantMessageId));
    }
    processWindowEventsFor(std::chrono::milliseconds{350});
    const qint64 streamMilliseconds = streamElapsed.elapsed();
    heartbeat.stop();
    QObject::disconnect(frameConnection);

    const bool workbenchClosed = controller.toggleTerminalWorkbench(workbenchPage)
                                 && processWindowEventsUntil(
                                     [rootObject] {
                                         auto *workbench =
                                             rootObject->findChild<QQuickItem *>(QStringLiteral("terminalWorkbench"));
                                         return workbench == nullptr || !workbench->isVisible();
                                     },
                                     std::chrono::seconds{5});
    const qsizetype retainedWorkbenchObjectCount = objectCount();
    const qsizetype retainedWorkbenchQuickItemCount = quickItemCount();
    QElapsedTimer workbenchReopen;
    workbenchReopen.start();
    const bool workbenchReopened =
        controller.toggleTerminalWorkbench(workbenchPage)
        && processWindowEventsUntil(
            [rootObject, aiWorkbench] {
                auto *workbench = rootObject->findChild<QQuickItem *>(QStringLiteral("terminalWorkbench"));
                auto *assistant = rootObject->findChild<QQuickItem *>(QStringLiteral("aiAssistantPane"));
                return workbench != nullptr && workbench->isVisible()
                       && (!aiWorkbench
                           || (assistant != nullptr && assistant->isVisible() && assistant->width() > 0
                               && assistant->height() > 0));
            },
            std::chrono::seconds{5});
    const qint64 workbenchReopenMilliseconds = workbenchReopen.elapsed();

    QDir().mkpath(outputDirectory);
    const QString capturePath = QDir(outputDirectory).filePath(QStringLiteral("ui-performance-complete.png"));
    const bool captureEnabled = qEnvironmentVariable("ZTERMY_UI_BENCHMARK_CAPTURE") != QStringLiteral("0");
    const bool captureSaved = !captureEnabled || window.grabWindow().save(capturePath);
    const QSGRendererInterface *rendererInterface = window.rendererInterface();
    const auto graphicsApiName = [](const QSGRendererInterface::GraphicsApi api) {
        switch (api)
        {
            case QSGRendererInterface::Software:
                return QStringLiteral("software");
            case QSGRendererInterface::OpenGL:
                return QStringLiteral("opengl");
            case QSGRendererInterface::Direct3D11:
                return QStringLiteral("direct3d11");
            case QSGRendererInterface::Direct3D12:
                return QStringLiteral("direct3d12");
            case QSGRendererInterface::Vulkan:
                return QStringLiteral("vulkan");
            case QSGRendererInterface::Metal:
                return QStringLiteral("metal");
            case QSGRendererInterface::OpenVG:
                return QStringLiteral("openvg");
            case QSGRendererInterface::Null:
                return QStringLiteral("null");
            case QSGRendererInterface::Unknown:
                return QStringLiteral("unknown");
        }
        return QStringLiteral("unknown");
    };
    int appliedDwmBackdrop = -1;
    const auto benchmarkWindowHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
    static_cast<void>(queryDwmIntAttribute(benchmarkWindowHandle, kSystemBackdropTypeAttribute, &appliedDwmBackdrop));
    const QJsonObject report{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("generatedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("environment"),
         QJsonObject{
             {QStringLiteral("applicationVersion"), QCoreApplication::applicationVersion()},
             {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
             {QStringLiteral("os"), QSysInfo::prettyProductName()},
#if defined(NDEBUG)
             {QStringLiteral("buildType"), QStringLiteral("release")},
#else
             {QStringLiteral("buildType"), QStringLiteral("debug")},
#endif
             {QStringLiteral("graphicsApi"),
              graphicsApiName(rendererInterface == nullptr ? QSGRendererInterface::Unknown
                                                           : rendererInterface->graphicsApi())},
             {QStringLiteral("requestedRhiBackend"), qEnvironmentVariable("QSG_RHI_BACKEND")},
             {QStringLiteral("preferSoftwareRenderer"),
              qEnvironmentVariableIntValue("QSG_RHI_PREFER_SOFTWARE_RENDERER") != 0},
             {QStringLiteral("devicePixelRatio"), window.effectiveDevicePixelRatio()},
             {QStringLiteral("logicalWidth"), window.width()},
             {QStringLiteral("logicalHeight"), window.height()},
             {QStringLiteral("alphaBufferBits"), window.format().alphaBufferSize()},
             {QStringLiteral("windowColorAlpha"), window.color().alpha()},
             {QStringLiteral("dwmBackdropType"), appliedDwmBackdrop},
             {QStringLiteral("backdrop"), requestedPerformanceBackdrop()},
         }},
        {QStringLiteral("startup"),
         QJsonObject{
             {QStringLiteral("qmlLoadMs"), qmlLoadMilliseconds},
             {QStringLiteral("terminalReadyMs"), terminalStartupMilliseconds},
             {QStringLiteral("initialObjects"), static_cast<qint64>(initialObjectCount)},
             {QStringLiteral("initialQuickItems"), static_cast<qint64>(initialQuickItemCount)},
             {QStringLiteral("closedWorkbenchObjects"), static_cast<qint64>(closedWorkbenchObjectCount)},
             {QStringLiteral("closedWorkbenchQuickItems"), static_cast<qint64>(closedWorkbenchQuickItemCount)},
         }},
        {QStringLiteral("workbench"),
         QJsonObject{
             {QStringLiteral("openMs"), workbenchOpenMilliseconds},
             {QStringLiteral("visible"), workbenchVisible},
             {QStringLiteral("openObjects"), static_cast<qint64>(openWorkbenchObjectCount)},
             {QStringLiteral("openQuickItems"), static_cast<qint64>(openWorkbenchQuickItemCount)},
             {QStringLiteral("closed"), workbenchClosed},
             {QStringLiteral("retainedObjects"), static_cast<qint64>(retainedWorkbenchObjectCount)},
             {QStringLiteral("retainedQuickItems"), static_cast<qint64>(retainedWorkbenchQuickItemCount)},
             {QStringLiteral("reopened"), workbenchReopened},
             {QStringLiteral("reopenMs"), workbenchReopenMilliseconds},
         }},
        {QStringLiteral("stream"),
         QJsonObject{
             {QStringLiteral("chunkCount"), chunksDelivered},
             {QStringLiteral("chunkIntervalMs"), streamIntervalMilliseconds},
             {QStringLiteral("durationMs"), streamMilliseconds},
             {QStringLiteral("heartbeatTicks"), static_cast<qint64>(heartbeatTicks)},
             {QStringLiteral("maximumHeartbeatGapMs"), maximumHeartbeatGapMilliseconds},
             {QStringLiteral("frameSwaps"), static_cast<qint64>(frameSwaps)},
             {QStringLiteral("completed"), streamCompleted},
         }},
    };
    const QString reportPath = QDir(outputDirectory).filePath(QStringLiteral("ui-performance.json"));
    QSaveFile reportFile(reportPath);
    const QByteArray reportBytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const bool reportSaved = reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                             && reportFile.write(reportBytes) == reportBytes.size() && reportFile.commit();
    qCInfo(applicationLog) << "UI performance evidence"
                           << "saved=" << reportSaved << "path=" << reportPath << "qmlLoadMs=" << qmlLoadMilliseconds
                           << "terminalReadyMs=" << terminalStartupMilliseconds
                           << "closedObjects=" << closedWorkbenchObjectCount
                           << "openObjects=" << openWorkbenchObjectCount
                           << "workbenchOpenMs=" << workbenchOpenMilliseconds
                           << "workbenchReopenMs=" << workbenchReopenMilliseconds << "streamMs=" << streamMilliseconds
                           << "maxHeartbeatGapMs=" << maximumHeartbeatGapMilliseconds << "frameSwaps=" << frameSwaps;
    return terminalReady && workbenchVisible && workbenchClosed && workbenchReopened && streamCompleted && captureSaved
           && reportSaved && heartbeatTicks >= 20 && maximumHeartbeatGapMilliseconds <= 250;
}

} // namespace

// Qt framework entry points are exception-opaque. Let unexpected failures reach
// the process crash-diagnostics boundary instead of swallowing them here.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[])
{
    const bool rawPerformanceBenchmark = rawArgumentPresent(argc, argv, "--performance-benchmark")
                                         || rawArgumentPresent(argc, argv, "--ui-performance-benchmark");
    const bool opaquePerformanceSurface =
        rawPerformanceBenchmark && requestedPerformanceBackdrop() == QStringLiteral("opaque");
    QQuickWindow::setDefaultAlphaBuffer(!opaquePerformanceSurface);
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationName(QStringLiteral("ztermy"));
    QGuiApplication::setApplicationVersion(QStringLiteral(ZTERMY_VERSION_STRING));
    QGuiApplication::setOrganizationName(QStringLiteral("ztermy"));
    const QIcon applicationIcon(QStringLiteral(":/ztermy/branding/app-icon.svg"));
    if (applicationIcon.isNull() || applicationIcon.pixmap(QSize{32, 32}).isNull())
    {
        qCritical() << "Could not load the embedded ztermy application icon";
        return EXIT_FAILURE;
    }
    QGuiApplication::setWindowIcon(applicationIcon);

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
    ztermy::diagnostics::DiagnosticReporter diagnosticReporter(*paths);
    qInfo().noquote() << "storageMode=" << ztermy::config::storageModeName(paths->mode)
                      << "dataDirectory=" << paths->dataDirectory;
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qRegisterMetaType<ztermy::terminal::TerminalSnapshotPtr>();

    ztermy::AppController appController(paths->profilesFile, paths->knownHostsFile, paths->settingsFile,
                                        paths->credentialsFile, paths->mode);
    const auto refreshAiPrivacyDiagnostics = [&appController, &diagnosticReporter] {
        diagnosticReporter.setAiPrivacySummary(QJsonObject::fromVariantMap(appController.aiPrivacyDiagnostics()));
    };
    refreshAiPrivacyDiagnostics();
    QObject::connect(&appController, &ztermy::AppController::aiPrivacyDiagnosticsChanged, &diagnosticReporter,
                     refreshAiPrivacyDiagnostics);
    ztermy::FontCatalog fontCatalog;
    fontCatalog.applyUiFont(appController.uiFontFamily());
    const bool uiLayoutSmoke = QCoreApplication::arguments().contains(QStringLiteral("--ui-layout-smoke"));
    const bool uiKeyboardSmoke = QCoreApplication::arguments().contains(QStringLiteral("--ui-keyboard-smoke"));
    const bool realHostUiSmoke = QCoreApplication::arguments().contains(QStringLiteral("--real-host-ui-smoke"));
    const bool terminalPerformanceBenchmark =
        QCoreApplication::arguments().contains(QStringLiteral("--performance-benchmark"));
    const bool uiPerformanceBenchmark =
        QCoreApplication::arguments().contains(QStringLiteral("--ui-performance-benchmark"));
    const bool performanceBenchmark = terminalPerformanceBenchmark || uiPerformanceBenchmark;
    const QString performanceBackdrop = requestedPerformanceBackdrop();
    if (performanceBenchmark && !validPerformanceBackdrop(performanceBackdrop))
    {
        qCCritical(applicationLog) << "Unsupported performance backdrop" << performanceBackdrop;
        return EXIT_FAILURE;
    }
    const bool terminalRenderSmoke = QCoreApplication::arguments().contains(QStringLiteral("--terminal-render-smoke"))
                                     || terminalPerformanceBenchmark;
    const bool lifecycleRuntimeSmoke =
        QCoreApplication::arguments().contains(QStringLiteral("--lifecycle-runtime-smoke"));
    const bool windowAppearanceSmoke =
        QCoreApplication::arguments().contains(QStringLiteral("--window-appearance-smoke"));
    const bool windowResizeSmoke = QCoreApplication::arguments().contains(QStringLiteral("--window-resize-smoke"));
    const bool windowDpiSmoke = QCoreApplication::arguments().contains(QStringLiteral("--window-dpi-smoke"));
    ztermy::LocalizationManager localizationManager;
    const auto initialLanguage = uiLayoutSmoke || uiKeyboardSmoke || realHostUiSmoke || performanceBenchmark
                                     ? std::optional{ztermy::config::LanguagePreference::english}
                                     : ztermy::config::parseLanguagePreference(appController.languagePreference());
    if (!initialLanguage || !localizationManager.apply(*initialLanguage))
    {
        qCCritical(applicationLog) << "Could not apply the configured UI language";
        return EXIT_FAILURE;
    }
    const bool automatedSettingsSaved =
        performanceBenchmark
            ? applyUiLayoutSmokeTheme(appController, QStringLiteral("dark"), QStringLiteral("en"),
                                      settingsBackdropForPerformance(performanceBackdrop))
            : (!(uiLayoutSmoke || uiKeyboardSmoke || realHostUiSmoke)
               || applyUiLayoutSmokeTheme(appController, QStringLiteral("dark"), QStringLiteral("en")));
    if (!automatedSettingsSaved)
    {
        qCCritical(applicationLog) << "Could not prepare the UI runtime smoke settings";
        return EXIT_FAILURE;
    }
    if ((uiLayoutSmoke || uiKeyboardSmoke)
        && !appController.saveHostProfile(QStringLiteral("ui-layout-smoke-profile"), QStringLiteral("Layout test host"),
                                          QStringLiteral("192.0.2.10"), 22, QStringLiteral("developer"),
                                          QStringLiteral("private-key"), QStringLiteral("C:/test/id_ed25519"), false,
                                          QStringLiteral("Test fixtures")))
    {
        qCCritical(applicationLog) << "Could not prepare the responsive UI layout fixture";
        return EXIT_FAILURE;
    }

    ztermy::NativeWindow window;
    auto iconImageProvider = std::make_unique<ztermy::ui::SvgIconImageProvider>();
    window.engine()->addImageProvider(QStringLiteral("ztermy-icons"), iconImageProvider.release());
    auto brandImageProvider = std::make_unique<ztermy::ui::SvgIconImageProvider>(QStringLiteral(":/ztermy/branding"));
    window.engine()->addImageProvider(QStringLiteral("ztermy-brand"), brandImageProvider.release());
    QVariantMap initialProperties;
    initialProperties.insert(QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject *>(&appController)));
    initialProperties.insert(QStringLiteral("fontCatalog"), QVariant::fromValue(static_cast<QObject *>(&fontCatalog)));
    initialProperties.insert(QStringLiteral("diagnostics"),
                             QVariant::fromValue(static_cast<QObject *>(&diagnosticReporter)));
    QElapsedTimer qmlLoadTimer;
    qmlLoadTimer.start();
    if (!window.load(initialProperties))
    {
        return EXIT_FAILURE;
    }
    const qint64 qmlLoadMilliseconds = qmlLoadTimer.elapsed();
    const bool closeToTrayAllowed = !QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"));
    window.setCloseToTrayEnabled(closeToTrayAllowed && appController.closeToTray());
    QObject::connect(&appController, &ztermy::AppController::applicationSettingsChanged, &window,
                     [&appController, &fontCatalog, &localizationManager, &window, closeToTrayAllowed] {
                         window.setCloseToTrayEnabled(closeToTrayAllowed && appController.closeToTray());
                         fontCatalog.applyUiFont(appController.uiFontFamily());
                         const auto language =
                             ztermy::config::parseLanguagePreference(appController.languagePreference());
                         if (!language || !localizationManager.apply(*language, window.engine()))
                         {
                             qCWarning(applicationLog) << "Could not apply the updated UI language";
                             return;
                         }
                         appController.retranslateUiState();
                     });

    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test")))
    {
        QTimer::singleShot(50, &window, &QWindow::close);
        window.show();
        const int smokeExitCode = application.exec();
        appController.shutdown();
        window.releaseResources();
        if (smokeExitCode != EXIT_SUCCESS)
        {
            qCCritical(applicationLog) << "QML window close smoke test returned" << smokeExitCode;
            return EXIT_FAILURE;
        }
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
        const bool passed = runWindowAppearanceRuntimeSmoke(window, appController);
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
        const bool passed = runUiKeyboardRuntimeSmoke(window, appController, paths->dataDirectory);
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
    if (realHostUiSmoke)
    {
        const bool passed = runRealHostUiRuntimeSmoke(window, appController, paths->dataDirectory);
        appController.shutdown();
        window.releaseResources();
        if (!passed)
        {
            qCCritical(applicationLog) << "Real-host SFTP UI runtime smoke test failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Real-host SFTP UI runtime smoke test completed";
        return EXIT_SUCCESS;
    }
    if (lifecycleRuntimeSmoke)
    {
        const bool exercised = runLifecycleRuntimeSmoke(window, appController);
        QElapsedTimer shutdownTimer;
        shutdownTimer.start();
        appController.shutdown();
        const qint64 shutdownMilliseconds = shutdownTimer.elapsed();
        const bool shutdownPassed = shutdownMilliseconds < 5000 && appController.terminalTabs().isEmpty();
        window.releaseResources();
        if (!exercised || !shutdownPassed)
        {
            qCCritical(applicationLog) << "Lifecycle runtime smoke test failed"
                                       << "exercised=" << exercised << "shutdownMs=" << shutdownMilliseconds
                                       << "shutdownPassed=" << shutdownPassed;
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "Lifecycle runtime smoke test completed"
                               << "shutdownMs=" << shutdownMilliseconds;
        return EXIT_SUCCESS;
    }
    if (terminalRenderSmoke)
    {
        const bool passed =
            runTerminalRenderRuntimeSmoke(window, appController, paths->dataDirectory, !terminalPerformanceBenchmark);
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
    if (uiPerformanceBenchmark)
    {
        const bool passed = runUiPerformanceBenchmark(window, appController, paths->dataDirectory, qmlLoadMilliseconds);
        window.setCloseToTrayEnabled(false);
        QTimer::singleShot(0, &window, &QWindow::close);
        const int benchmarkExitCode = application.exec();
        appController.shutdown();
        window.releaseResources();
        if (!passed || benchmarkExitCode != EXIT_SUCCESS)
        {
            qCCritical(applicationLog) << "UI performance benchmark failed";
            return EXIT_FAILURE;
        }
        qCInfo(applicationLog) << "UI performance benchmark completed";
        return EXIT_SUCCESS;
    }

    window.show();
    const int exitCode = application.exec();

    qCInfo(applicationLog) << "Application event loop stopped; beginning orderly shutdown";
    appController.shutdown();
    window.releaseResources();
    qCInfo(applicationLog) << "Terminal and scene graph resources released";
    return exitCode;
}
