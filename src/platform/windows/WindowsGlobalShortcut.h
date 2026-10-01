#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <expected>

namespace ztermy::windowing
{
struct GlobalShortcutKey final
{
    unsigned int modifiers = 0;
    unsigned int virtualKey = 0;
    QString portableText;
};

// RegisterHotKey, not a keyboard hook: ordinary terminal input stays untouched.
class WindowsGlobalShortcut final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit WindowsGlobalShortcut(QWindow *window);
    ~WindowsGlobalShortcut() override;
    [[nodiscard]] static std::expected<GlobalShortcutKey, QString> parse(const QString &text);
    [[nodiscard]] QString configure(const QString &text);
    [[nodiscard]] QString shortcut() const { return m_shortcut; }
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
signals:
    void activated();

private:
    QPointer<QWindow> m_window;
    QString m_shortcut;
    int m_registeredId = 0;
};
} // namespace ztermy::windowing
