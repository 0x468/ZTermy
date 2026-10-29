#pragma once

#include "platform/windows/NativeWindow.h"
#include "ui/RuntimeSmokeItems.h"
#include "ui/WindowStateRuntimeSmoke.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>

#include <cmath>

namespace ztermy::ui
{
// Exercises the production card, including effective parent/window visibility.
// The control mode pauses only its two animations to establish causality before
// changing the QML lifecycle; it is deliberately not a passing regression gate.
inline bool verifyReleaseCardActivity(NativeWindow &window, const QString &directory, const bool stopControl)
{
    using namespace std::chrono_literals;
    if (!QCoreApplication::arguments().contains(QStringLiteral("--data-dir")))
        return false;
    auto *root = window.rootObject();
    auto *pane = quickItem(root, "settingsPane");
    auto *card = quickItem(root, "settingsReleaseIdentityCard");
    if (!root || !pane || !card)
        return false;
    auto *moon = card->findChild<QObject *>(QStringLiteral("releaseMoonAnimation"));
    auto *wave = card->findChild<QObject *>(QStringLiteral("releaseWaveAnimation"));
    if (!moon || !wave)
        return false;
    window.resize(QSize{1120, 800});
    showForRuntimeSmoke(window);
    window.requestActivate();
    if (!settleWindowUntil(
            [&] {
                return window.isExposed() && window.isSceneGraphInitialized();
            },
            5s))
        return false;
    qint64 frames = 0;
    const auto connection = QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        ++frames;
    });
    const auto disconnect = qScopeGuard([&] {
        QObject::disconnect(connection);
    });
    QJsonArray stages;
    const auto sample = [&](const QString &name) {
        processWindowEventsFor(250ms);
        const double moonBefore = card->property("animatedMoonPhase").toDouble();
        const double waveBefore = card->property("animatedWavePhase").toDouble();
        const qint64 framesBefore = frames;
        processWindowEventsFor(900ms);
        const double moonDelta = std::abs(card->property("animatedMoonPhase").toDouble() - moonBefore);
        const double waveDelta = std::abs(card->property("animatedWavePhase").toDouble() - waveBefore);
        const bool animated = moonDelta > 1e-7 || waveDelta > 1e-7;
        stages.append(QJsonObject{{QStringLiteral("name"), name},
                                  {QStringLiteral("cardVisible"), card->isVisible()},
                                  {QStringLiteral("windowMinimized"), window.visibility() == QWindow::Minimized},
                                  {QStringLiteral("moonDelta"), moonDelta},
                                  {QStringLiteral("waveDelta"), waveDelta},
                                  {QStringLiteral("animated"), animated},
                                  {QStringLiteral("frameSwaps"), frames - framesBefore}});
        return animated;
    };
    root->setProperty("currentPage", QStringLiteral("hosts"));
    pane->setProperty("currentCategory", QStringLiteral("about"));
    const bool hiddenAnimated = sample(QStringLiteral("hidden-page"));
    bool controlStopped = true;
    if (stopControl)
    {
        moon->setProperty("paused", true);
        wave->setProperty("paused", true);
        controlStopped = !sample(QStringLiteral("hidden-card-only-paused"));
        moon->setProperty("paused", false);
        wave->setProperty("paused", false);
    }
    root->setProperty("currentPage", QStringLiteral("settings"));
    const bool visibleAnimated = sample(QStringLiteral("visible-about"));
    const bool visibleCapture = window.grabWindow().save(QDir(directory).filePath(QStringLiteral("release-card.png")));
    pane->setProperty("currentCategory", QStringLiteral("application"));
    const bool categoryAnimated = sample(QStringLiteral("hidden-category"));
    pane->setProperty("currentCategory", QStringLiteral("about"));
    const bool categoryResumed = sample(QStringLiteral("visible-again"));
    window.showMinimized();
    const bool minimizedAnimated = sample(QStringLiteral("minimized"));
    window.showNormal();
    window.requestActivate();
    const bool restoredAnimated = sample(QStringLiteral("restored"));
    root->setProperty("currentPage", QStringLiteral("hosts"));
    const bool pageAnimatedAgain = sample(QStringLiteral("hidden-page-again"));
    const bool hiddenPassed = stopControl
                                  ? hiddenAnimated && categoryAnimated && pageAnimatedAgain
                                  : !hiddenAnimated && !categoryAnimated && !minimizedAnimated && !pageAnimatedAgain;
    const bool passed =
        hiddenPassed && controlStopped && visibleAnimated && categoryResumed && restoredAnimated && visibleCapture;
    const QJsonObject report{{QStringLiteral("controlMode"), stopControl},
                             {QStringLiteral("passed"), passed},
                             {QStringLiteral("stages"), stages}};
    QSaveFile file(QDir(directory).filePath(QStringLiteral("release-card-activity.json")));
    const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const bool saved = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    qInfo() << "Release card activity passed=" << passed << "control=" << stopControl << "saved=" << saved;
    return passed && saved;
}
} // namespace ztermy::ui
