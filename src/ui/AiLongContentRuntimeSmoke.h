#pragma once

#include "application/AppController.h"
#include "application/ai/AiConversationModel.h"
#include "ui/RuntimeSmokeItems.h"

#include <QDir>
#include <QQmlComponent>
#include <QQmlEngine>

#include <memory>

namespace ztermy::ui
{
inline bool verifyAiLongContentRuntime(NativeWindow &window, AppController &controller, const QDir &captures)
{
    // Real model and QML, synthetic presentation controller: no provider, Shell,
    // credentials or network requests are involved in a geometry regression.
    ai::AiConversationModel conversation;
    auto *engine = window.engine();
    QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&conversation, QQmlEngine::CppOwnership);
    auto presentation =
        engine->evaluate(QStringLiteral("({activeAiState:'idle', activeAiConversation:null, aiModel:'layout-fixture',"
                                        "aiProviderPreference:'ollama', aiBaseUrl:'http://127.0.0.1'})"));
    presentation.setPrototype(engine->newQObject(&controller));
    presentation.setProperty(QStringLiteral("activeAiConversation"), engine->newQObject(&conversation));
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/AiAssistantPane.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("controller"), QVariant::fromValue(presentation)},
         {QStringLiteral("activeTab"), QVariantMap{{QStringLiteral("id"), QStringLiteral("layout-fixture")}}}}));
    auto *pane = qobject_cast<QQuickItem *>(object.get());
    if (!pane)
    {
        qWarning() << component.errors();
        return false;
    }
    pane->setParentItem(window.contentItem());
    pane->setZ(1000);
    showForRuntimeSmoke(window);
    const QString command = QStringLiteral("python3 - <<'PY'\n")
                            + QStringLiteral("print('long command fixture with no side effects')\n").repeated(80)
                            + QStringLiteral("PY");
    const QString output = QStringLiteral("output fixture: ") + QString(4000, QChar{'x'})
                           + QStringLiteral("\nnext output row\n").repeated(80);
    const QString prose =
        QStringLiteral("A long answer must wrap without overlapping the tool timeline. ").repeated(24);
    bool passed = captures.mkpath(QStringLiteral("."));
    for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("light")})
    {
        if (!controller.saveApplicationSettings(theme, 1.0, QStringLiteral("acrylic"), QStringLiteral("ztermy"),
                                                QStringLiteral("#22C55E"), {}, QStringLiteral("Cascadia Mono"), 14,
                                                false, true, 1.0, QStringLiteral("terminal"), true, false, false, true,
                                                QStringLiteral("en"), false, true))
            return false;
        const QVariantMap policy = controller.themePolicy();
        if (!controller.saveThemePolicy(QStringLiteral("fixed"), policy.value(theme).toString(),
                                        policy.value(QStringLiteral("light")).toString(),
                                        policy.value(QStringLiteral("dark")).toString()))
            return false;
        if (controller.terminalThemeColors().value(QStringLiteral("dark")).toBool()
            != (theme == QStringLiteral("dark")))
            return false;
        for (const int width : {280, 440, 620})
        {
            window.resize(QSize{800, 840});
            pane->setPosition({20, 20});
            pane->setSize(QSizeF{static_cast<qreal>(width), 800});
            conversation.clear();
            const auto message = conversation.beginAssistantMessage();
            if (!conversation.appendAssistantDelta(message, prose.left(160))
                || !conversation.upsertAssistantToolActivity(message, QStringLiteral("long-tool"),
                                                             QStringLiteral("run_command"), command,
                                                             QStringLiteral("running"), {}, true, false)
                || !conversation.setAssistantToolDetails(message, QStringLiteral("long-tool"), command, output))
                return false;
            for (int index = 0; index < 5; ++index)
            {
                if (!conversation.upsertAssistantToolActivity(
                        message, QStringLiteral("short-tool-%1").arg(index), QStringLiteral("read_terminal_frame"),
                        QStringLiteral("Read current terminal output"), QStringLiteral("running"), {}, false, false))
                    return false;
            }
            processWindowEventsFor(std::chrono::milliseconds{150});
            auto *list = quickItem(pane, "aiConversationList");
            auto *toggle = quickItem(pane, "aiToolActivityToggle");
            auto *summary = quickItem(pane, "aiToolSummary");
            if (!list || !toggle || !summary)
            {
                qWarning() << "AI layout fixture missing items" << list << toggle << summary
                           << "rows=" << conversation.rowCount();
                return false;
            }
            list->setProperty("stickToBottom", false);
            list->setProperty("contentY", 0.0);
            // Keep the group expanded even after all tools finish, like a user
            // inspecting results while the response continues streaming.
            auto *group = quickItem(pane, "aiToolGroupToggle");
            if (!group)
                return false;
            sendMouseClick(window, *group, {group->width() / 2, group->height() / 2});
            sendMouseClick(window, *group, {group->width() / 2, group->height() / 2});
            toggle = quickItem(pane, "aiToolActivityToggle");
            summary = quickItem(pane, "aiToolSummary");
            if (!toggle || !summary)
                return false;
            const QPointF summaryPosition = summary->mapToItem(toggle, QPointF{});
            const bool headerBounded =
                summaryPosition.y() >= -0.5 && summaryPosition.y() + summary->height() <= toggle->height() + 0.5;
            qInfo() << "AI long summary" << theme << width << "height=" << summary->height()
                    << "headerBounded=" << headerBounded;
            passed = headerBounded && passed;
            sendMouseClick(window, *toggle, {toggle->width() / 2, toggle->height() / 2});
            processWindowEventsFor(std::chrono::milliseconds{100});
            auto *detail = quickItem(pane, "aiToolDetailViewport");
            auto *detailText = quickItem(pane, "aiToolDetailText");
            const bool detailBounded = detail && detailText && detail->clip() && detail->height() <= 132.5
                                       && detail->property("contentHeight").toReal() > detail->height()
                                       && detailText->property("text").toString() == command;
            passed = detailBounded && passed;
            if (detailText)
            {
                QMetaObject::invokeMethod(detailText, "selectAll");
                passed = detailText->property("selectedText").toString() == command && passed;
            }
            if (detail)
            {
                detail->setProperty("contentY", 80.0);
                passed = detail->property("contentY").toReal() > 0 && passed;
            }
            if (!conversation.appendAssistantDelta(message, prose.mid(160)))
                return false;
            processWindowEventsFor(std::chrono::milliseconds{150});
            auto *body = quickItem(pane, "aiMessageBody");
            auto *column = quickItem(pane, "aiMessageColumn");
            auto *item = quickItem(pane, "aiMessageItem");
            const auto bodyFits = [&] {
                return body && column && item && body->height() > 100 && body->height() + 0.5 >= body->implicitHeight()
                       && body->y() + body->height() <= column->height() + 0.5
                       && column->height() + 18 <= item->height() + 0.5;
            };
            const bool streamingFits = processWindowEventsUntil(bodyFits, std::chrono::milliseconds{3000});
            for (int index = 0; index < 5; ++index)
            {
                if (!conversation.upsertAssistantToolActivity(
                        message, QStringLiteral("short-tool-%1").arg(index), QStringLiteral("read_terminal_frame"),
                        QStringLiteral("Read current terminal output"), QStringLiteral("succeeded"),
                        QStringLiteral("ok"), false, false))
                    return false;
            }
            if (!conversation.upsertAssistantToolActivity(
                    message, QStringLiteral("long-tool"), QStringLiteral("run_command"), command,
                    QStringLiteral("succeeded"), QStringLiteral("ok"), true, false)
                || !conversation.completeAssistantMessage(message))
                return false;
            processWindowEventsFor(std::chrono::milliseconds{150});
            const bool completedFits = processWindowEventsUntil(bodyFits, std::chrono::milliseconds{3000});
            detail = quickItem(pane, "aiToolDetailViewport");
            const bool expansionRetained = detail && detail->isVisible() && detail->height() <= 132.5;
            list->setProperty("contentY", 0.0);
            processWindowEventsFor(std::chrono::milliseconds{100});
            const bool captured =
                window.grabWindow().save(captures.filePath(QStringLiteral("ai-long-%1-%2.png").arg(theme).arg(width)));
            QMetaObject::invokeMethod(list, "positionViewAtEnd");
            processWindowEventsFor(std::chrono::milliseconds{100});
            const bool answerCaptured = window.grabWindow().save(
                captures.filePath(QStringLiteral("ai-answer-%1-%2.png").arg(theme).arg(width)));
            qInfo() << "AI long content" << theme << width << "detail=" << detailBounded
                    << "streaming=" << streamingFits << "complete=" << completedFits
                    << "expandedAfterUpdate=" << expansionRetained << "capture=" << captured;
            if (!streamingFits || !completedFits)
                qWarning() << "AI body geometry" << (body ? body->height() : -1) << (body ? body->implicitHeight() : -1)
                           << (column ? column->height() : -1) << (item ? item->height() : -1);
            passed = streamingFits && completedFits && expansionRetained && captured && answerCaptured && passed;
        }
    }
    return passed;
}
} // namespace ztermy::ui
