#pragma once

#include "application/AppController.h"
#include "platform/windows/NativeWindow.h"
#include "ui/RuntimeSmokeItems.h"

#include <QCoreApplication>
#include <QDir>
#include <QScreen>

namespace ztermy::ui
{
// Exercise the actual detached QML surface with a synthetic snapshot, never a process or SSH connection.
inline bool verifyDetachedWindowSurface(NativeWindow &window, AppController &controller)
{
    const auto arguments = QCoreApplication::arguments();
    const qsizetype dataArgument = arguments.indexOf(QStringLiteral("--data-dir"));
    if (dataArgument < 0 || dataArgument + 1 >= arguments.size() || !controller.terminalTabs().isEmpty())
        return false;
    QDir captures(QDir(arguments[dataArgument + 1]).filePath(QStringLiteral("captures")));
    if (!captures.mkpath(QStringLiteral(".")))
        return false;
    auto *root = window.rootObject();
    auto *detached = root ? root->findChild<QQuickWindow *>(QStringLiteral("detachedTerminalWindow")) : nullptr;
    if (!detached)
        return false;
    QQuickWindow backdrop;
    backdrop.setFlags(Qt::Window | Qt::FramelessWindowHint);
    backdrop.setColor(QColor(QStringLiteral("#404040")));
    backdrop.setGeometry(window.screen()->geometry());
    backdrop.show();
    window.setGeometry(180, 180, 600, 400);
    window.show();
    window.hide();
    window.show();
    const QVariantMap tab{{QStringLiteral("kind"), QStringLiteral("local")},
                          {QStringLiteral("title"), QStringLiteral("Material fixture")},
                          {QStringLiteral("running"), true},
                          {QStringLiteral("localExited"), false},
                          {QStringLiteral("sessionBackgroundOpacity"), 0.5}};
    const QVariantMap leaf{{QStringLiteral("kind"), QStringLiteral("leaf")},
                           {QStringLiteral("id"), QStringLiteral("material-fixture")},
                           {QStringLiteral("active"), true},
                           {QStringLiteral("tab"), tab}};
    root->setProperty("detachedTerminalWorkspace", QVariantMap{{QStringLiteral("root"), leaf}});
    root->setProperty("detachedTerminalPaneId", QStringLiteral("material-fixture"));
    bool passed = true;
    for (const auto *material : {"acrylic", "solid"})
    {
        passed = window.applyAppearance(QString::fromLatin1(material), true) && passed;
        detached->setWindowState(Qt::WindowNoState);
        detached->setGeometry(100, 100, 560, 380);
        passed = window.configureDetachedWindow(detached) && passed;
        detached->show();
        detached->raise();
        detached->requestActivate();
        processWindowEventsFor(std::chrono::milliseconds{400});
        auto *viewport =
            qobject_cast<TerminalItem *>(visualQuickItem(detached->contentItem(), "terminalViewport-material-fixture"));
        if (!viewport)
        {
            passed = false;
            break;
        }
        auto snapshot = std::make_shared<terminal::TerminalSnapshot>();
        snapshot->columns = 4;
        snapshot->rows = 2;
        snapshot->cells.resize(8);
        snapshot->defaultBackground = {.red = 12, .green = 20, .blue = 28};
        snapshot->cursor.visible = false;
        viewport->setSnapshot(snapshot);
        for (const auto *phase : {"normal", "maximized", "parent-hidden", "restored"})
        {
            if (QLatin1StringView{phase} == "maximized")
                detached->showMaximized();
            else if (QLatin1StringView{phase} == "parent-hidden")
                window.hide();
            else if (QLatin1StringView{phase} == "restored")
            {
                detached->showNormal();
                detached->setGeometry(320, 120, 760, 520);
            }
            processWindowEventsFor(std::chrono::milliseconds{600});
            const QImage raw = detached->grabWindow();
            const QPoint clientPosition = detached->mapToGlobal(QPoint{}) - detached->screen()->geometry().topLeft();
            const QImage composited =
                detached->screen()
                    ->grabWindow(0, clientPosition.x(), clientPosition.y(), detached->width(), detached->height())
                    .toImage();
            const QString name = QStringLiteral("%1-%2").arg(QLatin1StringView{material}, QLatin1StringView{phase});
            bool uniform = !raw.isNull();
            const QColor reference = raw.isNull() ? QColor{} : raw.pixelColor(raw.width() / 2, raw.height() / 2);
            for (const qreal x : {0.15, 0.5, 0.85})
                for (const qreal y : {0.35, 0.6, 0.85})
                    if (!raw.isNull())
                        uniform =
                            raw.pixelColor(qRound(raw.width() * x), qRound(raw.height() * y)) == reference && uniform;
            const bool saved = raw.save(captures.filePath(name + QStringLiteral("-scene.png")))
                               && composited.save(captures.filePath(name + QStringLiteral("-desktop.png")));
            bool uniformDesktop = !composited.isNull();
            const bool checkDesktop =
                QLatin1StringView{phase} == "parent-hidden" || QLatin1StringView{phase} == "restored";
            if (checkDesktop && !composited.isNull())
            {
                const QColor center = composited.pixelColor(composited.width() / 2, composited.height() / 2);
                for (const qreal x : {0.15, 0.5, 0.85})
                    for (const qreal y : {0.15, 0.5, 0.85})
                    {
                        const QColor pixel =
                            composited.pixelColor(qRound(composited.width() * x), qRound(composited.height() * y));
                        uniformDesktop = qAbs(pixel.red() - center.red()) <= 2
                                         && qAbs(pixel.green() - center.green()) <= 2
                                         && qAbs(pixel.blue() - center.blue()) <= 2 && uniformDesktop;
                    }
            }
            qInfo() << "Detached material probe" << name << "size=" << detached->size()
                    << "dpr=" << detached->devicePixelRatio() << "uniformScene=" << uniform
                    << "sceneColor=" << reference << "saved=" << saved
                    << "alphaBits=" << detached->format().alphaBufferSize() << "uniformDesktop=" << uniformDesktop
                    << "desktopCheckActive=" << checkDesktop
                    << "independent=" << (detached->transientParent() == nullptr);
            passed = uniform && uniformDesktop && saved && detached->transientParent() == nullptr
                     && detached->format().alphaBufferSize() == 8 && passed;
        }
        window.show();
        detached->hide();
    }
    root->setProperty("detachedTerminalPaneId", QString{});
    root->setProperty("detachedTerminalWorkspace", QVariantMap{});
    detached->hide();
    return passed;
}
} // namespace ztermy::ui
