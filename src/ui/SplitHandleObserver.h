#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

namespace ztermy::ui
{
// Observes before SplitView's internal child-event filter; never consumes input.
class SplitHandleObserver : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit SplitHandleObserver(QQuickItem *parent = nullptr);

signals:
    void pointerPressed(QPointF scenePosition);
    void pointerMoved(QPointF scenePosition);
    void resetRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QQuickWindow> m_observedWindow;
    bool m_tracking = false;
};
} // namespace ztermy::ui
