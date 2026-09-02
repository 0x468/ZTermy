#include "application/workbench/CommandHistoryController.h"

#include "infrastructure/workbench/CommandHistoryStore.h"

#include <QPointer>

#include <algorithm>
#include <memory>
#include <ranges>
#include <utility>

namespace
{

[[nodiscard]] QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::string text(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString shellToken(const ztermy::workbench::ShellKind shell)
{
    using ztermy::workbench::ShellKind;
    switch (shell)
    {
        case ShellKind::bash:
            return QStringLiteral("bash");
        case ShellKind::zsh:
            return QStringLiteral("zsh");
        case ShellKind::fish:
            return QStringLiteral("fish");
        case ShellKind::powershell:
            return QStringLiteral("powershell");
        case ShellKind::nushell:
            return QStringLiteral("nushell");
        case ShellKind::unknown:
        default:
            return QStringLiteral("unknown");
    }
}

[[nodiscard]] QVariantMap value(const ztermy::workbench::IndexedCommand &entry)
{
    return {{QStringLiteral("command"), text(entry.command)},
            {QStringLiteral("sourceId"), text(entry.sourceId)},
            {QStringLiteral("sourceLabel"), text(entry.sourceLabel)},
            {QStringLiteral("shell"), shellToken(entry.shell)},
            {QStringLiteral("timestampUtcMs"), entry.lastUsedUtcSeconds * 1000},
            {QStringLiteral("useCount"), entry.useCount}};
}

} // namespace

namespace ztermy::workbench
{

CommandHistoryController::CommandHistoryController(QString storePath, QObject *parent)
    : QObject(parent), m_storePath(std::move(storePath))
{
    qRegisterMetaType<CommandHistoryIndex>();
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(15'000);
    QObject::connect(this, &CommandHistoryController::loadCompleted, this, &CommandHistoryController::applyLoad,
                     Qt::QueuedConnection);
    QObject::connect(
        this, &CommandHistoryController::persistenceFailed, this,
        [this] {
            m_error = tr("Command history could not be saved.");
            emit changed();
        },
        Qt::QueuedConnection);
    const QString path = m_storePath;
    const QPointer<CommandHistoryController> self(this);
    m_worker.start([path, self]() noexcept {
        try
        {
            auto loaded = CommandHistoryStore(path).load();
            if (self)
                emit self->loadCompleted(loaded ? std::move(*loaded) : CommandHistoryIndex{}, loaded.has_value());
        }
        catch (...)
        {
            if (self)
                emit self->loadCompleted({}, false);
        }
    });
}

CommandHistoryController::~CommandHistoryController()
{
    m_worker.clear();
    m_worker.waitForDone();
}

bool CommandHistoryController::ready() const noexcept
{
    return m_ready;
}

QString CommandHistoryController::error() const
{
    return m_error;
}

QVariantList CommandHistoryController::entries() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_index.entries.size()));
    for (const IndexedCommand &entry : m_index.entries)
        result.push_back(value(entry));
    return result;
}

QVariantList CommandHistoryController::suggestions(const QString &prefix, const int limit) const
{
    const QString needle = prefix.trimmed();
    if (needle.size() < 2 || limit <= 0)
        return {};
    std::vector<const IndexedCommand *> matches;
    matches.reserve(m_index.entries.size());
    for (const IndexedCommand &entry : m_index.entries)
    {
        const QString command = text(entry.command);
        if (command.compare(needle, Qt::CaseInsensitive) != 0 && command.contains(needle, Qt::CaseInsensitive))
            matches.push_back(&entry);
    }
    std::ranges::stable_sort(matches, [&needle](const IndexedCommand *left, const IndexedCommand *right) {
        const bool leftPrefix = text(left->command).startsWith(needle, Qt::CaseInsensitive);
        const bool rightPrefix = text(right->command).startsWith(needle, Qt::CaseInsensitive);
        if (leftPrefix != rightPrefix)
            return leftPrefix;
        if (left->useCount != right->useCount)
            return left->useCount > right->useCount;
        return left->lastUsedUtcSeconds > right->lastUsedUtcSeconds;
    });
    QVariantList result;
    const qsizetype count = (std::min)(static_cast<qsizetype>(matches.size()), static_cast<qsizetype>(limit));
    result.reserve(count);
    for (qsizetype index = 0; index < count; ++index)
        result.push_back(value(*matches[static_cast<std::size_t>(index)]));
    return result;
}

void CommandHistoryController::record(const QString &command, const ShellKind shell, const QString &sourceId,
                                      const QString &sourceLabel, const std::int64_t timestampUtcSeconds)
{
    IndexedCommand entry{.command = text(command.trimmed()),
                         .sourceId = text(sourceId),
                         .sourceLabel = text(sourceLabel),
                         .shell = shell,
                         .firstUsedUtcSeconds = timestampUtcSeconds,
                         .lastUsedUtcSeconds = timestampUtcSeconds};
    if (!validIndexedCommand(entry))
        return;
    if (!m_ready)
    {
        m_pending.push_back(std::move(entry));
        return;
    }
    recordIndexedCommand(m_index, std::move(entry));
    persist();
    emit changed();
}

void CommandHistoryController::applyLoad(CommandHistoryIndex index, const bool succeeded)
{
    if (!succeeded)
        m_error = tr("Command history could not be loaded.");
    for (IndexedCommand &entry : m_pending)
        recordIndexedCommand(index, std::move(entry));
    const bool shouldPersist = !m_pending.empty();
    m_pending.clear();
    m_index = std::move(index);
    m_ready = true;
    if (shouldPersist)
        persist();
    emit changed();
}

void CommandHistoryController::persist()
{
    const QString path = m_storePath;
    const auto snapshot = std::make_shared<const CommandHistoryIndex>(m_index);
    const QPointer<CommandHistoryController> self(this);
    m_worker.start([path, snapshot, self]() noexcept {
        try
        {
            if (!CommandHistoryStore(path).save(*snapshot) && self)
                emit self->persistenceFailed();
        }
        catch (...)
        {
            if (self)
                emit self->persistenceFailed();
        }
    });
}

} // namespace ztermy::workbench
