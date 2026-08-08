#include "application/AppController.h"
#include "application/FontCatalog.h"
#include "application/LocalizationManager.h"
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
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLoggingCategory>
#include <QMetaType>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
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
    constexpr DWORD systemBackdropTypeAttribute = 38;
    const bool darkModeRead = queryDwmIntAttribute(windowHandle, useImmersiveDarkModeAttribute, &appliedDarkMode);
    const bool cornerPreferenceRead =
        queryDwmIntAttribute(windowHandle, windowCornerPreferenceAttribute, &appliedCornerPreference);
    const bool backdropRead = queryDwmIntAttribute(windowHandle, systemBackdropTypeAttribute, &appliedBackdrop);
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
    bool securityCaptured = false;
    bool securityMatches = false;
    bool shortcutsCaptured = false;
    bool shortcutsMatch = false;
    bool sftpCaptured = false;
    bool sftpMatches = false;
    if (settingsPane != nullptr)
    {
        settingsPane->setProperty("currentCategory", QStringLiteral("application"));
        processWindowEventsFor(std::chrono::milliseconds{500});
        auto *brandLockup = rootObject->findChild<QObject *>(QStringLiteral("settingsApplicationBrandLockup"));
        auto *releaseIdentity = rootObject->findChild<QObject *>(QStringLiteral("settingsReleaseIdentityCard"));
        auto *diagnosticsCard = rootObject->findChild<QObject *>(QStringLiteral("settingsDiagnosticsCard"));
        auto *buildInfo = rootObject->findChild<QObject *>(QStringLiteral("settingsApplicationBuildInfo"));
        applicationCaptured = captureLayout(window, outputDirectory, capturePrefix + QStringLiteral("-application"));
        applicationMatches =
            brandLockup != nullptr && brandLockup->property("visible").toBool()
            && brandLockup->property("width").toReal() > 0.0 && brandLockup->property("height").toReal() > 0.0
            && releaseIdentity != nullptr && releaseIdentity->property("visible").toBool()
            && releaseIdentity->property("codename").toString() == QStringLiteral("此")
            && releaseIdentity->property("verse").toString() == QStringLiteral("天长地久有时尽，此恨绵绵无绝期。")
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
                           << "applicationMatches=" << applicationMatches << "securityMatches=" << securityMatches
                           << "shortcutsMatch=" << shortcutsMatch << "sftpMatches=" << sftpMatches;
    return hostMatches && settingsMatch && applicationMatches && securityMatches && shortcutsMatch && sftpMatches
           && hostCaptured && settingsCaptured && applicationCaptured && securityCaptured && shortcutsCaptured
           && sftpCaptured;
}

[[nodiscard]] bool applyUiLayoutSmokeTheme(ztermy::AppController &controller, const QString &theme,
                                           const QString &language)
{
    return controller.saveApplicationSettings(theme, 1.0, QStringLiteral("acrylic"), QStringLiteral("ztermy"),
                                              QStringLiteral("#22C55E"), {}, QStringLiteral("Cascadia Mono"), 14, false,
                                              true, 1.0, QStringLiteral("terminal"), true, false, true, language, false,
                                              true);
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
    const bool restoredDark = applyUiLayoutSmokeTheme(controller, QStringLiteral("dark"), QStringLiteral("en"));
    return titleBrandPalettePassed && lightCompactPassed && lightRegularPassed && chineseShortcuts && chinesePalette
           && restoredDark;
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
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        QQuickItem *candidate = pending[index];
        if (candidate->objectName() == expectedName)
        {
            return candidate;
        }
        const QList<QQuickItem *> children = candidate->childItems();
        pending.insert(pending.end(), children.cbegin(), children.cend());
    }
    return nullptr;
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
        || !verifyAccessibleButton(rootObject, "settingsSftpCategory", "SFTP settings"))
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

    sendKey(window, Qt::Key_F, Qt::ControlModifier);
    const bool controlFindPreservedForTerminal = !rootObject->property("terminalSearchVisible").toBool()
                                                 && namedFocusItem(window) == QStringLiteral("terminalViewport");
    const QVariantMap unbindFind = controller.setActionShortcut(QStringLiteral("terminal.find"), QString{});
    sendKey(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    const bool unboundFindPreservedForTerminal = unbindFind.value(QStringLiteral("valid")).toBool()
                                                 && !rootObject->property("terminalSearchVisible").toBool()
                                                 && namedFocusItem(window) == QStringLiteral("terminalViewport");
    const bool findShortcutReset = controller.resetActionShortcut(QStringLiteral("terminal.find"));
    sendKey(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    const bool terminalSearchOpened = findShortcutReset && rootObject->property("terminalSearchVisible").toBool()
                                      && namedFocusItem(window) == QStringLiteral("terminalSearchQuery");
    sendKey(window, Qt::Key_Escape);
    const bool terminalSearchKeyboard = controlFindPreservedForTerminal && unboundFindPreservedForTerminal
                                        && terminalSearchOpened
                                        && !rootObject->property("terminalSearchVisible").toBool()
                                        && namedFocusItem(window) == QStringLiteral("terminalViewport");
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
    const bool terminalFindActionClosed = !rootObject->property("terminalSearchVisible").toBool()
                                          && namedFocusItem(window) == QStringLiteral("terminalViewport");
    if (!terminalFindActionOpened || !terminalFindCaptured || !terminalFindActionClosed
        || namedFocusItem(window) != QStringLiteral("terminalViewport"))
    {
        qCWarning(applicationLog) << "Terminal Find action keyboard routing failed"
                                  << "opened=" << terminalFindActionOpened << "focus=" << namedFocusItem(window);
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
                   && namedFocusItem(window) == QStringLiteral("terminalViewport");
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
                   && namedFocusItem(window) == QStringLiteral("terminalViewport");
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
                                                 && namedFocusItem(window) == QStringLiteral("terminalViewport");
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

    QQuickItem *terminalViewport = quickItem(rootObject, "terminalViewport");
    const bool dialogOpened = terminalViewport != nullptr
                              && QMetaObject::invokeMethod(terminalViewport, "multilinePasteConfirmationRequested",
                                                           Qt::DirectConnection, Q_ARG(int, 2));
    processWindowEventsFor(std::chrono::milliseconds{100});
    const bool safeDialogFocus = dialogOpened && namedFocusItem(window) == QStringLiteral("multilinePasteReject");
    sendKey(window, Qt::Key_Right);
    const bool arrowMovedDialogFocus = namedFocusItem(window) == QStringLiteral("multilinePasteAccept");
    sendKey(window, Qt::Key_Escape);
    processWindowEventsFor(std::chrono::milliseconds{180});
    const bool dialogKeyboard =
        safeDialogFocus && arrowMovedDialogFocus && namedFocusItem(window) == QStringLiteral("terminalViewport");
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
    QQuickItem *sftpAction = nullptr;
    const bool sftpActionReady = processWindowEventsUntil(
        [&] {
            sftpAction = visualQuickItem(rootObject, "terminalSftpAction");
            return sftpAction != nullptr && sftpAction->isVisible() && sftpAction->isEnabled();
        },
        std::chrono::seconds{2});
    if (!connected || !sftpActionReady || !focusItem(window, sftpAction, QStringLiteral("terminalSftpAction")))
    {
        qCWarning(applicationLog) << "Real-host UI smoke could not focus the connected terminal SFTP action"
                                  << "connected=" << connected;
        return false;
    }
    sendKey(window, Qt::Key_Return);

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
            return browser->width() >= 520 && moreActionsButton != nullptr && !moreActionsButton->isVisible()
                   && refreshButton != nullptr && refreshButton->isVisible() && newFileButton != nullptr
                   && newFileButton->isVisible();
        },
        std::chrono::seconds{2});
    const bool newFileKeyboard = wideToolbar && focusItem(window, newFileButton, QStringLiteral("sftpNewFileButton"));
    const bool wideCaptured =
        wideToolbar && captureLayout(window, outputDirectory, QStringLiteral("real-host-sftp-wide"));

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

    QQuickItem *closeWorkbenchButton = visualQuickItem(rootObject, "closeTerminalWorkbenchButton");
    if (!pathCopied || !pathBookmarked || !fileListKeyboard || !narrowCaptured || !wideCaptured || !newFileKeyboard
        || !deniedPathPreserved || !deniedPathRecovered
        || !focusItem(window, closeWorkbenchButton, QStringLiteral("closeTerminalWorkbenchButton")))
    {
        qCWarning(applicationLog) << "Real-host SFTP UI contract failed"
                                  << "pathCopied=" << pathCopied << "pathBookmarked=" << pathBookmarked
                                  << "fileListKeyboard=" << fileListKeyboard << "narrowToolbar=" << narrowToolbar
                                  << "wideToolbar=" << wideToolbar << "newFileKeyboard=" << newFileKeyboard
                                  << "deniedPathPreserved=" << deniedPathPreserved
                                  << "deniedPathRecovered=" << deniedPathRecovered;
        return false;
    }
    sendKey(window, Qt::Key_Return);
    const bool workbenchClosed = processWindowEventsUntil(
        [&] {
            return !activeTabState().value(QStringLiteral("workbenchOpen")).toBool()
                   && namedFocusItem(window) == QStringLiteral("terminalViewport");
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
    const bool scrollbarExposed = terminalItem.scrollbarVisible() && terminalItem.scrollbarPageRatio() < 1.0
                                  && terminalItem.scrollbarPosition() > 0.9;
    const auto waitForScrollbarPosition = [&](const auto predicate) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 2000 && !predicate(terminalItem.scrollbarPosition()))
        {
            processWindowEventsFor(std::chrono::milliseconds{20});
        }
        return predicate(terminalItem.scrollbarPosition());
    };
    terminalItem.scrollToFraction(0.0);
    const bool scrollbarReachedHistory = waitForScrollbarPosition([](const qreal position) {
        return position < 0.1;
    });
    terminalItem.scrollToFraction(1.0);
    const bool scrollbarReturnedToBottom = waitForScrollbarPosition([](const qreal position) {
        return position > 0.9;
    });
    const bool scrollbarPassed = scrollbarExposed && scrollbarReachedHistory && scrollbarReturnedToBottom;
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
                           << "terminalRendered=" << terminalRendered << "scrollbarPassed=" << scrollbarPassed
                           << "capture=" << capturePath;
    return completed && responsive && progressiveFrames && resizeCompleted && captureSaved && terminalRendered
           && scrollbarPassed;
}

} // namespace

// Qt framework entry points are exception-opaque. Let unexpected failures reach
// the process crash-diagnostics boundary instead of swallowing them here.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char *argv[])
{
    QQuickWindow::setDefaultAlphaBuffer(true);
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
    ztermy::FontCatalog fontCatalog;
    fontCatalog.applyUiFont(appController.uiFontFamily());
    const bool uiLayoutSmoke = QCoreApplication::arguments().contains(QStringLiteral("--ui-layout-smoke"));
    const bool uiKeyboardSmoke = QCoreApplication::arguments().contains(QStringLiteral("--ui-keyboard-smoke"));
    const bool realHostUiSmoke = QCoreApplication::arguments().contains(QStringLiteral("--real-host-ui-smoke"));
    const bool terminalRenderSmoke = QCoreApplication::arguments().contains(QStringLiteral("--terminal-render-smoke"));
    const bool windowAppearanceSmoke =
        QCoreApplication::arguments().contains(QStringLiteral("--window-appearance-smoke"));
    const bool windowResizeSmoke = QCoreApplication::arguments().contains(QStringLiteral("--window-resize-smoke"));
    const bool windowDpiSmoke = QCoreApplication::arguments().contains(QStringLiteral("--window-dpi-smoke"));
    ztermy::LocalizationManager localizationManager;
    const auto initialLanguage = uiLayoutSmoke || uiKeyboardSmoke || realHostUiSmoke
                                     ? std::optional{ztermy::config::LanguagePreference::english}
                                     : ztermy::config::parseLanguagePreference(appController.languagePreference());
    if (!initialLanguage || !localizationManager.apply(*initialLanguage))
    {
        qCCritical(applicationLog) << "Could not apply the configured UI language";
        return EXIT_FAILURE;
    }
    if ((uiLayoutSmoke || uiKeyboardSmoke || realHostUiSmoke)
        && !applyUiLayoutSmokeTheme(appController, QStringLiteral("dark"), QStringLiteral("en")))
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
    if (!window.load(initialProperties))
    {
        return EXIT_FAILURE;
    }
    QObject::connect(&appController, &ztermy::AppController::applicationSettingsChanged, &window,
                     [&appController, &fontCatalog, &localizationManager, &window] {
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

    auto *terminalItem = window.findChild<ztermy::ui::TerminalItem *>(QStringLiteral("terminalViewport"));
    if (terminalItem == nullptr)
    {
        qCCritical(applicationLog) << "Terminal viewport was not created";
        return EXIT_FAILURE;
    }

    appController.attachTerminal(terminalItem);
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

    window.show();
    const int exitCode = application.exec();

    qCInfo(applicationLog) << "Application event loop stopped; beginning orderly shutdown";
    appController.shutdown();
    window.releaseResources();
    qCInfo(applicationLog) << "Terminal and scene graph resources released";
    return exitCode;
}
