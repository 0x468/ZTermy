#include "application/terminal/LocalTerminalSession.h"

#include "domain/terminal/GhosttyTerminalEngine.h"
#include "infrastructure/terminal/ConPtyProcess.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QStandardPaths>

#include <Windows.h>

#include <array>
#include <chrono>
#include <exception>
#include <span>
#include <utility>

Q_LOGGING_CATEGORY(terminalSessionLog, "ztermy.terminal.session")

namespace ztermy::terminal
{

LocalTerminalSession::LocalTerminalSession(QObject *parent) : QObject(parent) {}

LocalTerminalSession::~LocalTerminalSession()
{
    stop();
}

std::error_code LocalTerminalSession::start(const TerminalGeometry geometry)
{
    stop();
    if (!geometry.valid())
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    auto engineResult = GhosttyTerminalEngine::create(geometry);
    if (!engineResult)
    {
        return engineResult.error();
    }

    auto process = std::make_unique<ConPtyProcess>();
    const std::wstring workingDirectory = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdWString();
    std::error_code processError =
        process->start(L"pwsh.exe -NoLogo", {.columns = geometry.columns, .rows = geometry.rows}, workingDirectory);
    if (processError == std::make_error_code(std::errc::no_such_file_or_directory)
        || processError.value() == ERROR_FILE_NOT_FOUND)
    {
        processError = process->start(L"powershell.exe -NoLogo", {.columns = geometry.columns, .rows = geometry.rows},
                                      workingDirectory);
    }
    if (processError)
    {
        return processError;
    }

    m_engine = std::move(*engineResult);
    m_process = std::move(process);
    m_running.store(true);
    emit runningChanged(true);
    emit statusChanged(QStringLiteral("Local PowerShell connected"));
    publishSnapshot();

    m_readThread = std::jthread([this](const std::stop_token &token) {
        try
        {
            readLoop(token);
        }
        catch (const std::exception &exception)
        {
            postStatus(QStringLiteral("Terminal read worker failed: %1").arg(QString::fromUtf8(exception.what())));
        }
        catch (...)
        {
            postStatus(QStringLiteral("Terminal read worker failed with an unknown error"));
        }
    });
    m_writeThread = std::jthread([this](const std::stop_token &token) {
        try
        {
            writeLoop(token);
        }
        catch (const std::exception &exception)
        {
            postStatus(QStringLiteral("Terminal write worker failed: %1").arg(QString::fromUtf8(exception.what())));
        }
        catch (...)
        {
            postStatus(QStringLiteral("Terminal write worker failed with an unknown error"));
        }
    });
    qCInfo(terminalSessionLog) << "Local terminal session started";
    return {};
}

void LocalTerminalSession::stop() noexcept
{
    m_writeThread.request_stop();
    m_readThread.request_stop();
    m_commandAvailable.notify_all();

    if (m_readThread.joinable())
    {
        const auto threadHandle = static_cast<HANDLE>(m_readThread.native_handle());
        CancelSynchronousIo(threadHandle);
    }
    if (m_writeThread.joinable())
    {
        m_writeThread.join();
    }
    if (m_readThread.joinable())
    {
        m_readThread.join();
    }

    if (m_process)
    {
        m_process->close();
    }
    m_process.reset();
    m_engine.reset();

    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.clear();
        m_queuedInputBytes = 0;
    }
    {
        std::scoped_lock lock(m_snapshotMutex);
        m_pendingSnapshot.reset();
    }
    m_snapshotDeliveryScheduled.store(false);

    if (m_running.exchange(false))
    {
        emit runningChanged(false);
        emit statusChanged(QStringLiteral("Local terminal stopped"));
        qCInfo(terminalSessionLog) << "Local terminal session stopped";
    }
}

void LocalTerminalSession::queueInput(const QByteArray &bytes)
{
    if (bytes.isEmpty() || !m_running.load())
    {
        return;
    }
    queueByteCommand(InputCommand{.bytes = bytes}, static_cast<std::size_t>(bytes.size()));
}

void LocalTerminalSession::queuePaste(const QByteArray &bytes)
{
    if (bytes.isEmpty() || !m_running.load())
    {
        return;
    }
    queueByteCommand(PasteCommand{.bytes = bytes}, static_cast<std::size_t>(bytes.size()));
}

void LocalTerminalSession::queueByteCommand(Command command, const std::size_t byteCount)
{
    {
        std::scoped_lock lock(m_commandMutex);
        if (byteCount > maximumQueuedInputBytes - std::min(m_queuedInputBytes, maximumQueuedInputBytes))
        {
            qCWarning(terminalSessionLog) << "Terminal input queue is full; input batch dropped";
            return;
        }
        m_queuedInputBytes += byteCount;
        m_commands.emplace_back(std::move(command));
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                         const quint32 cellHeightPixels)
{
    const TerminalGeometry geometry{.columns = columns,
                                    .rows = rows,
                                    .cellWidthPixels = cellWidthPixels,
                                    .cellHeightPixels = cellHeightPixels};
    if (!geometry.valid() || !m_running.load())
    {
        return;
    }

    {
        std::scoped_lock lock(m_commandMutex);
        if (!m_commands.empty() && std::holds_alternative<TerminalGeometry>(m_commands.back()))
        {
            m_commands.back() = geometry;
        }
        else
        {
            m_commands.emplace_back(geometry);
        }
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::requestScroll(const int rows)
{
    if (rows == 0 || !m_running.load())
    {
        return;
    }

    {
        std::scoped_lock lock(m_commandMutex);
        if (!m_commands.empty())
        {
            if (auto *pending = std::get_if<ScrollCommand>(&m_commands.back()))
            {
                pending->rows += rows;
            }
            else
            {
                m_commands.emplace_back(ScrollCommand{.rows = rows});
            }
        }
        else
        {
            m_commands.emplace_back(ScrollCommand{.rows = rows});
        }
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::requestSelection(const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                            const quint16 endRow, const bool rectangular)
{
    if (!m_running.load())
    {
        return;
    }

    SelectionCommand command{.selection = TerminalSelection{.start = {.column = startColumn, .row = startRow},
                                                            .end = {.column = endColumn, .row = endRow},
                                                            .rectangular = rectangular}};
    {
        std::scoped_lock lock(m_commandMutex);
        if (!m_commands.empty() && std::holds_alternative<SelectionCommand>(m_commands.back()))
        {
            m_commands.back() = command;
        }
        else
        {
            m_commands.emplace_back(command);
        }
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::clearSelection()
{
    if (!m_running.load())
    {
        return;
    }
    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.emplace_back(SelectionCommand{});
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::copySelection()
{
    if (!m_running.load())
    {
        return;
    }
    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.emplace_back(CopyCommand{});
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::readLoop(const std::stop_token &stopToken)
{
    std::array<std::byte, std::size_t{64} * 1024U> buffer{};
    while (!stopToken.stop_requested())
    {
        const auto readResult = m_process->read(buffer);
        if (!readResult)
        {
            if (!stopToken.stop_requested())
            {
                postStatus(QStringLiteral("Terminal read failed: %1")
                               .arg(QString::fromStdString(readResult.error().message())));
            }
            break;
        }
        if (*readResult == 0)
        {
            break;
        }

        {
            std::scoped_lock lock(m_engineMutex);
            const std::error_code feedError = m_engine->feed(std::span(buffer).first(*readResult));
            if (feedError)
            {
                postStatus(
                    QStringLiteral("Terminal parser failed: %1").arg(QString::fromStdString(feedError.message())));
                break;
            }
        }
        publishSnapshot();
    }

    if (!stopToken.stop_requested())
    {
        postStatus(QStringLiteral("Local shell exited"));
    }
}

void LocalTerminalSession::writeLoop(const std::stop_token &stopToken)
{
    while (!stopToken.stop_requested())
    {
        Command command;
        {
            std::unique_lock lock(m_commandMutex);
            if (!m_commandAvailable.wait(lock, stopToken, [this] {
                    return !m_commands.empty();
                }))
            {
                break;
            }
            command = std::move(m_commands.front());
            m_commands.pop_front();
            if (const auto *input = std::get_if<InputCommand>(&command))
            {
                m_queuedInputBytes -= static_cast<std::size_t>(input->bytes.size());
            }
            else if (const auto *paste = std::get_if<PasteCommand>(&command))
            {
                m_queuedInputBytes -= static_cast<std::size_t>(paste->bytes.size());
            }
        }

        if (const auto *input = std::get_if<InputCommand>(&command))
        {
            std::error_code selectionError;
            {
                std::scoped_lock lock(m_engineMutex);
                selectionError = m_engine->setSelection(std::nullopt);
                m_engine->scrollToBottom();
            }
            if (selectionError)
            {
                postStatus(QStringLiteral("Terminal selection clear failed: %1")
                               .arg(QString::fromStdString(selectionError.message())));
            }
            publishSnapshot();

            const auto bytes =
                std::as_bytes(std::span(input->bytes.constData(), static_cast<std::size_t>(input->bytes.size())));
            if (const std::error_code writeError = m_process->write(bytes))
            {
                postStatus(
                    QStringLiteral("Terminal write failed: %1").arg(QString::fromStdString(writeError.message())));
                break;
            }
            continue;
        }

        if (const auto *paste = std::get_if<PasteCommand>(&command))
        {
            std::expected<std::vector<std::byte>, std::error_code> encoded;
            std::error_code selectionError;
            {
                std::scoped_lock lock(m_engineMutex);
                selectionError = m_engine->setSelection(std::nullopt);
                m_engine->scrollToBottom();
                const auto bytes =
                    std::as_bytes(std::span(paste->bytes.constData(), static_cast<std::size_t>(paste->bytes.size())));
                encoded = m_engine->encodePaste(bytes);
            }
            if (selectionError)
            {
                postStatus(QStringLiteral("Terminal selection clear failed: %1")
                               .arg(QString::fromStdString(selectionError.message())));
            }
            if (!encoded)
            {
                postStatus(
                    QStringLiteral("Terminal paste failed: %1").arg(QString::fromStdString(encoded.error().message())));
                continue;
            }
            publishSnapshot();
            if (const std::error_code writeError = m_process->write(*encoded))
            {
                postStatus(
                    QStringLiteral("Terminal write failed: %1").arg(QString::fromStdString(writeError.message())));
                break;
            }
            continue;
        }

        if (const auto *scroll = std::get_if<ScrollCommand>(&command))
        {
            {
                std::scoped_lock lock(m_engineMutex);
                m_engine->scrollViewport(scroll->rows);
            }
            publishSnapshot();
            continue;
        }

        if (const auto *selection = std::get_if<SelectionCommand>(&command))
        {
            std::error_code selectionError;
            {
                std::scoped_lock lock(m_engineMutex);
                selectionError = m_engine->setSelection(selection->selection);
            }
            if (selectionError)
            {
                postStatus(QStringLiteral("Terminal selection failed: %1")
                               .arg(QString::fromStdString(selectionError.message())));
                continue;
            }
            publishSnapshot();
            continue;
        }

        if (std::holds_alternative<CopyCommand>(command))
        {
            std::expected<std::optional<std::string>, std::error_code> selectedText;
            {
                std::scoped_lock lock(m_engineMutex);
                selectedText = m_engine->selectedText();
            }
            if (!selectedText)
            {
                postStatus(QStringLiteral("Terminal copy failed: %1")
                               .arg(QString::fromStdString(selectedText.error().message())));
            }
            else if (*selectedText)
            {
                emit clipboardTextReady(
                    QString::fromUtf8((*selectedText)->data(), static_cast<qsizetype>((*selectedText)->size())));
            }
            continue;
        }

        const auto geometry = std::get<TerminalGeometry>(command);
        if (const std::error_code resizeError = m_process->resize({.columns = geometry.columns, .rows = geometry.rows}))
        {
            postStatus(QStringLiteral("Terminal resize failed: %1").arg(QString::fromStdString(resizeError.message())));
            continue;
        }
        {
            std::scoped_lock lock(m_engineMutex);
            if (const std::error_code resizeError = m_engine->resize(geometry))
            {
                postStatus(QStringLiteral("Terminal state resize failed: %1")
                               .arg(QString::fromStdString(resizeError.message())));
                continue;
            }
        }
        publishSnapshot();
    }
}

void LocalTerminalSession::publishSnapshot()
{
    TerminalSnapshotPtr snapshot;
    {
        std::scoped_lock engineLock(m_engineMutex);
        auto snapshotResult = m_engine->snapshot();
        if (!snapshotResult)
        {
            postStatus(QStringLiteral("Terminal snapshot failed: %1")
                           .arg(QString::fromStdString(snapshotResult.error().message())));
            return;
        }
        snapshot = std::make_shared<const TerminalSnapshot>(std::move(*snapshotResult));
    }
    {
        std::scoped_lock snapshotLock(m_snapshotMutex);
        m_pendingSnapshot = std::move(snapshot);
    }

    if (!m_snapshotDeliveryScheduled.exchange(true))
    {
        (void)QMetaObject::invokeMethod(this, "deliverLatestSnapshot", Qt::QueuedConnection);
    }
}

void LocalTerminalSession::deliverLatestSnapshot()
{
    TerminalSnapshotPtr snapshot;
    {
        std::scoped_lock lock(m_snapshotMutex);
        snapshot = std::move(m_pendingSnapshot);
    }
    m_snapshotDeliveryScheduled.store(false);
    if (snapshot)
    {
        emit snapshotReady(std::move(snapshot));
    }

    {
        std::scoped_lock lock(m_snapshotMutex);
        if (!m_pendingSnapshot || m_snapshotDeliveryScheduled.exchange(true))
        {
            return;
        }
    }
    (void)QMetaObject::invokeMethod(this, "deliverLatestSnapshot", Qt::QueuedConnection);
}

void LocalTerminalSession::postStatus(const QString &status)
{
    emit statusChanged(status);
}

} // namespace ztermy::terminal
