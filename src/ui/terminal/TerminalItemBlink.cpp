#include "ui/terminal/TerminalItem.h"

#include <QQuickWindow>
#include <algorithm>
#include <functional>
#include <utility>

namespace ztermy::ui
{
class TerminalTextBlink final : public QObject
{
public:
    TerminalTextBlink(QQuickItem *item, std::function<void()> repaint)
        : QObject(item), m_item(item), m_repaint(std::move(repaint))
    {
        m_timer.setInterval(530);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            visible = !visible;
            m_repaint();
        });
        connect(item, &QQuickItem::visibleChanged, this, [this] {
            refresh();
        });
        connect(item, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
            attach(window);
        });
        attach(item->window());
    }

    void refresh()
    {
        const auto *window = m_item->window();
        const bool running = enabled && hasInk && m_item->isVisible() && window && window->isVisible()
                             && window->visibility() != QWindow::Minimized;
        if (running)
        {
            if (!m_timer.isActive())
                m_timer.start();
        }
        else
        {
            m_timer.stop();
            if (!visible)
            {
                visible = true;
                m_repaint();
            }
        }
    }

    bool enabled = true;
    bool hasInk = false;
    bool visible = true;

private:
    void attach(QQuickWindow *window)
    {
        disconnect(m_windowConnection);
        if (window)
            m_windowConnection = connect(window, &QWindow::visibilityChanged, this, [this] {
                refresh();
            });
        refresh();
    }

    QQuickItem *m_item;
    std::function<void()> m_repaint;
    QTimer m_timer;
    QMetaObject::Connection m_windowConnection;
};

void TerminalItem::initializeBlinkTimers()
{
    m_textBlink = new TerminalTextBlink(this, [this] {
        invalidateRenderer(false);
    });
    m_cursorBlinkTimer.setInterval(530);
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this] {
        if (!isVisible() || !hasActiveFocus() || !m_snapshot || !m_snapshot->cursor.visible)
            return;
        m_cursorBlinkPhase = !m_cursorBlinkPhase;
        m_renderMetrics.recordCursorInvalidation();
        invalidateRenderer(false);
    });
    m_cursorBlinkTimer.start();
}

void TerminalItem::refreshTextBlink()
{
    m_textBlink->hasInk = m_snapshot && std::ranges::any_of(m_snapshot->cells, [](const auto &cell) {
                              return cell.blink && !cell.invisible && !cell.selected && !cell.grapheme.empty();
                          });
    m_textBlink->refresh();
}

bool TerminalItem::textBlinkEnabled() const
{
    return m_textBlink->enabled;
}
bool TerminalItem::textBlinkVisible() const
{
    return m_textBlink->visible;
}

void TerminalItem::setTextBlinkEnabled(bool enabled)
{
    if (m_textBlink->enabled == enabled)
        return;
    m_textBlink->enabled = enabled;
    m_textBlink->refresh();
    emit textBlinkEnabledChanged();
}

void TerminalItem::showSelectionAction(const QPointF &position, bool preferBelow)
{
    m_selectionActionPosition = position;
    m_selectionActionPreferBelow = preferBelow;
    m_selectionActionVisible = true;
    emit selectionActionChanged();
}
} // namespace ztermy::ui
