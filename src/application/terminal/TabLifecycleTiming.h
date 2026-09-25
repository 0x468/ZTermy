#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(appControllerLog)

namespace ztermy
{
class TabLifecycleTiming final
{
public:
    explicit TabLifecycleTiming(const char *operation)
        : m_operation(operation), m_enabled(qEnvironmentVariableIntValue("ZTERMY_TAB_TIMING") > 0)
    {
        if (m_enabled)
        {
            m_timer.start();
        }
    }

    void mark(const char *stage)
    {
        if (!m_enabled)
        {
            return;
        }
        const qint64 elapsed = m_timer.elapsed();
        qCInfo(appControllerLog) << "Terminal tab timing" << "operation=" << m_operation << "stage=" << stage
                                 << "stageMs=" << elapsed - m_previousElapsed << "elapsedMs=" << elapsed;
        m_previousElapsed = elapsed;
    }

private:
    const char *m_operation;
    bool m_enabled = false;
    QElapsedTimer m_timer;
    qint64 m_previousElapsed = 0;
};
} // namespace ztermy
