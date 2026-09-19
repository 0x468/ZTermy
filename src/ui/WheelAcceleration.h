#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <algorithm>

namespace ztermy::ui
{
// Shared by settings scrolling and terminal scrollback, never terminal mouse reporting.
class WheelAcceleration : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit WheelAcceleration(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE int scale(int steps, quint64 timestamp)
    {
        if (steps == 0)
            return 0;
        const int direction = steps > 0 ? 1 : -1;
        const bool continuing =
            m_timestamp != 0 && timestamp > m_timestamp && timestamp - m_timestamp <= 100 && direction == m_direction;
        m_burst = continuing ? std::min(m_burst + 1, 6) : 0;
        m_timestamp = timestamp;
        m_direction = direction;
        return steps * (1 + m_burst / 2);
    }

private:
    quint64 m_timestamp = 0;
    int m_direction = 0;
    int m_burst = 0;
};
} // namespace ztermy::ui
