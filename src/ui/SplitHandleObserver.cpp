#include "ui/SplitHandleObserver.h"

#include <QMouseEvent>

namespace ztermy::ui
{
SplitHandleObserver::SplitHandleObserver(QQuickItem *parent) : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *next) {
        if (m_observedWindow)
            m_observedWindow->removeEventFilter(this);
        m_observedWindow = next;
        m_tracking = false;
        if (m_observedWindow)
            m_observedWindow->installEventFilter(this);
    });
}

bool SplitHandleObserver::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_observedWindow || !isVisible() || !isEnabled())
    {
        m_tracking = false;
        return false;
    }
    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            m_tracking = mouse->button() == Qt::LeftButton && contains(mapFromScene(mouse->position()));
            if (m_tracking)
                emit pointerPressed(mouse->position());
            break;
        }
        case QEvent::MouseMove:
            if (m_tracking)
                emit pointerMoved(static_cast<QMouseEvent *>(event)->position());
            break;
        case QEvent::MouseButtonDblClick:
            if (m_tracking && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
                emit resetRequested();
            break;
        case QEvent::MouseButtonRelease:
        case QEvent::WindowDeactivate:
        case QEvent::Hide:
            m_tracking = false;
            break;
        default:
            break;
    }
    return false;
}
} // namespace ztermy::ui
