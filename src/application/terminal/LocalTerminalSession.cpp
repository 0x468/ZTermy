#include "application/terminal/LocalTerminalSession.h"

#include "application/terminal/PowerShellShellIntegration.h"

#include "domain/terminal/GhosttyTerminalEngine.h"
#include "infrastructure/terminal/ConPtyProcess.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QStandardPaths>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <span>
#include <utility>

Q_LOGGING_CATEGORY(terminalSessionLog, "ztermy.terminal.session")

namespace ztermy::terminal
{

namespace
{

[[nodiscard]] std::wstring quotedExecutable(const QString &path)
{
    std::wstring result = L"\"";
    result.append(path.toStdWString());
    result.push_back(L'\"');
    return result;
}

[[nodiscard]] std::wstring launchCommand(const LocalTerminalLaunchSpec &spec, const std::string_view nonce)
{
    const std::wstring executable = quotedExecutable(spec.executable);
    if (spec.powerShellIntegration)
    {
        return powerShellLaunchCommand(executable, nonce).value_or(executable + L" -NoLogo");
    }
    std::wstring command = executable;
    for (const QString &argument : spec.arguments)
    {
        command.push_back(L' ');
        command.append(argument.toStdWString());
    }
    return command;
}

} // namespace

LocalTerminalSession::LocalTerminalSession(QObject *parent) : LocalTerminalSessionBackend(parent)
{
    m_snapshotDeliveryTimer.setInterval(8);
    m_snapshotDeliveryTimer.setSingleShot(true);
    QObject::connect(&m_snapshotDeliveryTimer, &QTimer::timeout, this, &LocalTerminalSession::deliverLatestSnapshot);
    QObject::connect(this, &LocalTerminalSession::processExitObserved, this, &LocalTerminalSession::postProcessExited,
                     Qt::QueuedConnection);
}

LocalTerminalSession::~LocalTerminalSession()
{
    stop();
}

diagnostics::LatencySummary LocalTerminalSession::inputQueueLatencySummary() const noexcept
{
    return m_inputQueueLatency.summary();
}

diagnostics::LatencySummary LocalTerminalSession::takeInputQueueLatencySummary() noexcept
{
    return m_inputQueueLatencyWindow.takeSummary();
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
    LocalTerminalLaunchSpec launchSpec = m_launchSpec;
    if (launchSpec.executable.isEmpty())
    {
        launchSpec.executable = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
        launchSpec.displayName = tr("PowerShell 7");
        if (launchSpec.executable.isEmpty())
        {
            launchSpec.executable = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
            launchSpec.displayName = tr("Windows PowerShell");
        }
    }
    if (launchSpec.executable.isEmpty())
    {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }
    const QString workingDirectory = launchSpec.workingDirectory.isEmpty()
                                         ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                                         : launchSpec.workingDirectory;
    const std::error_code processError =
        process->start(launchSpec.executable.toStdWString(), launchCommand(launchSpec, m_shellIntegrationNonce),
                       {.columns = geometry.columns, .rows = geometry.rows}, workingDirectory.toStdWString());
    if (processError)
    {
        return processError;
    }

    m_engine = std::move(*engineResult);
    if (m_colorScheme)
    {
        if (const std::error_code error = m_engine->setColorScheme(*m_colorScheme))
        {
            qCWarning(terminalSessionLog) << "Terminal color scheme was not applied:" << error.message();
        }
    }
    m_process = std::move(process);
    m_synchronizedOutputStartedNanoseconds.store(0, std::memory_order_release);
    resetMetrics();
    m_running.store(true);
    emit runningChanged(true);
    emit statusChanged(tr("Local %1 connected").arg(launchSpec.displayName));
    publishSnapshot();

    const auto guardedWorker = [this](void (LocalTerminalSession::*loop)(const std::stop_token &),
                                      const QString &failure, const QString &unknownFailure) {
        return [this, loop, failure, unknownFailure](const std::stop_token &token) {
            try
            {
                (this->*loop)(token);
            }
            catch (const std::exception &exception)
            {
                postStatus(failure.arg(QString::fromUtf8(exception.what())));
            }
            catch (...)
            {
                postStatus(unknownFailure);
            }
        };
    };
    m_readThread = std::jthread(guardedWorker(&LocalTerminalSession::readLoop, tr("Terminal read worker failed: %1"),
                                              tr("Terminal read worker failed with an unknown error")));
    m_writeThread = std::jthread(guardedWorker(&LocalTerminalSession::writeLoop, tr("Terminal write worker failed: %1"),
                                               tr("Terminal write worker failed with an unknown error")));
    m_exitThread = std::jthread([this](const std::stop_token &token) {
        monitorProcessExit(token);
    });
    qCInfo(terminalSessionLog) << "Local terminal session started";
    return {};
}

void LocalTerminalSession::requestStop()
{
    if (m_stopThread.joinable())
        return;
    m_snapshotDeliveryTimer.stop();
    m_stopFinished.store(false);
    m_stopThread = std::jthread([this] {
        stopWorkers();
        m_stopFinished.store(true);
    });
}

void LocalTerminalSession::stopWorkers() noexcept
{
    m_exitThread.request_stop();
    m_writeThread.request_stop();
    m_readThread.request_stop();
    m_commandAvailable.notify_all();

    for (std::jthread *worker : {&m_writeThread, &m_readThread})
    {
        if (!worker->joinable())
            continue;
        const auto handle = static_cast<HANDLE>(worker->native_handle());
        // Cancellation can race the next ReadFile/WriteFile. Repeat until the
        // worker exits so a successful stop cannot leave a newly blocked I/O.
        while (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT)
        {
            CancelSynchronousIo(handle);
            WaitForSingleObject(handle, 10);
        }
        worker->join();
    }
    if (m_exitThread.joinable())
    {
        m_exitThread.join();
    }

    if (m_process)
    {
        m_process->close();
    }
    m_process.reset();
    m_engine.reset();
}

void LocalTerminalSession::stop() noexcept
{
    if (m_stopThread.joinable())
        m_stopThread.join();
    else
        stopWorkers();
    m_stopFinished.store(true);

    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.clear();
        m_queuedInputBytes = 0;
    }
    {
        std::scoped_lock lock(m_snapshotMutex);
        m_pendingSnapshot.reset();
    }
    m_snapshotDeliveryTimer.stop();
    m_snapshotDeliveryScheduled.store(false);
    m_engineDirty.store(false);
    m_synchronizedOutputStartedNanoseconds.store(0, std::memory_order_release);

    if (m_running.exchange(false))
    {
        logMetrics();
        emit runningChanged(false);
        emit statusChanged(tr("Local terminal stopped"));
        qCInfo(terminalSessionLog) << "Local terminal session stopped";
    }
}

void LocalTerminalSession::setOutputSink(const std::shared_ptr<TerminalOutputSink> &sink)
{
    m_outputSink = sink;
}

void LocalTerminalSession::setShellIntegrationNonce(const std::string &nonce)
{
    if (m_running.load())
    {
        return;
    }
    m_shellIntegrationNonce = nonce;
}

void LocalTerminalSession::setLaunchSpec(const LocalTerminalLaunchSpec &spec)
{
    if (!m_running.load())
    {
        m_launchSpec = spec;
    }
}

void LocalTerminalSession::queueInput(const QByteArray &bytes)
{
    if (bytes.isEmpty() || !m_running.load())
    {
        return;
    }
    queueByteCommand(InputCommand{.bytes = bytes, .enqueuedAt = std::chrono::steady_clock::now()},
                     static_cast<std::size_t>(bytes.size()));
}

void LocalTerminalSession::queueKeyEvent(const TerminalKeyEvent &event)
{
    if (!m_running.load())
    {
        return;
    }
    {
        std::scoped_lock lock(m_commandMutex);
        if (m_commands.size() >= maximumQueuedEvents)
        {
            qCWarning(terminalSessionLog) << "Terminal event queue is full; key event dropped";
            return;
        }
        m_commands.emplace_back(KeyCommand{.event = event, .enqueuedAt = std::chrono::steady_clock::now()});
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::queueMouseEvent(const TerminalMouseEvent &event)
{
    if (!m_running.load())
    {
        return;
    }
    {
        std::scoped_lock lock(m_commandMutex);
        auto *pending = m_commands.empty() ? nullptr : std::get_if<MouseCommand>(&m_commands.back());
        if (pending != nullptr && pending->event.action == TerminalMouseAction::motion
            && event.action == TerminalMouseAction::motion)
        {
            pending->event = event;
        }
        else if (m_commands.size() < maximumQueuedEvents)
        {
            m_commands.emplace_back(MouseCommand{.event = event});
        }
        else
        {
            qCWarning(terminalSessionLog) << "Terminal event queue is full; mouse event dropped";
            return;
        }
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::queueFocusEvent(const bool focused)
{
    if (!m_running.load())
    {
        return;
    }
    {
        std::scoped_lock lock(m_commandMutex);
        if (auto *pending = m_commands.empty() ? nullptr : std::get_if<FocusCommand>(&m_commands.back()))
        {
            pending->focused = focused;
        }
        else if (m_commands.size() < maximumQueuedEvents)
        {
            m_commands.emplace_back(FocusCommand{.focused = focused});
        }
    }
    m_commandAvailable.notify_one();
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

void LocalTerminalSession::requestSelectionGesture(const TerminalSelectionGesture &gesture)
{
    if (!m_running.load())
    {
        return;
    }

    SelectionGestureCommand command{.gesture = gesture};
    {
        std::scoped_lock lock(m_commandMutex);
        auto *pending = m_commands.empty() ? nullptr : std::get_if<SelectionGestureCommand>(&m_commands.back());
        if (pending != nullptr && pending->gesture.type == gesture.type
            && (gesture.type == TerminalSelectionGestureType::drag
                || gesture.type == TerminalSelectionGestureType::autoscrollTick))
        {
            if (gesture.type == TerminalSelectionGestureType::autoscrollTick)
            {
                const bool sameDirection = pending->gesture.scrollRows != 0 && gesture.scrollRows != 0
                                           && ((pending->gesture.scrollRows > 0) == (gesture.scrollRows > 0));
                if (sameDirection)
                {
                    command.gesture.scrollRows = std::clamp(pending->gesture.scrollRows + gesture.scrollRows, -64, 64);
                }
            }
            *pending = std::move(command);
        }
        else
        {
            m_commands.emplace_back(std::move(command));
        }
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::requestCopyModeAction(const TerminalCopyModeAction &action)
{
    queueCommand(CopyModeCommand{.action = action});
}

void LocalTerminalSession::setColorScheme(const ztermy::terminal::TerminalColorScheme &scheme)
{
    if (m_colorScheme == scheme)
    {
        return;
    }
    m_colorScheme = scheme;
    queueCommand(ColorSchemeCommand{.scheme = scheme});
}

void LocalTerminalSession::queueCommand(Command command)
{
    if (!m_running.load())
    {
        return;
    }
    {
        std::scoped_lock lock(m_commandMutex);
        m_commands.emplace_back(std::move(command));
    }
    m_commandAvailable.notify_one();
}

void LocalTerminalSession::selectAll()
{
    queueCommand(SelectAllCommand{});
}

void LocalTerminalSession::clearSelection()
{
    queueCommand(SelectionCommand{});
}

void LocalTerminalSession::copySelection()
{
    queueCommand(CopyCommand{});
}

void LocalTerminalSession::requestSelectedText()
{
    queueCommand(SelectedTextCommand{});
}

void LocalTerminalSession::search(const QString &query, const bool backwards, const bool caseSensitive)
{
    queueCommand(SearchCommand{
        .query = query.toUtf8(),
        .direction = backwards ? TerminalSearchDirection::backward : TerminalSearchDirection::forward,
        .caseSensitive = caseSensitive,
    });
}

std::expected<ztermy::terminal::TerminalScrollbackPage, std::error_code>
LocalTerminalSession::scrollbackPage(const ztermy::terminal::TerminalScrollbackRequest request) const
{
    if (!m_running.load() || m_engine == nullptr)
    {
        return std::unexpected(std::make_error_code(std::errc::not_connected));
    }
    // The ghostty terminal synchronizes internally; a query from the caller's
    // thread is safe against the worker-thread feed path.
    return m_engine->scrollbackPage(request);
}

void LocalTerminalSession::clearSearch()
{
    queueCommand(ClearSearchCommand{});
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
                postStatus(tr("Terminal read failed: %1").arg(QString::fromStdString(readResult.error().message())));
            }
            break;
        }
        if (*readResult == 0)
        {
            break;
        }
        if (!consumeOutput(std::span(buffer).first(*readResult)))
        {
            break;
        }
    }

    if (!stopToken.stop_requested())
    {
        // The write worker may already be gone; build the exit frame here.
        if (m_engineDirty.load(std::memory_order_acquire)
            && !m_snapshotBuildActive.exchange(true, std::memory_order_acq_rel))
        {
            buildSnapshot(true);
            m_snapshotBuildActive.store(false, std::memory_order_release);
        }
        emit processExitObserved();
    }
}

bool LocalTerminalSession::consumeOutput(const std::span<const std::byte> bytes)
{
    m_readBytes.fetch_add(bytes.size(), std::memory_order_relaxed);
    if (m_outputSink)
        m_outputSink->append(bytes);

    std::vector<std::byte> ptyWrite;
    std::optional<std::string> clipboardWrite;
    std::int64_t synchronizedOutputStarted = 0;
    {
        std::scoped_lock lock(m_engineMutex);
        if (const auto error = m_engine->feed(bytes))
        {
            postStatus(tr("Terminal parser failed: %1").arg(QString::fromStdString(error.message())));
            return false;
        }
        ptyWrite = m_engine->takePtyWrite();
        clipboardWrite = m_engine->takeClipboardWrite();
        if (m_engine->synchronizedOutput())
        {
            if (m_synchronizedOutputStartedNanoseconds.load(std::memory_order_relaxed) == 0)
            {
                synchronizedOutputStarted = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count();
                m_synchronizedOutputStartedNanoseconds.store(synchronizedOutputStarted, std::memory_order_release);
            }
        }
        else
            m_synchronizedOutputStartedNanoseconds.store(0, std::memory_order_release);
    }
    if (synchronizedOutputStarted != 0)
        scheduleSynchronizedOutputFallback(synchronizedOutputStarted);
    if (!ptyWrite.empty() && !writeToProcess(ptyWrite))
        return false;
    if (clipboardWrite)
        emit clipboardTextReady(QString::fromUtf8(*clipboardWrite));
    publishSnapshot(false);
    return true;
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

        if (std::holds_alternative<SnapshotRequestCommand>(command))
        {
            publishSnapshotIfDirty();
            continue;
        }

        if (const auto *input = std::get_if<InputCommand>(&command))
        {
            const auto latency = std::chrono::steady_clock::now() - input->enqueuedAt;
            m_inputQueueLatency.record(latency);
            m_inputQueueLatencyWindow.record(latency);
            std::error_code selectionError;
            {
                std::scoped_lock lock(m_engineMutex);
                selectionError = m_engine->setSelection(std::nullopt);
                m_engine->scrollToBottom();
            }
            if (selectionError)
            {
                postStatus(
                    tr("Terminal selection clear failed: %1").arg(QString::fromStdString(selectionError.message())));
            }
            // Write to the PTY first so the snapshot build never delays the keystroke.
            if (!writeToProcess(
                    std::as_bytes(std::span(input->bytes.constData(), static_cast<std::size_t>(input->bytes.size())))))
            {
                break;
            }
            publishSnapshot();
            continue;
        }

        if (const auto *key = std::get_if<KeyCommand>(&command))
        {
            m_inputQueueLatency.record(std::chrono::steady_clock::now() - key->enqueuedAt);
            m_inputQueueLatencyWindow.record(std::chrono::steady_clock::now() - key->enqueuedAt);
            std::expected<std::vector<std::byte>, std::error_code> encoded;
            std::error_code selectionError;
            {
                std::scoped_lock lock(m_engineMutex);
                encoded = m_engine->encodeKey(key->event);
                if (key->event.action != TerminalKeyAction::release && encoded && !encoded->empty())
                {
                    selectionError = m_engine->setSelection(std::nullopt);
                    m_engine->scrollToBottom();
                }
            }
            if (!encoded)
            {
                postStatus(
                    tr("Terminal key encoding failed: %1").arg(QString::fromStdString(encoded.error().message())));
                continue;
            }
            if (selectionError)
            {
                postStatus(
                    tr("Terminal selection clear failed: %1").arg(QString::fromStdString(selectionError.message())));
            }
            if (encoded->empty())
            {
                continue;
            }
            if (!writeToProcess(*encoded))
            {
                break;
            }
            publishSnapshot();
            continue;
        }

        if (const auto *mouse = std::get_if<MouseCommand>(&command))
        {
            std::expected<std::vector<std::byte>, std::error_code> encoded;
            {
                std::scoped_lock lock(m_engineMutex);
                encoded = m_engine->encodeMouse(mouse->event);
            }
            if (!encoded)
            {
                postStatus(
                    tr("Terminal mouse encoding failed: %1").arg(QString::fromStdString(encoded.error().message())));
                continue;
            }
            if (!encoded->empty() && !writeToProcess(*encoded))
            {
                break;
            }
            continue;
        }

        if (const auto *focus = std::get_if<FocusCommand>(&command))
        {
            std::expected<std::vector<std::byte>, std::error_code> encoded;
            {
                std::scoped_lock lock(m_engineMutex);
                encoded = m_engine->encodeFocus(focus->focused);
            }
            if (!encoded)
            {
                postStatus(
                    tr("Terminal focus encoding failed: %1").arg(QString::fromStdString(encoded.error().message())));
                continue;
            }
            if (!encoded->empty() && !writeToProcess(*encoded))
            {
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
                postStatus(
                    tr("Terminal selection clear failed: %1").arg(QString::fromStdString(selectionError.message())));
            }
            if (!encoded)
            {
                postStatus(tr("Terminal paste failed: %1").arg(QString::fromStdString(encoded.error().message())));
                continue;
            }
            if (!writeToProcess(*encoded))
            {
                break;
            }
            publishSnapshot();
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
                postStatus(tr("Terminal selection failed: %1").arg(QString::fromStdString(selectionError.message())));
                continue;
            }
            publishSnapshot();
            continue;
        }

        if (std::holds_alternative<SelectionGestureCommand>(command)
            || std::holds_alternative<CopyModeCommand>(command))
        {
            const auto *gesture = std::get_if<SelectionGestureCommand>(&command);
            std::expected<bool, std::error_code> changed;
            {
                std::scoped_lock lock(m_engineMutex);
                changed = gesture ? m_engine->applySelectionGesture(gesture->gesture)
                                  : m_engine->applyCopyModeAction(std::get<CopyModeCommand>(command).action);
            }
            if (!changed)
            {
                postStatus((gesture ? tr("Terminal selection gesture failed: %1") : tr("Terminal Copy Mode failed: %1"))
                               .arg(QString::fromStdString(changed.error().message())));
                continue;
            }
            if (*changed)
            {
                publishSnapshot();
            }
            continue;
        }

        if (std::holds_alternative<SelectAllCommand>(command))
        {
            std::error_code selectionError;
            {
                std::scoped_lock lock(m_engineMutex);
                selectionError = m_engine->selectAll();
            }
            if (selectionError)
            {
                postStatus(tr("Terminal select all failed: %1").arg(QString::fromStdString(selectionError.message())));
                continue;
            }
            publishSnapshot();
            continue;
        }

        if (std::holds_alternative<CopyCommand>(command) || std::holds_alternative<SelectedTextCommand>(command))
        {
            const bool copy = std::holds_alternative<CopyCommand>(command);
            std::expected<std::optional<std::string>, std::error_code> selectedText;
            {
                std::scoped_lock lock(m_engineMutex);
                selectedText = m_engine->selectedText();
            }
            if (!selectedText)
            {
                postStatus((copy ? tr("Terminal copy failed: %1") : tr("Terminal selection read failed: %1"))
                               .arg(QString::fromStdString(selectedText.error().message())));
                continue;
            }
            const QString text = *selectedText ? QString::fromUtf8((*selectedText)->data(),
                                                                   static_cast<qsizetype>((*selectedText)->size()))
                                               : QString{};
            if (!copy)
            {
                emit selectedTextReady(text);
            }
            else if (*selectedText)
            {
                emit clipboardTextReady(text);
            }
            continue;
        }

        if (const auto *search = std::get_if<SearchCommand>(&command))
        {
            std::expected<TerminalSearchResult, std::error_code> result;
            {
                std::scoped_lock lock(m_engineMutex);
                result = m_engine->search(
                    std::string_view(search->query.constData(), static_cast<std::size_t>(search->query.size())),
                    search->direction, search->caseSensitive);
            }
            if (!result)
            {
                postStatus(tr("Terminal search failed: %1").arg(QString::fromStdString(result.error().message())));
                continue;
            }
            emit searchResultReady(QString::fromUtf8(search->query), result->current, result->total, result->wrapped);
            publishSnapshot();
            continue;
        }

        if (const auto *colorScheme = std::get_if<ColorSchemeCommand>(&command))
        {
            std::error_code error;
            std::vector<std::byte> ptyWrite;
            {
                std::scoped_lock lock(m_engineMutex);
                error = m_engine->setColorScheme(colorScheme->scheme);
                if (!error)
                {
                    ptyWrite = m_engine->takePtyWrite();
                }
            }
            if (error)
            {
                postStatus(tr("Terminal color scheme failed: %1").arg(QString::fromStdString(error.message())));
                continue;
            }
            if (!ptyWrite.empty() && !writeToProcess(ptyWrite))
            {
                break;
            }
            publishSnapshot();
            continue;
        }

        if (std::holds_alternative<ClearSearchCommand>(command))
        {
            std::error_code error;
            {
                std::scoped_lock lock(m_engineMutex);
                error = m_engine->clearSearch();
            }
            if (error)
            {
                postStatus(tr("Terminal search clear failed: %1").arg(QString::fromStdString(error.message())));
                continue;
            }
            emit searchResultReady({}, 0, 0, false);
            publishSnapshot();
            continue;
        }

        const auto geometry = std::get<TerminalGeometry>(command);
        {
            std::scoped_lock lock(m_engineMutex);
            // Keep ConPTY and the parser on one geometry boundary. Otherwise
            // resize-triggered shell output can be parsed against the old grid.
            if (const std::error_code resizeError =
                    m_process->resize({.columns = geometry.columns, .rows = geometry.rows}))
            {
                postStatus(tr("Terminal resize failed: %1").arg(QString::fromStdString(resizeError.message())));
                continue;
            }
            if (const std::error_code resizeError = m_engine->resize(geometry))
            {
                postStatus(tr("Terminal state resize failed: %1").arg(QString::fromStdString(resizeError.message())));
                continue;
            }
        }
        publishSnapshot();
    }
}

void LocalTerminalSession::monitorProcessExit(const std::stop_token &stopToken)
{
    using namespace std::chrono_literals;
    // Block on the process handle plus a stop event instead of polling every
    // 50 ms; the stop callback wakes the wait when the session shuts down.
    const HANDLE wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const std::stop_callback wake(stopToken, [wakeEvent] {
        SetEvent(wakeEvent);
    });
    while (!stopToken.stop_requested())
    {
        const auto exited = wakeEvent == nullptr ? m_process->waitForExit(50ms)
                                                 : m_process->waitForExitOrEvent(std::chrono::hours{1}, wakeEvent);
        if (!exited)
        {
            postStatus(tr("Unable to monitor local shell: %1").arg(QString::fromStdString(exited.error().message())));
            break;
        }
        if (!*exited)
            continue;
        if (m_readThread.joinable())
            CancelSynchronousIo(static_cast<HANDLE>(m_readThread.native_handle()));
        emit processExitObserved();
        break;
    }
    CloseHandle(wakeEvent);
}

bool LocalTerminalSession::writeToProcess(const std::span<const std::byte> bytes)
{
    std::scoped_lock lock(m_processWriteMutex);
    if (const std::error_code writeError = m_process->write(bytes))
    {
        postStatus(tr("Terminal write failed: %1").arg(QString::fromStdString(writeError.message())));
        return false;
    }
    return true;
}

void LocalTerminalSession::postStatus(const QString &status)
{
    emit statusChanged(status);
}

void LocalTerminalSession::postProcessExited()
{
    if (m_stopThread.joinable() || !m_running.load())
    {
        return;
    }
    // Preserve the last prompt/output frame before the stopped state disables
    // normal coalesced snapshot delivery.
    deliverLatestSnapshot();
    m_snapshotDeliveryTimer.stop();
    if (!m_running.exchange(false))
    {
        return;
    }
    m_writeThread.request_stop();
    m_commandAvailable.notify_all();
    emit statusChanged(tr("Local shell exited"));
    emit runningChanged(false);
    logMetrics();
    qCInfo(terminalSessionLog) << "Local shell process exited";
}

LocalTerminalSession::SnapshotCounters LocalTerminalSession::snapshotCounters() const noexcept
{
    return {.produced = m_snapshotsProduced.load(),
            .delivered = m_snapshotsDelivered.load(),
            .coalesced = m_snapshotsCoalesced.load()};
}

void LocalTerminalSession::resetMetrics() noexcept
{
    m_readBytes.store(0, std::memory_order_relaxed);
    m_snapshotsProduced.store(0, std::memory_order_relaxed);
    m_snapshotsDelivered.store(0, std::memory_order_relaxed);
    m_snapshotsCoalesced.store(0, std::memory_order_relaxed);
    m_fullDamageSnapshots.store(0, std::memory_order_relaxed);
    m_partialDamageSnapshots.store(0, std::memory_order_relaxed);
    m_cleanSnapshots.store(0, std::memory_order_relaxed);
    m_snapshotBuildNanoseconds.store(0, std::memory_order_relaxed);
    m_maxSnapshotBuildNanoseconds.store(0, std::memory_order_relaxed);
    m_inputQueueLatency.reset();
    m_inputQueueLatencyWindow.reset();
}

void LocalTerminalSession::logMetrics() const
{
    constexpr double nanosecondsPerMillisecond = 1'000'000.0;
    const std::uint64_t produced = m_snapshotsProduced.load(std::memory_order_relaxed);
    const std::uint64_t totalBuildNanoseconds = m_snapshotBuildNanoseconds.load(std::memory_order_relaxed);
    const double averageBuildMilliseconds =
        produced == 0
            ? 0.0
            : static_cast<double>(totalBuildNanoseconds) / static_cast<double>(produced) / nanosecondsPerMillisecond;
    const diagnostics::LatencySummary inputQueueLatency = m_inputQueueLatency.summary();
    qCInfo(terminalSessionLog) << "Terminal session metrics"
                               << "readBytes=" << m_readBytes.load(std::memory_order_relaxed)
                               << "snapshotsProduced=" << produced
                               << "snapshotsDelivered=" << m_snapshotsDelivered.load(std::memory_order_relaxed)
                               << "snapshotsCoalesced=" << m_snapshotsCoalesced.load(std::memory_order_relaxed)
                               << "fullDamage=" << m_fullDamageSnapshots.load(std::memory_order_relaxed)
                               << "partialDamage=" << m_partialDamageSnapshots.load(std::memory_order_relaxed)
                               << "clean=" << m_cleanSnapshots.load(std::memory_order_relaxed)
                               << "snapshotBuildAverageMs=" << averageBuildMilliseconds << "snapshotBuildMaxMs="
                               << (static_cast<double>(m_maxSnapshotBuildNanoseconds.load(std::memory_order_relaxed))
                                   / nanosecondsPerMillisecond)
                               << "inputQueueSamples=" << inputQueueLatency.count
                               << "inputQueueP50Us=" << inputQueueLatency.p50UpperBoundMicroseconds
                               << "inputQueueP95Us=" << inputQueueLatency.p95UpperBoundMicroseconds
                               << "inputQueueP99Us=" << inputQueueLatency.p99UpperBoundMicroseconds
                               << "inputQueueMaxUs=" << inputQueueLatency.maxMicroseconds;
}

} // namespace ztermy::terminal
