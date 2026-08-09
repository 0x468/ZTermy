#include "infrastructure/workbench/WorkspaceStateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <optional>
#include <utility>

namespace ztermy::workbench
{
namespace
{

constexpr int currentSchemaVersion = 6;

QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string bytes(const QString &value)
{
    const QByteArray encoded = value.toUtf8();
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
}

QString restoreKindToken(const TerminalRestoreKind kind)
{
    switch (kind)
    {
        case TerminalRestoreKind::Local:
            return QStringLiteral("local");
        case TerminalRestoreKind::SshProfile:
            return QStringLiteral("ssh-profile");
        case TerminalRestoreKind::Transient:
            return QStringLiteral("transient");
    }
    return {};
}

QString nodeKindToken(const TerminalLayoutNodeKind kind)
{
    return kind == TerminalLayoutNodeKind::Leaf ? QStringLiteral("leaf") : QStringLiteral("split");
}

QString orientationToken(const TerminalSplitOrientation orientation)
{
    return orientation == TerminalSplitOrientation::Horizontal ? QStringLiteral("horizontal")
                                                               : QStringLiteral("vertical");
}

QJsonObject serializeTerminalWorkspace(const TerminalWorkspaceLayout &layout)
{
    QJsonArray nodes;
    for (const TerminalLayoutNode &node : layout.nodes)
    {
        nodes.push_back(QJsonObject{
            {QStringLiteral("id"), text(node.id)},
            {QStringLiteral("kind"), nodeKindToken(node.kind)},
            {QStringLiteral("restoreIntentId"), text(node.restoreIntentId)},
            {QStringLiteral("firstChildId"), text(node.firstChildId)},
            {QStringLiteral("secondChildId"), text(node.secondChildId)},
            {QStringLiteral("orientation"), orientationToken(node.orientation)},
            {QStringLiteral("ratio"), node.ratio},
        });
    }
    QJsonArray intents;
    for (const TerminalRestoreIntent &intent : layout.restoreIntents)
    {
        intents.push_back(QJsonObject{
            {QStringLiteral("id"), text(intent.id)},
            {QStringLiteral("kind"), restoreKindToken(intent.kind)},
            {QStringLiteral("profileId"), text(intent.profileId)},
            {QStringLiteral("title"), text(intent.title)},
        });
    }
    return {
        {QStringLiteral("id"), text(layout.id)},
        {QStringLiteral("title"), text(layout.title)},
        {QStringLiteral("rootNodeId"), text(layout.rootNodeId)},
        {QStringLiteral("activePaneId"), text(layout.activePaneId)},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("restoreIntents"), intents},
    };
}

std::optional<TerminalRestoreKind> parseRestoreKind(const QJsonValue &value)
{
    if (!value.isString())
    {
        return std::nullopt;
    }
    const QString token = value.toString();
    if (token == QStringLiteral("local"))
    {
        return TerminalRestoreKind::Local;
    }
    if (token == QStringLiteral("ssh-profile"))
    {
        return TerminalRestoreKind::SshProfile;
    }
    if (token == QStringLiteral("transient"))
    {
        return TerminalRestoreKind::Transient;
    }
    return std::nullopt;
}

std::optional<TerminalLayoutNodeKind> parseNodeKind(const QJsonValue &value)
{
    if (!value.isString())
    {
        return std::nullopt;
    }
    const QString token = value.toString();
    if (token == QStringLiteral("leaf"))
    {
        return TerminalLayoutNodeKind::Leaf;
    }
    if (token == QStringLiteral("split"))
    {
        return TerminalLayoutNodeKind::Split;
    }
    return std::nullopt;
}

std::optional<TerminalSplitOrientation> parseOrientation(const QJsonValue &value)
{
    if (!value.isString())
    {
        return std::nullopt;
    }
    const QString token = value.toString();
    if (token == QStringLiteral("horizontal"))
    {
        return TerminalSplitOrientation::Horizontal;
    }
    if (token == QStringLiteral("vertical"))
    {
        return TerminalSplitOrientation::Vertical;
    }
    return std::nullopt;
}

std::optional<TerminalWorkspaceLayout> parseTerminalWorkspace(const QJsonValue &value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue nodesValue = object.value(QStringLiteral("nodes"));
    const QJsonValue intentsValue = object.value(QStringLiteral("restoreIntents"));
    if (!object.value(QStringLiteral("id")).isString() || !object.value(QStringLiteral("title")).isString()
        || !object.value(QStringLiteral("rootNodeId")).isString()
        || !object.value(QStringLiteral("activePaneId")).isString() || !nodesValue.isArray() || !intentsValue.isArray())
    {
        return std::nullopt;
    }
    TerminalWorkspaceLayout layout{
        .id = bytes(object.value(QStringLiteral("id")).toString()),
        .title = bytes(object.value(QStringLiteral("title")).toString()),
        .rootNodeId = bytes(object.value(QStringLiteral("rootNodeId")).toString()),
        .activePaneId = bytes(object.value(QStringLiteral("activePaneId")).toString()),
    };
    const QJsonArray nodes = nodesValue.toArray();
    layout.nodes.reserve(static_cast<std::size_t>(nodes.size()));
    for (const QJsonValue nodeValue : nodes)
    {
        if (!nodeValue.isObject())
        {
            return std::nullopt;
        }
        const QJsonObject node = nodeValue.toObject();
        const auto kind = parseNodeKind(node.value(QStringLiteral("kind")));
        const auto orientation = parseOrientation(node.value(QStringLiteral("orientation")));
        if (!kind || !orientation || !node.value(QStringLiteral("id")).isString()
            || !node.value(QStringLiteral("restoreIntentId")).isString()
            || !node.value(QStringLiteral("firstChildId")).isString()
            || !node.value(QStringLiteral("secondChildId")).isString()
            || !node.value(QStringLiteral("ratio")).isDouble())
        {
            return std::nullopt;
        }
        layout.nodes.push_back({
            .id = bytes(node.value(QStringLiteral("id")).toString()),
            .restoreIntentId = bytes(node.value(QStringLiteral("restoreIntentId")).toString()),
            .firstChildId = bytes(node.value(QStringLiteral("firstChildId")).toString()),
            .secondChildId = bytes(node.value(QStringLiteral("secondChildId")).toString()),
            .kind = *kind,
            .orientation = *orientation,
            .ratio = node.value(QStringLiteral("ratio")).toDouble(),
        });
    }
    const QJsonArray intents = intentsValue.toArray();
    layout.restoreIntents.reserve(static_cast<std::size_t>(intents.size()));
    for (const QJsonValue intentValue : intents)
    {
        if (!intentValue.isObject())
        {
            return std::nullopt;
        }
        const QJsonObject intent = intentValue.toObject();
        const auto kind = parseRestoreKind(intent.value(QStringLiteral("kind")));
        if (!kind || !intent.value(QStringLiteral("id")).isString()
            || !intent.value(QStringLiteral("profileId")).isString()
            || !intent.value(QStringLiteral("title")).isString())
        {
            return std::nullopt;
        }
        layout.restoreIntents.push_back({
            .id = bytes(intent.value(QStringLiteral("id")).toString()),
            .profileId = bytes(intent.value(QStringLiteral("profileId")).toString()),
            .title = bytes(intent.value(QStringLiteral("title")).toString()),
            .kind = *kind,
        });
    }
    return validTerminalWorkspaceLayout(layout) ? std::optional{std::move(layout)} : std::nullopt;
}

QJsonObject serializeProfile(const ProfileWorkspaceState &state)
{
    QJsonArray recent;
    for (const std::string &path : state.recentRemotePaths)
    {
        recent.push_back(text(path));
    }
    QJsonArray bookmarks;
    for (const std::string &path : state.bookmarkedRemotePaths)
    {
        bookmarks.push_back(text(path));
    }
    return {
        {QStringLiteral("profileId"), text(state.profileId)},
        {QStringLiteral("lastRemotePath"), text(state.lastRemotePath)},
        {QStringLiteral("recentRemotePaths"), recent},
        {QStringLiteral("bookmarkedRemotePaths"), bookmarks},
        {QStringLiteral("workbenchPage"), text(state.workbenchPage)},
        {QStringLiteral("workbenchSide"), text(state.workbenchSide)},
        {QStringLiteral("sftpViewMode"), text(state.sftpViewMode)},
        {QStringLiteral("sftpSortColumn"), text(state.sftpSortColumn)},
        {QStringLiteral("sftpFilenameEncoding"), text(state.sftpFilenameEncoding)},
        {QStringLiteral("followTerminalDirectory"), state.followTerminalDirectory},
        {QStringLiteral("sftpSortAscending"), state.sftpSortAscending},
        {QStringLiteral("sftpDirectoriesFirst"), state.sftpDirectoriesFirst},
        {QStringLiteral("sftpShowModifiedColumn"), state.sftpShowModifiedColumn},
        {QStringLiteral("sftpShowSizeColumn"), state.sftpShowSizeColumn},
        {QStringLiteral("sftpShowTypeColumn"), state.sftpShowTypeColumn},
        {QStringLiteral("workbenchWidth"), state.workbenchWidth},
        {QStringLiteral("composerHeight"), state.composerHeight},
    };
}

std::optional<ProfileWorkspaceState> parseProfile(const QJsonValue &value, const int schemaVersion)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue recentValue = object.value(QStringLiteral("recentRemotePaths"));
    const QJsonValue bookmarksValue = object.value(QStringLiteral("bookmarkedRemotePaths"));
    if (!object.value(QStringLiteral("profileId")).isString()
        || !object.value(QStringLiteral("lastRemotePath")).isString() || !recentValue.isArray()
        || !object.value(QStringLiteral("workbenchPage")).isString()
        || !object.value(QStringLiteral("workbenchSide")).isString()
        || !object.value(QStringLiteral("workbenchWidth")).isDouble()
        || !object.value(QStringLiteral("composerHeight")).isDouble()
        || (schemaVersion >= 3 && !bookmarksValue.isArray())
        || (schemaVersion >= 4
            && (!object.value(QStringLiteral("sftpViewMode")).isString()
                || !object.value(QStringLiteral("followTerminalDirectory")).isBool()))
        || (schemaVersion >= 5
            && (!object.value(QStringLiteral("sftpSortColumn")).isString()
                || !object.value(QStringLiteral("sftpFilenameEncoding")).isString()
                || !object.value(QStringLiteral("sftpSortAscending")).isBool()
                || !object.value(QStringLiteral("sftpDirectoriesFirst")).isBool()
                || !object.value(QStringLiteral("sftpShowModifiedColumn")).isBool()
                || !object.value(QStringLiteral("sftpShowSizeColumn")).isBool()
                || !object.value(QStringLiteral("sftpShowTypeColumn")).isBool())))
    {
        return std::nullopt;
    }
    ProfileWorkspaceState state{
        .profileId = bytes(object.value(QStringLiteral("profileId")).toString()),
        .lastRemotePath = bytes(object.value(QStringLiteral("lastRemotePath")).toString()),
        .workbenchPage = bytes(object.value(QStringLiteral("workbenchPage")).toString()),
        .workbenchSide = bytes(object.value(QStringLiteral("workbenchSide")).toString()),
        .workbenchWidth = object.value(QStringLiteral("workbenchWidth")).toDouble(),
        .composerHeight = object.value(QStringLiteral("composerHeight")).toDouble(),
    };
    if (schemaVersion >= 4)
    {
        state.sftpViewMode = bytes(object.value(QStringLiteral("sftpViewMode")).toString());
        state.followTerminalDirectory = object.value(QStringLiteral("followTerminalDirectory")).toBool();
    }
    if (schemaVersion >= 5)
    {
        state.sftpSortColumn = bytes(object.value(QStringLiteral("sftpSortColumn")).toString());
        state.sftpFilenameEncoding = bytes(object.value(QStringLiteral("sftpFilenameEncoding")).toString());
        state.sftpSortAscending = object.value(QStringLiteral("sftpSortAscending")).toBool();
        state.sftpDirectoriesFirst = object.value(QStringLiteral("sftpDirectoriesFirst")).toBool();
        state.sftpShowModifiedColumn = object.value(QStringLiteral("sftpShowModifiedColumn")).toBool();
        state.sftpShowSizeColumn = object.value(QStringLiteral("sftpShowSizeColumn")).toBool();
        state.sftpShowTypeColumn = object.value(QStringLiteral("sftpShowTypeColumn")).toBool();
    }
    const QJsonArray recent = recentValue.toArray();
    state.recentRemotePaths.reserve(static_cast<std::size_t>(recent.size()));
    for (const QJsonValue path : recent)
    {
        if (!path.isString())
        {
            return std::nullopt;
        }
        state.recentRemotePaths.push_back(bytes(path.toString()));
    }
    if (schemaVersion >= 3)
    {
        const QJsonArray bookmarks = bookmarksValue.toArray();
        state.bookmarkedRemotePaths.reserve(static_cast<std::size_t>(bookmarks.size()));
        for (const QJsonValue path : bookmarks)
        {
            if (!path.isString())
            {
                return std::nullopt;
            }
            state.bookmarkedRemotePaths.push_back(bytes(path.toString()));
        }
    }
    return validProfileWorkspaceState(state) ? std::optional{std::move(state)} : std::nullopt;
}

std::expected<WorkspaceState, WorkspaceStateStoreError> parseWorkspacePayload(const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
    }
    const QJsonObject root = document.object();
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion < 1 || schemaVersion > currentSchemaVersion)
    {
        return std::unexpected(WorkspaceStateStoreError::UnsupportedVersion);
    }
    const QJsonValue profilesValue = root.value(QStringLiteral("profiles"));
    if (!profilesValue.isArray())
    {
        return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
    }
    WorkspaceState state;
    const QJsonArray profiles = profilesValue.toArray();
    state.profiles.reserve(static_cast<std::size_t>(profiles.size()));
    for (const QJsonValue value : profiles)
    {
        auto profile = parseProfile(value, schemaVersion);
        if (!profile || findProfileWorkspaceState(state, profile->profileId) != nullptr)
        {
            return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
        }
        state.profiles.push_back(std::move(*profile));
    }
    if (schemaVersion >= 2)
    {
        const QJsonValue collapsedValue = root.value(QStringLiteral("collapsedHostSections"));
        if (!collapsedValue.isArray())
        {
            return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
        }
        for (const QJsonValue section : collapsedValue.toArray())
        {
            if (!section.isString())
            {
                return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
            }
            state.collapsedHostSections.push_back(bytes(section.toString()));
        }
    }
    if (schemaVersion >= 6)
    {
        const QJsonValue workspacesValue = root.value(QStringLiteral("terminalWorkspaces"));
        const QJsonValue activeWorkspaceValue = root.value(QStringLiteral("activeTerminalWorkspaceId"));
        if (!workspacesValue.isArray() || !activeWorkspaceValue.isString())
        {
            return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
        }
        const QJsonArray workspaces = workspacesValue.toArray();
        state.terminalWorkspaces.reserve(static_cast<std::size_t>(workspaces.size()));
        for (const QJsonValue workspaceValue : workspaces)
        {
            auto workspace = parseTerminalWorkspace(workspaceValue);
            if (!workspace)
            {
                return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
            }
            state.terminalWorkspaces.push_back(std::move(*workspace));
        }
        state.activeTerminalWorkspaceId = bytes(activeWorkspaceValue.toString());
    }
    return validWorkspaceState(state) ? std::expected<WorkspaceState, WorkspaceStateStoreError>{std::move(state)}
                                      : std::unexpected(WorkspaceStateStoreError::InvalidDocument);
}

std::expected<QByteArray, WorkspaceStateStoreError> readWorkspacePayload(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(WorkspaceStateStoreError::Io);
    }
    return file.readAll();
}

std::expected<void, WorkspaceStateStoreError> writeWorkspacePayload(const QString &path, const QByteArray &payload)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit())
    {
        return std::unexpected(WorkspaceStateStoreError::Io);
    }
    return {};
}

} // namespace

WorkspaceStateStore::WorkspaceStateStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &WorkspaceStateStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<WorkspaceState, WorkspaceStateStoreError> WorkspaceStateStore::load() const
{
    const QString backupPath = m_filePath + QStringLiteral(".bak");
    if (!QFileInfo::exists(m_filePath))
    {
        if (QFileInfo::exists(backupPath))
        {
            auto backupPayload = readWorkspacePayload(backupPath);
            if (backupPayload)
            {
                return parseWorkspacePayload(*backupPayload);
            }
        }
        return WorkspaceState{};
    }
    auto primaryPayload = readWorkspacePayload(m_filePath);
    auto primary =
        primaryPayload
            ? parseWorkspacePayload(*primaryPayload)
            : std::expected<WorkspaceState, WorkspaceStateStoreError>{std::unexpected(primaryPayload.error())};
    if (primary || primary.error() == WorkspaceStateStoreError::UnsupportedVersion || !QFileInfo::exists(backupPath))
    {
        return primary;
    }
    auto backupPayload = readWorkspacePayload(backupPath);
    if (backupPayload)
    {
        auto backup = parseWorkspacePayload(*backupPayload);
        if (backup)
        {
            return backup;
        }
    }
    return primary;
}

std::expected<void, WorkspaceStateStoreError> WorkspaceStateStore::save(const WorkspaceState &state) const
{
    if (!validWorkspaceState(state))
    {
        return std::unexpected(WorkspaceStateStoreError::InvalidDocument);
    }
    QDir directory = QFileInfo(m_filePath).dir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        return std::unexpected(WorkspaceStateStoreError::Io);
    }
    const QString backupPath = m_filePath + QStringLiteral(".bak");
    if (QFileInfo::exists(m_filePath))
    {
        auto previousPayload = readWorkspacePayload(m_filePath);
        if (previousPayload)
        {
            auto previous = parseWorkspacePayload(*previousPayload);
            if (!previous && previous.error() == WorkspaceStateStoreError::UnsupportedVersion)
            {
                return std::unexpected(WorkspaceStateStoreError::UnsupportedVersion);
            }
            if (previous)
            {
                auto backupWritten = writeWorkspacePayload(backupPath, *previousPayload);
                if (!backupWritten)
                {
                    return backupWritten;
                }
            }
        }
    }
    QJsonArray profiles;
    for (const ProfileWorkspaceState &profile : state.profiles)
    {
        profiles.push_back(serializeProfile(profile));
    }
    QJsonArray collapsedSections;
    for (const std::string &section : state.collapsedHostSections)
    {
        collapsedSections.push_back(text(section));
    }
    QJsonArray terminalWorkspaces;
    for (const TerminalWorkspaceLayout &workspace : state.terminalWorkspaces)
    {
        terminalWorkspaces.push_back(serializeTerminalWorkspace(workspace));
    }
    const QByteArray payload =
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), currentSchemaVersion},
                                  {QStringLiteral("profiles"), profiles},
                                  {QStringLiteral("collapsedHostSections"), collapsedSections},
                                  {QStringLiteral("terminalWorkspaces"), terminalWorkspaces},
                                  {QStringLiteral("activeTerminalWorkspaceId"), text(state.activeTerminalWorkspaceId)}})
            .toJson(QJsonDocument::Indented);
    return writeWorkspacePayload(m_filePath, payload);
}

} // namespace ztermy::workbench
