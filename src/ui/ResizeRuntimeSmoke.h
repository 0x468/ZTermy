#pragma once

#include "ui/RuntimeSmokeItems.h"

#include <QQmlComponent>
#include <memory>

namespace ztermy::ui
{
inline bool verifyResizeInteractions(NativeWindow &window, AppController &)
{
    window.resize(1120, 800);
    window.show();
    window.hide();
    window.show();
    window.requestActivate();
    const auto send = [&window](QPointF point, Qt::MouseButtons buttons, Qt::MouseButton button, QEvent::Type type) {
        qt_handleMouseEvent(&window, point, window.mapToGlobal(point.toPoint()), buttons, button, type, Qt::NoModifier,
                            static_cast<int>(GetTickCount()));
        processWindowEventsFor(std::chrono::milliseconds{60});
    };
    const auto doubleClick = [&](QQuickItem &item) {
        const QPointF point = item.mapToScene({item.width() / 2, item.height() / 2});
        send(point, Qt::NoButton, Qt::NoButton, QEvent::MouseMove);
        send(point, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        qInfo() << "Resize press target" << item.objectName() << "pressed=" << item.property("pressed")
                << "grabber=" << window.mouseGrabberItem();
        send(point, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        // The QPA entry point accepts native presses, not a synthesized double-click event.
        // Let Qt derive the double click from two presses at the same position.
        send(point, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        send(point, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        processWindowEventsFor(std::chrono::milliseconds{250});
    };
    auto *root = window.rootObject();
    QMetaObject::invokeMethod(root, "resizeWorkspaceNavigation", Q_ARG(QVariant, 280));
    processWindowEventsFor(std::chrono::milliseconds{350});
    settleWindowLayout(window);
    auto *navigation = quickItem(root, "workspaceNavigationResizeHandle");
    if (!navigation)
        return false;
    qInfo() << "Resize navigation geometry" << navigation->mapToScene({0, 0}) << navigation->size()
            << root->property("workspaceNavigationWidth");
    doubleClick(*navigation);
    bool passed = qAbs(root->property("workspaceNavigationWidth").toReal() - 208) < 0.5;
    qInfo() << "Resize navigation double click passed=" << passed;

    QQmlComponent gripComponent(window.engine());
    gripComponent.setData("import Ztermy\nResizeGrip { onValueEdited: next => value = next }", QUrl{});
    for (const int mode : {0, 1, 2})
    {
        const bool horizontal = mode != 2;
        const qreal direction = mode == 0 ? 1 : -1;
        const qreal defaultSize = horizontal ? 520 : 132;
        std::unique_ptr<QObject> object(
            gripComponent.createWithInitialProperties({{QStringLiteral("value"), defaultSize + 60},
                                                       {QStringLiteral("minimum"), horizontal ? 320 : 92},
                                                       {QStringLiteral("maximum"), horizontal ? 800 : 360},
                                                       {QStringLiteral("defaultValue"), defaultSize},
                                                       {QStringLiteral("horizontal"), horizontal},
                                                       {QStringLiteral("direction"), direction}}));
        auto *grip = qobject_cast<QQuickItem *>(object.get());
        if (!grip)
        {
            qWarning() << gripComponent.errors();
            return false;
        }
        grip->setParentItem(window.contentItem());
        grip->setPosition({400, 150});
        grip->setSize(horizontal ? QSizeF{8, 300} : QSizeF{300, 8});
        grip->setZ(100);
        processWindowEventsFor(std::chrono::milliseconds{100});
        doubleClick(*grip);
        bool valid = qAbs(grip->property("value").toReal() - defaultSize) < 0.5;
        grip->setProperty("value", defaultSize + 60);
        const QPointF start = grip->mapToScene({grip->width() / 2, grip->height() / 2});
        const auto pointFor = [&](qreal value) {
            const qreal delta = (value - defaultSize - 60) * direction;
            return start + (horizontal ? QPointF{delta, 0} : QPointF{0, delta});
        };
        send(start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        send(pointFor(defaultSize + 6), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        valid = qAbs(grip->property("value").toReal() - defaultSize) < 0.5 && valid;
        send(pointFor(defaultSize + 12), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        valid = qAbs(grip->property("value").toReal() - defaultSize) < 0.5 && valid;
        send(pointFor(defaultSize + 20), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        valid = qAbs(grip->property("value").toReal() - defaultSize - 20) < 0.5 && valid;
        send(pointFor(2000), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        valid = qAbs(grip->property("value").toReal() - grip->property("maximum").toReal()) < 0.5 && valid;
        send(pointFor(0), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        valid = qAbs(grip->property("value").toReal() - grip->property("minimum").toReal()) < 0.5 && valid;
        send(pointFor(0), Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        qInfo() << "Resize grip reset/snap/release/bounds mode=" << mode << "passed=" << valid;
        passed = valid && passed;
    }

    QQmlComponent splitComponent(window.engine());
    splitComponent.setData(R"qml(import QtQuick
import QtQuick.Controls
import Ztermy
AppSplitView {
    id: split
    leadingPane: first
    trailingPane: second
    ratio: 0.65
    readonly property real minimumValue: first.SplitView.minimumWidth
    readonly property real minimumHeightValue: first.SplitView.minimumHeight
    onRatioEdited: next => ratio = next
    Rectangle { id: first; SplitView.minimumWidth: 100; SplitView.minimumHeight: 100 }
    Rectangle {
        id: second
        SplitView.minimumWidth: 100; SplitView.minimumHeight: 100
        SplitView.fillWidth: split.orientation === Qt.Horizontal
        SplitView.fillHeight: split.orientation === Qt.Vertical
    }
})qml",
                           QUrl{});
    for (const bool horizontal : {true, false})
    {
        std::unique_ptr<QObject> object(splitComponent.create());
        auto *split = qobject_cast<QQuickItem *>(object.get());
        if (!split)
        {
            qWarning() << splitComponent.errors();
            return false;
        }
        split->setParentItem(window.contentItem());
        split->setPosition({20, 70});
        split->setSize({960, 640});
        split->setZ(100);
        split->setProperty("orientation", horizontal ? Qt::Horizontal : Qt::Vertical);
        settleWindowLayout(window);
        auto *handle = visualQuickItem(split, "splitResizeHandle");
        if (!handle)
            return false;
        auto *leading = split->property("leadingPane").value<QQuickItem *>();
        if (!leading)
            return false;
        doubleClick(*leading);
        const bool untouchedOutsideHandle = qAbs(split->property("ratio").toReal() - 0.65) < 0.001;
        doubleClick(*handle);
        bool valid = untouchedOutsideHandle && qAbs(split->property("ratio").toReal() - 0.5) < 0.001;
        qInfo() << "Native split double click horizontal=" << horizontal << "ratio=" << split->property("ratio")
                << "passed=" << valid;
        split->setProperty("ratio", 0.65);
        settleWindowLayout(window);
        const qreal span = split->property("availableSpan").toReal();
        const QPointF start = handle->mapToScene({handle->width() / 2, handle->height() / 2});
        const qreal delta = span * (0.5 - 0.65) + 4;
        const QPointF target = start + (horizontal ? QPointF{delta, 0} : QPointF{0, delta});
        send(start, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        send((start + target) / 2, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        send(target, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        settleWindowLayout(window);
        valid = split->property("resizing").toBool() && qAbs(split->property("leadingSize").toReal() - span / 2) < 1
                && valid;
        const QPointF away = target + (horizontal ? QPointF{16, 0} : QPointF{0, 16});
        send(away, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        valid = qAbs(split->property("leadingSize").toReal() - span / 2 - 20) < 1 && valid;
        send(target, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        send(target, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        valid = qAbs(split->property("ratio").toReal() - 0.5) < 0.002 && valid;
        valid = qAbs(split->property("minimumValue").toReal() - 100) < 0.5
                && qAbs(split->property("minimumHeightValue").toReal() - 100) < 0.5 && valid;
        const QPointF secondStart = handle->mapToScene({handle->width() / 2, handle->height() / 2});
        const QPointF secondEnd = secondStart + (horizontal ? QPointF{span * 0.28, 0} : QPointF{0, span * 0.28});
        send(secondStart, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
        send(secondEnd, Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
        send(secondEnd, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
        valid = qAbs(split->property("ratio").toReal() - 0.78) < 0.002 && valid;
        qInfo() << "Native split reset/live snap horizontal=" << horizontal << "ratio=" << split->property("ratio")
                << "passed=" << valid;
        passed = valid && passed;
    }
    return passed;
}
} // namespace ztermy::ui
