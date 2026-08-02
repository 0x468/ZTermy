#pragma once

#include "domain/terminal/TerminalOutputSink.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <thread>

namespace ztermy::logging
{

enum class SessionLogState : std::uint8_t
{
    Idle,
    Starting,
    Active,
    Failed,
};

class SessionLogWriter final : public QObject, public terminal::TerminalOutputSink
{
    Q_OBJECT

public:
    explicit SessionLogWriter(QObject *parent = nullptr);
    ~SessionLogWriter() override;

    SessionLogWriter(const SessionLogWriter &) = delete;
    SessionLogWriter &operator=(const SessionLogWriter &) = delete;

    [[nodiscard]] bool start(const QString &path);
    void stop() noexcept;
    void append(std::span<const std::byte> bytes) noexcept override;

    [[nodiscard]] SessionLogState state() const noexcept;
    [[nodiscard]] QString path() const;
    [[nodiscard]] QString errorString() const;
    [[nodiscard]] std::uint64_t droppedBytes() const noexcept;

signals:
    void stateChanged();

private:
    void run(QString path, const std::stop_token &stopToken) noexcept;
    void publishState(SessionLogState state, QString error = {});

    static constexpr std::size_t maximumQueuedBytes = std::size_t{4} * 1024 * 1024;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_available;
    std::deque<QByteArray> m_queue;
    std::size_t m_queuedBytes = 0;
    QString m_path;
    QString m_error;
    std::jthread m_worker;
    std::atomic<SessionLogState> m_state = SessionLogState::Idle;
    std::atomic_uint64_t m_droppedBytes = 0;
};

} // namespace ztermy::logging
