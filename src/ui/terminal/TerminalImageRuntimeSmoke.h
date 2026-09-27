#pragma once

#include "application/AppController.h"
#include "ui/WindowStateRuntimeSmoke.h"
#include "ui/terminal/TerminalImagePainter.h"
#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QFileInfo>
#include <QQuickItemGrabResult>

#include <array>
#include <string>

namespace ztermy::ui
{
// Isolate conversion cost from shell I/O and texture upload. All placements
// reference the same raster; increasing their count must not change the pixels.
inline bool measureSharedImagePainting()
{
    auto image = std::make_shared<terminal::TerminalImage>();
    image->width = image->height = 1024;
    image->format = terminal::TerminalImageFormat::grayAlpha;
    image->pixels.resize(std::size_t{1024} * 1024 * 2, 255);
    std::vector<terminal::TerminalImagePlacement> placements(32);
    for (std::size_t index = 0; index < placements.size(); ++index)
    {
        placements[index] = {.image = image,
                             .column = static_cast<std::int32_t>(index % 8),
                             .row = static_cast<std::int32_t>(index / 8),
                             .width = 32,
                             .height = 32,
                             .sourceWidth = 1024,
                             .sourceHeight = 1024};
    }
    QImage target(256, 128, QImage::Format_ARGB32_Premultiplied);
    for (const std::size_t count : {std::size_t{1}, std::size_t{32}})
    {
        QElapsedTimer elapsed;
        elapsed.start();
        for (int frame = 0; frame < 5; ++frame)
        {
            target.fill(Qt::transparent);
            QPainter painter(&target);
            paintTerminalImages(painter, std::span{placements}.first(count), TerminalImageLayer::aboveText,
                                QSizeF{32, 32}, {}, QRectF{0, 0, 256, 128});
        }
        qInfo() << "Shared gray-alpha image paint: placements=" << count
                << "frames=5 elapsedMs=" << static_cast<double>(elapsed.nsecsElapsed()) / 1'000'000.0;
        if (target.pixelColor(16, 16) != QColor(Qt::white)
            || (count == 32 && target.pixelColor(240, 112) != QColor(Qt::white)))
            return false;
    }
    // A different raster between two reused placements must replace the view;
    // equal dimensions are not image identity.
    auto black = std::make_shared<terminal::TerminalImage>(*image);
    for (std::size_t offset = 0; offset < black->pixels.size(); offset += 2)
        black->pixels[offset] = 0;
    placements[1].image = black;
    target.fill(Qt::transparent);
    {
        QPainter painter(&target);
        paintTerminalImages(painter, placements, TerminalImageLayer::aboveText, QSizeF{32, 32}, {},
                            QRectF{0, 0, 256, 128});
    }
    if (target.pixelColor(16, 16) != QColor(Qt::white) || target.pixelColor(48, 16) != QColor(Qt::black)
        || target.pixelColor(80, 16) != QColor(Qt::white))
        return false;
    return true;
}

// Exercises the shell -> ConPTY -> worker -> viewport path, not direct snapshots.
[[nodiscard]] inline bool runTerminalImageRuntimeSmoke(NativeWindow &window, AppController &controller,
                                                       const QString &outputDirectory)
{
    if (!measureSharedImagePainting())
        return false;
    window.resize(QSize{1120, 800});
    showForRuntimeSmoke(window);
    // A hidden CLI launch can consume the first ShowWindow request.
    window.hide();
    showForRuntimeSmoke(window);
    if (!settleWindowUntil(
            [&] {
                return window.isExposed();
            },
            std::chrono::seconds{10}))
        return false;
    const auto id = controller.startLocalTerminal();
    if (id.isEmpty())
        return false;
    if (auto *root = window.rootObject())
        root->setProperty("currentPage", QStringLiteral("terminal"));
    if (!settleWindowUntil(
            [&] {
                for (const auto &value : controller.terminalTabs())
                {
                    const auto tab = value.toMap();
                    if (tab.value(QStringLiteral("id")).toString() == id)
                        return tab.value(QStringLiteral("running")).toBool();
                }
                return false;
            },
            std::chrono::seconds{10}))
        return false;
    processWindowEventsFor(std::chrono::milliseconds{500});
    auto *item = window.findChild<TerminalItem *>();
    if (!item)
        return false;
    QByteArray protocol("\x1b[2J\x1b[H\x1bP0;1q\"1;1;120;96#1;2;100;0;0");
    for (int band = 0; band < 16; ++band)
        protocol += band == 0 ? "!120~" : "-!120~";
    protocol += "\x1b\\\x1b[2;25H\x1b_Ga=T,f=24,s=32,v=32,i=991,C=1;";
    QByteArray blue;
    for (int pixel = 0; pixel < 32 * 32; ++pixel)
        blue.append("\0\0\xff", 3);
    protocol += blue.toBase64();
    protocol += "\x1b\\";
    protocol += "\x1b_Ga=T,f=24,s=1,v=1,i=992,U=1,c=10,r=4,q=2;AP8A\x1b\\\x1b[38;2;0;3;224m";
    constexpr std::array<char32_t, 4> rowMarks{U'\u0305', U'\u030d', U'\u030e', U'\u0310'};
    for (int row = 0; row < 4; ++row)
    {
        protocol += "\x1b[" + QByteArray::number(12 + row) + ";40H";
        std::u32string placeholders{U'\U0010eeee', rowMarks[static_cast<std::size_t>(row)]};
        placeholders.append(9, U'\U0010eeee');
        protocol += QString::fromStdU32String(placeholders).toUtf8();
    }
    protocol += "\x1b[0m";
    QByteArray command = "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);"
                         "[Console]::Write([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"
                         + protocol.toBase64() + "')))\r";
    // Optional independent encoder workload, confined to this explicit smoke
    // entry point. Ordinary checks retain their dependency-free fixture.
    const auto externalScript = QDir(outputDirectory).filePath(QStringLiteral("external-images.ps1"));
    if (QFileInfo::exists(externalScript))
    {
        auto quotedPath = externalScript;
        quotedPath.replace(QLatin1Char('\''), QStringLiteral("''"));
        command = "& '" + quotedPath.toUtf8() + "'\r";
        qInfo() << "Terminal image runtime check uses external encoder script";
    }
    item->inputGenerated(command);

    // Longer external workloads opt in explicitly so the ordinary screenshot
    // check cannot terminate them after seeing only their first image.
    if (QFileInfo::exists(QDir(outputDirectory).filePath(QStringLiteral("external-images.wait"))))
    {
        const bool suppressPainting =
            QFileInfo::exists(QDir(outputDirectory).filePath(QStringLiteral("external-images.no-render")));
        if (suppressPainting)
            item->setVisible(false);
        QElapsedTimer heartbeat;
        heartbeat.start();
        qint64 maximumGap = 0;
        QTimer timer;
        timer.setInterval(8);
        timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
            maximumGap = std::max(maximumGap, heartbeat.restart());
        });
        timer.start();
        const auto done = QDir(outputDirectory).filePath(QStringLiteral("external-images.done"));
        const bool extended =
            QFileInfo::exists(QDir(outputDirectory).filePath(QStringLiteral("external-images.extended")));
        if (!settleWindowUntil(
                [&] {
                    return QFileInfo::exists(done);
                },
                std::chrono::seconds{extended ? 180 : 45}))
            return false;
        qInfo() << "External image workload completed; maximum GUI heartbeat gap ms=" << maximumGap;
        if (suppressPainting)
        {
            qInfo() << "External image workload viewport remained hidden=" << !item->isVisible();
            item->setVisible(true);
        }
    }

    const QString savedEffects = controller.effectsTier();
    for (const QString &tier : {QStringLiteral("full"), QStringLiteral("reduced"), QStringLiteral("off")})
    {
        if (!controller.saveEffectsTier(tier))
            return false;
        processWindowEventsFor(std::chrono::milliseconds{100});
        const bool expectedBlink = tier == QStringLiteral("full") && window.animationsEnabled();
        if (item->textBlinkEnabled() != expectedBlink)
            return false;
        QImage frame;
        qsizetype redPixels = 0, bluePixels = 0, greenPixels = 0;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10'000)
        {
            processWindowEventsFor(std::chrono::milliseconds{200});
            const auto grab = item->grabToImage();
            if (!grab)
                continue;
            if (grab->image().isNull())
            {
                QEventLoop loop;
                QObject::connect(grab.data(), &QQuickItemGrabResult::ready, &loop, &QEventLoop::quit);
                QTimer::singleShot(std::chrono::seconds{2}, &loop, &QEventLoop::quit);
                loop.exec();
            }
            frame = grab->image();
            redPixels = bluePixels = greenPixels = 0;
            for (int y = 0; y < frame.height(); ++y)
                for (int x = 0; x < frame.width(); ++x)
                {
                    const auto color = frame.pixelColor(x, y);
                    redPixels += color.red() > 245 && color.green() < 10 && color.blue() < 10 ? 1 : 0;
                    bluePixels += color.blue() > 245 && color.red() < 10 && color.green() < 10 ? 1 : 0;
                    greenPixels += color.green() > 245 && color.red() < 10 && color.blue() < 10 ? 1 : 0;
                }
            if (redPixels > 5'000 && bluePixels > 800 && greenPixels > 2'000)
                break;
        }
        const bool captured =
            !frame.isNull()
            && frame.save(QDir(outputDirectory).filePath(QStringLiteral("terminal-images-%1.png").arg(tier)));
        qInfo() << "Terminal image runtime check" << "sixelRedPixels=" << redPixels << "kittyBluePixels=" << bluePixels
                << "unicodeGreenPixels=" << greenPixels << "effects=" << tier
                << "performanceMode=" << window.performanceModeActive() << "textBlink=" << item->textBlinkEnabled();
        const auto geometry = item->currentTerminalGeometry();
        qInfo() << "Image check geometry: cell=" << geometry.cellWidthPixels << geometry.cellHeightPixels
                << "item=" << item->size() << "capture=" << frame.size();
        if (!captured || redPixels <= 5'000 || bluePixels <= 800 || greenPixels <= 2'000)
            return false;
    }
    return controller.saveEffectsTier(savedEffects);
}
} // namespace ztermy::ui
