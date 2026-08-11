#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>

namespace ztermy::windowing
{

class WindowsProtectedClipboard final : public QObject
{
    Q_OBJECT

public:
    explicit WindowsProtectedClipboard(QObject *parent = nullptr);
    ~WindowsProtectedClipboard() override;

    [[nodiscard]] bool setProtectedText(const QString &text,
                                        std::chrono::milliseconds autoClearAfter = std::chrono::milliseconds::zero());

private slots:
    void clearIfUnchanged();

private:
    QTimer m_clearTimer;
    std::uint32_t m_expectedSequence = 0;
    void *m_ownerWindow = nullptr;
};

} // namespace ztermy::windowing
