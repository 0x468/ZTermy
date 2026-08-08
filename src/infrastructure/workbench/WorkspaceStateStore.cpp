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

constexpr int currentSchemaVersion = 4;

QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string bytes(const QString &value)
{
    const QByteArray encoded = value.toUtf8();
    return {encoded.constData(), static_cast<std::size_t>(encoded.size())};
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
        {QStringLiteral("followTerminalDirectory"), state.followTerminalDirectory},
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
                || !object.value(QStringLiteral("followTerminalDirectory")).isBool())))
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

} // namespace

WorkspaceStateStore::WorkspaceStateStore(QString filePath) : m_filePath(std::move(filePath)) {}

const QString &WorkspaceStateStore::filePath() const noexcept
{
    return m_filePath;
}

std::expected<WorkspaceState, WorkspaceStateStoreError> WorkspaceStateStore::load() const
{
    QFile file(m_filePath);
    if (!file.exists())
    {
        return WorkspaceState{};
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::unexpected(WorkspaceStateStoreError::Io);
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
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
    return validWorkspaceState(state) ? std::expected<WorkspaceState, WorkspaceStateStoreError>{std::move(state)}
                                      : std::unexpected(WorkspaceStateStoreError::InvalidDocument);
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
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        return std::unexpected(WorkspaceStateStoreError::Io);
    }
    const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), currentSchemaVersion},
                                                         {QStringLiteral("profiles"), profiles},
                                                         {QStringLiteral("collapsedHostSections"), collapsedSections}})
                                   .toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit())
    {
        return std::unexpected(WorkspaceStateStoreError::Io);
    }
    return {};
}

} // namespace ztermy::workbench
