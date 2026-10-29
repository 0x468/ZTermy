#pragma once

#include "platform/windows/NativeWindow.h"
#include "ui/WindowStateRuntimeSmoke.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QTextDocument>

#include <memory>

namespace ztermy::ui
{
inline bool verifyMarkdownDocumentRuntime(NativeWindow &window)
{
    using namespace std::chrono_literals;
    QQmlComponent component(window.engine(), QUrl(QStringLiteral("qrc:/qt/qml/Ztermy/MarkdownMessage.qml")));
    const QString prefix = QStringLiteral("First **bold** line\n\n- item one\n- item two\n");
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{QStringLiteral("streaming"), true}, {QStringLiteral("source"), prefix}}));
    auto *item = qobject_cast<QQuickItem *>(object.get());
    if (!item)
        return false;
    item->setParentItem(window.contentItem());
    item->setZ(1000);
    item->setWidth(440);
    window.resize({800, 800});
    showForRuntimeSmoke(window);
    processWindowEventsFor(100ms);
    auto *text = item->findChild<QQuickItem *>(QStringLiteral("markdownTextDocument"));
    if (!text)
        return false;
    text->forceActiveFocus();
    if (!QMetaObject::invokeMethod(text, "select", Q_ARG(int, 0), Q_ARG(int, 5)))
        return false;
    const auto *document = text->property("textDocument").value<QQuickTextDocument *>();
    if (!document)
        return false;
    // Complete before the 50ms stream timer: the final delta must not be lost.
    item->setProperty("source", prefix + QStringLiteral("\nfinal token"));
    item->setProperty("streaming", false);
    processWindowEventsFor(100ms);
    const bool finalFlushed = document->textDocument()->toPlainText().endsWith(QStringLiteral("final token"));
    const bool selectionKept = text->property("selectedText").toString() == QStringLiteral("First");
    QMetaObject::invokeMethod(text, "deselect");
    const bool reused = item->property("singleTextBlock").toBool() && text->isVisible()
                        && text->property("textDocument").value<QQuickTextDocument *>() == document;
    item->setProperty("source", QStringLiteral("```sh\nprintf sample\n```\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"));
    processWindowEventsFor(100ms);
    const bool richBlocks = !item->property("singleTextBlock").toBool()
                            && !item->property("useStreamingRenderer").toBool() && item->implicitHeight() > 50;
    item->setProperty("textFormat", 0); // TextEdit.PlainText
    item->setProperty("source", QStringLiteral("literal **text** ${value}"));
    processWindowEventsFor(100ms);
    const bool plainText =
        text->isVisible() && document->textDocument()->toPlainText() == QStringLiteral("literal **text** ${value}");
    const bool passed = finalFlushed && selectionKept && reused && richBlocks && plainText;
    qInfo() << "Markdown document reuse" << "finalFlushed=" << finalFlushed << "selection=" << selectionKept
            << "reused=" << reused << "richBlocks=" << richBlocks << "plainText=" << plainText;
    return passed;
}
} // namespace ztermy::ui
