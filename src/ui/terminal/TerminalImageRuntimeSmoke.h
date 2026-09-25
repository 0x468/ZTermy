#pragma once

#include "application/AppController.h"
#include "ui/WindowStateRuntimeSmoke.h"
#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QQuickItemGrabResult>

#include <array>
#include <string>

namespace ztermy::ui
{
// Exercises the shell -> ConPTY -> worker -> viewport path, not direct snapshots.
[[nodiscard]] inline bool runTerminalImageRuntimeSmoke(NativeWindow &window, AppController &controller,
                                                       const QString &outputDirectory)
{
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
    const QByteArray command = "[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);"
                               "[Console]::Write([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"
                               + protocol.toBase64() + "')))\r";
    item->inputGenerated(command);

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
        !frame.isNull() && frame.save(QDir(outputDirectory).filePath(QStringLiteral("terminal-images.png")));
    qInfo() << "Terminal image runtime check" << "sixelRedPixels=" << redPixels << "kittyBluePixels=" << bluePixels
            << "unicodeGreenPixels=" << greenPixels;
    return captured && redPixels > 5'000 && bluePixels > 800 && greenPixels > 2'000;
}
} // namespace ztermy::ui
