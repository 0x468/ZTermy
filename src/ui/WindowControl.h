#pragma once

#include <QObject>
#include <QVariantMap>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

namespace ztermy::ui
{
// QML entry point for window state changes. Main and detached windows share
// this one policy instead of calling QWindow::show*() from QML.
// Not final: QML_ELEMENT registration derives QQmlPrivate::QQmlElement from it.
class WindowControl : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit WindowControl(QObject *parent = nullptr);

    Q_INVOKABLE void minimize(QWindow *window) const;
    Q_INVOKABLE void toggleMaximize(QWindow *window) const;
    Q_INVOKABLE void reveal(QWindow *window) const;
    Q_INVOKABLE void present(QWindow *window) const;
    Q_INVOKABLE bool acceptsDropAt(QWindow *window, QPointF localPosition, QWindow *movingWindow) const;
    Q_INVOKABLE QVariantMap placement(QWindow *window, const QVariantMap &previous) const;
    Q_INVOKABLE QVariantMap restorePlacement(QWindow *window, const QVariantMap &saved) const;
};
} // namespace ztermy::ui
