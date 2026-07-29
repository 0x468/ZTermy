#include "application/AppController.h"

#include "ui/terminal/TerminalItem.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <system_error>

Q_LOGGING_CATEGORY(appControllerLog, "ztermy.application.controller")

namespace
{

[[nodiscard]] QString applicationDataFile(const QString &fileName)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(fileName);
}

[[nodiscard]] QString siblingKnownHostsFile(const QString &profileStorePath)
{
    return QFileInfo(profileStorePath).dir().filePath(QStringLiteral("known_hosts.json"));
}

[[nodiscard]] std::string utf8String(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

[[nodiscard]] QString utf8QString(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

} // namespace

namespace ztermy
{

AppController::AppController(QObject *parent)
    : AppController(applicationDataFile(QStringLiteral("profiles.json")),
                    applicationDataFile(QStringLiteral("known_hosts.json")), parent)
{
}

AppController::AppController(const QString &profileStorePath, QObject *parent)
    : AppController(profileStorePath, siblingKnownHostsFile(profileStorePath), parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath, QObject *parent)
    : AppController(
          std::move(profileStorePath), std::move(knownHostsPath),
          [] {
              return std::make_unique<terminal::LocalTerminalSession>();
          },
          parent)
{
}

AppController::AppController(QString profileStorePath, QString knownHostsPath,
                             LocalTerminalSessionFactory localSessionFactory, QObject *parent)
    : QObject(parent),
      m_localSessionFactory(std::move(localSessionFactory)),
      m_profileStore(std::move(profileStorePath)),
      m_knownHostsPath(std::move(knownHostsPath))
{
    if (!m_localSessionFactory)
    {
        m_localSessionFactory = [] {
            return std::make_unique<terminal::LocalTerminalSession>();
        };
    }
    Q_ASSERT(m_localSessionFactory);
    loadHostProfiles();
}

AppController::~AppController()
{
    shutdown();
}

void AppController::attachTerminal(ui::TerminalItem *terminal)
{
    if (m_terminal == terminal)
    {
        return;
    }
    if (m_terminal != nullptr)
    {
        QObject::disconnect(m_terminal, nullptr, this, nullptr);
    }
    m_terminal = terminal;
    connectTerminalSignals();
    showActiveTab();
}

void AppController::shutdown() noexcept
{
    clearHostKeyPrompt();
    for (const auto &tab : m_tabs)
    {
        if (tab->local)
        {
            tab->local->stop();
        }
        if (tab->ssh)
        {
            tab->ssh->stop();
        }
    }
    m_tabs.clear();
    m_activeTabId.clear();
    if (m_terminal != nullptr)
    {
        QObject::disconnect(m_terminal, nullptr, this, nullptr);
        m_terminal->setSnapshot({});
        m_terminal = nullptr;
    }
}

bool AppController::sshActive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->kind == TerminalTabKind::Ssh;
}

bool AppController::hostKeyPromptVisible() const noexcept
{
    return m_hostKeyPromptVisible;
}

QString AppController::hostKeyAlgorithm() const
{
    return m_hostKeyAlgorithm;
}

QString AppController::hostKeyFingerprint() const
{
    return m_hostKeyFingerprint;
}

bool AppController::hostKeyChangedWarning() const noexcept
{
    return m_hostKeyChangedWarning;
}

QString AppController::defaultPrivateKeyPath() const
{
    return QDir::toNativeSeparators(QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                                        .filePath(QStringLiteral(".ssh/id_ed25519")));
}

QVariantList AppController::hostProfiles() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_profiles.size()));
    for (const ssh::SshProfile &profile : m_profiles)
    {
        result.append(QVariantMap{
            {QStringLiteral("id"), utf8QString(profile.id)},
            {QStringLiteral("name"), utf8QString(profile.name)},
            {QStringLiteral("group"), utf8QString(profile.group)},
            {QStringLiteral("host"), utf8QString(profile.host)},
            {QStringLiteral("port"), profile.port},
            {QStringLiteral("username"), utf8QString(profile.username)},
            {QStringLiteral("authentication"), profile.authentication == ssh::SshAuthenticationMethod::PrivateKey
                                                   ? QStringLiteral("private-key")
                                                   : QStringLiteral("password")},
            {QStringLiteral("privateKeyPath"), utf8QString(profile.privateKeyPath)},
            {QStringLiteral("privateKeyPassphraseRequired"), profile.privateKeyPassphraseRequired},
        });
    }
    return result;
}

QVariantList AppController::terminalTabs() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_tabs.size()));
    for (const auto &tab : m_tabs)
    {
        result.append(QVariantMap{
            {QStringLiteral("id"), tab->id},
            {QStringLiteral("title"), tab->title},
            {QStringLiteral("kind"),
             tab->kind == TerminalTabKind::Local ? QStringLiteral("local") : QStringLiteral("ssh")},
            {QStringLiteral("status"), tab->status},
            {QStringLiteral("running"), tab->running},
        });
    }
    return result;
}

QString AppController::activeTerminalTabId() const
{
    return m_activeTabId;
}

QString AppController::terminalSearchQuery() const
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? QString{} : tab->searchQuery;
}

int AppController::terminalSearchCurrent() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchCurrent);
}

int AppController::terminalSearchTotal() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab == nullptr ? 0 : static_cast<int>(tab->searchTotal);
}

bool AppController::terminalSearchCaseSensitive() const noexcept
{
    const TerminalTab *tab = activeTab();
    return tab != nullptr && tab->searchCaseSensitive;
}

QString AppController::startLocalTerminal()
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("The maximum of 32 terminal tabs is already open"));
        }
        return {};
    }

    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->title = QStringLiteral("PowerShell %1").arg(m_nextLocalTabNumber++);
    tab->status = QStringLiteral("Starting local terminal...");
    tab->kind = TerminalTabKind::Local;
    tab->local = m_localSessionFactory();
    if (!tab->local)
    {
        return {};
    }
    QString tabId = tab->id;
    connectLocalTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->local)
    {
        return {};
    }
    const std::error_code error = created->local->start({.columns = 100, .rows = 30});
    if (error)
    {
        created->status =
            QStringLiteral("Unable to start local terminal: %1").arg(QString::fromStdString(error.message()));
        showActiveTab();
        emit terminalTabsChanged();
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return tabId;
}

bool AppController::activateTerminalTab(const QString &id)
{
    if (findTab(id) == nullptr)
    {
        return false;
    }
    if (m_activeTabId == id)
    {
        showActiveTab();
        return true;
    }
    m_activeTabId = id;
    emit activeTerminalTabChanged();
    emit sshActiveChanged();
    emit terminalSearchChanged();
    showActiveTab();
    return true;
}

bool AppController::closeTerminalTab(const QString &id)
{
    const auto position = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &tab) {
        return tab->id;
    });
    if (position == m_tabs.end())
    {
        return false;
    }

    const bool closingActive = (*position)->id == m_activeTabId;
    const std::size_t index = static_cast<std::size_t>(std::distance(m_tabs.begin(), position));
    if ((*position)->id == m_hostKeyTabId)
    {
        clearHostKeyPrompt();
    }
    if ((*position)->local)
    {
        (*position)->local->stop();
    }
    if ((*position)->ssh)
    {
        (*position)->ssh->stop();
    }
    m_tabs.erase(position);
    emit terminalTabsChanged();

    if (closingActive)
    {
        if (m_tabs.empty())
        {
            m_activeTabId.clear();
            emit activeTerminalTabChanged();
            emit sshActiveChanged();
            emit terminalSearchChanged();
            showActiveTab();
        }
        else
        {
            const std::size_t nextIndex = std::min(index, m_tabs.size() - 1U);
            m_activeTabId.clear();
            activateTerminalTab(m_tabs[nextIndex]->id);
        }
    }
    return true;
}

void AppController::searchTerminal(const QString &query, const bool backwards, const bool caseSensitive)
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    if (query.isEmpty())
    {
        clearTerminalSearch();
        return;
    }

    tab->searchQuery = query;
    tab->searchCaseSensitive = caseSensitive;
    if (tab->ssh)
    {
        tab->ssh->search(query, backwards, caseSensitive);
    }
    else if (tab->local)
    {
        tab->local->search(query, backwards, caseSensitive);
    }
    emit terminalSearchChanged();
}

void AppController::clearTerminalSearch()
{
    TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        return;
    }
    tab->searchQuery.clear();
    tab->searchCurrent = 0;
    tab->searchTotal = 0;
    if (tab->ssh)
    {
        tab->ssh->clearSearch();
    }
    else if (tab->local)
    {
        tab->local->clearSearch();
    }
    emit terminalSearchChanged();
}

bool AppController::connectPrivateKey(const QString &host, const int port, const QString &username,
                                      const QString &privateKeyPath, const QString &passphrase)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || privateKeyPath.trimmed().isEmpty() || port <= 0
        || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("Complete the SSH host, port, username, and private-key fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::PrivateKey,
        .privateKeyPath = privateKeyPath.trimmed(),
        .secret = security::SensitiveByteArray(passphrase.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::connectPassword(const QString &host, const int port, const QString &username,
                                    const QString &password)
{
    if (host.trimmed().isEmpty() || username.trimmed().isEmpty() || password.isEmpty() || port <= 0 || port > 65535)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("Complete the SSH host, port, username, and password fields"));
        }
        return false;
    }

    ssh::SshConnectionRequest request{
        .host = host.trimmed(),
        .port = static_cast<std::uint16_t>(port),
        .username = username.trimmed(),
        .authentication = ssh::SshAuthenticationMethod::Password,
        .privateKeyPath = {},
        .secret = security::SensitiveByteArray(password.toUtf8()),
        .knownHostsPath = m_knownHostsPath,
    };
    return startSshConnection(std::move(request));
}

bool AppController::startSshConnection(ssh::SshConnectionRequest request)
{
    if (m_tabs.size() >= maximumTerminalTabs)
    {
        if (m_terminal != nullptr)
        {
            m_terminal->setStatusText(QStringLiteral("The maximum of 32 terminal tabs is already open"));
        }
        return false;
    }

    auto tab = std::make_unique<TerminalTab>();
    tab->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab->title = QStringLiteral("%1@%2").arg(request.username, request.host);
    tab->status = QStringLiteral("Starting SSH connection...");
    tab->kind = TerminalTabKind::Ssh;
    tab->ssh = std::make_unique<ssh::SshTerminalSession>();
    const QString tabId = tab->id;
    connectSshTabSignals(*tab);
    m_tabs.push_back(std::move(tab));
    emit terminalTabsChanged();
    activateTerminalTab(tabId);

    TerminalTab *created = findTab(tabId);
    if (created == nullptr || !created->ssh)
    {
        return false;
    }
    const std::error_code error = created->ssh->start(std::move(request), {.columns = 100, .rows = 30});
    if (error)
    {
        closeTerminalTab(tabId);
        return false;
    }
    if (m_terminal != nullptr)
    {
        m_terminal->requestCurrentSize();
    }
    return true;
}

bool AppController::saveHostProfile(const QString &id, const QString &name, const QString &host, const int port,
                                    const QString &username, const QString &authentication,
                                    const QString &privateKeyPath, const bool privateKeyPassphraseRequired,
                                    const QString &group)
{
    const QString normalizedName = name.trimmed();
    const QString normalizedGroup = group.trimmed();
    const QString normalizedHost = host.trimmed();
    const QString normalizedUsername = username.trimmed();
    const QString normalizedPrivateKeyPath = privateKeyPath.trimmed();
    const QString profileId = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id.trimmed();
    const std::optional<ssh::SshAuthenticationMethod> authenticationMethod =
        authentication == QStringLiteral("private-key") ? std::optional{ssh::SshAuthenticationMethod::PrivateKey}
        : authentication == QStringLiteral("password")  ? std::optional{ssh::SshAuthenticationMethod::Password}
                                                        : std::nullopt;
    if (!authenticationMethod)
    {
        return false;
    }

    const ssh::SshProfile profile{
        .id = utf8String(profileId),
        .name = utf8String(normalizedName),
        .group = utf8String(normalizedGroup),
        .host = utf8String(normalizedHost),
        .port = port > 0 && port <= 65535 ? static_cast<std::uint16_t>(port) : std::uint16_t{0},
        .username = utf8String(normalizedUsername),
        .authentication = *authenticationMethod,
        .privateKeyPath = *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey
                              ? utf8String(normalizedPrivateKeyPath)
                              : std::string{},
        .privateKeyPassphraseRequired =
            *authenticationMethod == ssh::SshAuthenticationMethod::PrivateKey && privateKeyPassphraseRequired,
    };
    if (!ssh::validSshProfile(profile))
    {
        return false;
    }

    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto existing = std::ranges::find(updated, profile.id, &ssh::SshProfile::id);
    if (existing == updated.end())
    {
        updated.push_back(profile);
    }
    else
    {
        *existing = profile;
    }

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles";
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::duplicateHostProfile(const QString &id)
{
    const auto source = std::ranges::find(m_profiles, utf8String(id.trimmed()), &ssh::SshProfile::id);
    if (source == m_profiles.end())
    {
        return false;
    }

    const QString baseName = utf8QString(source->name) + QStringLiteral(" copy");
    QString copyName = baseName;
    for (int suffix = 2;
         std::ranges::any_of(m_profiles,
                             [&copyName](const ssh::SshProfile &profile) {
                                 return utf8QString(profile.name).compare(copyName, Qt::CaseInsensitive) == 0;
                             });
         ++suffix)
    {
        copyName = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
    }

    ssh::SshProfile copy = *source;
    copy.id = utf8String(QUuid::createUuid().toString(QUuid::WithoutBraces));
    copy.name = utf8String(copyName);
    std::vector<ssh::SshProfile> updated = m_profiles;
    updated.push_back(std::move(copy));
    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist duplicated SSH profile";
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::deleteHostProfile(const QString &id)
{
    const std::string profileId = utf8String(id);
    std::vector<ssh::SshProfile> updated = m_profiles;
    const auto profile = std::ranges::find(updated, profileId, &ssh::SshProfile::id);
    if (profile == updated.end())
    {
        return false;
    }
    updated.erase(profile);

    if (!m_profileStore.save(updated))
    {
        qCWarning(appControllerLog) << "Unable to persist SSH profiles after deletion";
        return false;
    }
    m_profiles = std::move(updated);
    emit hostProfilesChanged();
    return true;
}

bool AppController::connectHostProfile(const QString &id, const QString &secret)
{
    const std::string profileId = utf8String(id);
    const auto profile = std::ranges::find(m_profiles, profileId, &ssh::SshProfile::id);
    if (profile == m_profiles.end())
    {
        return false;
    }
    if (profile->authentication == ssh::SshAuthenticationMethod::Password)
    {
        return connectPassword(utf8QString(profile->host), profile->port, utf8QString(profile->username), secret);
    }
    if (profile->privateKeyPassphraseRequired && secret.isEmpty())
    {
        return false;
    }
    return connectPrivateKey(utf8QString(profile->host), profile->port, utf8QString(profile->username),
                             utf8QString(profile->privateKeyPath), secret);
}

void AppController::acceptHostKey(const bool remember)
{
    if (!m_hostKeyPromptVisible || m_hostKeyChangedWarning)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    clearHostKeyPrompt();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->confirmHostKey(remember);
    }
}

void AppController::rejectHostKey()
{
    if (!m_hostKeyPromptVisible)
    {
        return;
    }
    TerminalTab *tab = findTab(m_hostKeyTabId);
    clearHostKeyPrompt();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->rejectHostKey();
    }
}

void AppController::connectLocalTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setClipboardText(text);
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::runningChanged, this,
                     [this, tabId](const bool running) {
                         if (TerminalTab *updated = findTab(tabId))
                         {
                             updated->running = running;
                             emit terminalTabsChanged();
                         }
                     });
    QObject::connect(tab.local.get(), &terminal::LocalTerminalSessionBackend::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_activeTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
}

void AppController::connectSshTabSignals(TerminalTab &tab)
{
    const QString tabId = tab.id;
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::snapshotReady, this,
                     [this, tabId](const terminal::TerminalSnapshotPtr &snapshot) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->snapshot = snapshot;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setSnapshot(snapshot);
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::statusChanged, this,
                     [this, tabId](const QString &status) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr)
                         {
                             return;
                         }
                         updated->status = status;
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setStatusText(status);
                         }
                         emit terminalTabsChanged();
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::clipboardTextReady, this,
                     [this, tabId](const QString &text) {
                         if (m_terminal != nullptr && m_activeTabId == tabId)
                         {
                             m_terminal->setClipboardText(text);
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::runningChanged, this, [this, tabId](const bool running) {
        if (TerminalTab *updated = findTab(tabId))
        {
            updated->running = running;
            emit terminalTabsChanged();
        }
    });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::searchResultReady, this,
                     [this, tabId](const QString &query, const quint32 current, const quint32 total, const bool) {
                         TerminalTab *updated = findTab(tabId);
                         if (updated == nullptr || (!query.isEmpty() && query != updated->searchQuery))
                         {
                             return;
                         }
                         updated->searchQuery = query;
                         updated->searchCurrent = current;
                         updated->searchTotal = total;
                         if (m_activeTabId == tabId)
                         {
                             emit terminalSearchChanged();
                         }
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyConfirmationRequired, this,
                     [this, tabId](const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(algorithm, fingerprint, false);
                     });
    QObject::connect(tab.ssh.get(), &ssh::SshTerminalSession::hostKeyChanged, this,
                     [this, tabId](const QString &algorithm, const QString &fingerprint) {
                         m_hostKeyTabId = tabId;
                         activateTerminalTab(tabId);
                         setHostKeyPrompt(algorithm, fingerprint, true);
                         if (m_terminal != nullptr)
                         {
                             m_terminal->setStatusText(QStringLiteral("SSH host key changed; connection blocked"));
                         }
                     });
}

void AppController::connectTerminalSignals()
{
    if (m_terminal == nullptr)
    {
        return;
    }
    QObject::connect(m_terminal, &ui::TerminalItem::inputGenerated, this, &AppController::queueInput);
    QObject::connect(m_terminal, &ui::TerminalItem::pasteRequested, this, &AppController::queuePaste);
    QObject::connect(m_terminal, &ui::TerminalItem::sizeRequested, this, &AppController::requestResize);
    QObject::connect(m_terminal, &ui::TerminalItem::scrollRequested, this, &AppController::requestScroll);
    QObject::connect(m_terminal, &ui::TerminalItem::selectionRequested, this, &AppController::requestSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::clearSelectionRequested, this, &AppController::clearSelection);
    QObject::connect(m_terminal, &ui::TerminalItem::copyRequested, this, &AppController::copySelection);
}

void AppController::queueInput(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->queueInput(bytes);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->queueInput(bytes);
    }
}

void AppController::queuePaste(const QByteArray &bytes)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->queuePaste(bytes);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->queuePaste(bytes);
    }
}

void AppController::requestScroll(const int rows)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestScroll(rows);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestScroll(rows);
    }
}

void AppController::requestSelection(const quint16 startColumn, const quint16 startRow, const quint16 endColumn,
                                     const quint16 endRow, const bool rectangular)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestSelection(startColumn, startRow, endColumn, endRow, rectangular);
    }
}

void AppController::clearSelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->clearSelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->clearSelection();
    }
}

void AppController::copySelection()
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->copySelection();
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->copySelection();
    }
}

void AppController::requestResize(const quint16 columns, const quint16 rows, const quint32 cellWidthPixels,
                                  const quint32 cellHeightPixels)
{
    TerminalTab *tab = activeTab();
    if (tab != nullptr && tab->ssh)
    {
        tab->ssh->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
    else if (tab != nullptr && tab->local)
    {
        tab->local->requestResize(columns, rows, cellWidthPixels, cellHeightPixels);
    }
}

AppController::TerminalTab *AppController::activeTab()
{
    return findTab(m_activeTabId);
}

const AppController::TerminalTab *AppController::activeTab() const
{
    return findTab(m_activeTabId);
}

AppController::TerminalTab *AppController::findTab(const QString &id)
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

const AppController::TerminalTab *AppController::findTab(const QString &id) const
{
    const auto tab = std::ranges::find(m_tabs, id, [](const std::unique_ptr<TerminalTab> &candidate) {
        return candidate->id;
    });
    return tab == m_tabs.end() ? nullptr : tab->get();
}

void AppController::showActiveTab()
{
    if (m_terminal == nullptr)
    {
        return;
    }
    const TerminalTab *tab = activeTab();
    if (tab == nullptr)
    {
        m_terminal->setSnapshot({});
        m_terminal->setStatusText(QStringLiteral("No terminal session"));
        return;
    }
    m_terminal->setSnapshot(tab->snapshot);
    m_terminal->setStatusText(tab->status);
    m_terminal->requestCurrentSize();
}

void AppController::setHostKeyPrompt(QString algorithm, QString fingerprint, const bool changed)
{
    m_hostKeyAlgorithm = std::move(algorithm);
    m_hostKeyFingerprint = std::move(fingerprint);
    m_hostKeyPromptVisible = true;
    m_hostKeyChangedWarning = changed;
    emit hostKeyPromptChanged();
}

void AppController::clearHostKeyPrompt()
{
    if (!m_hostKeyPromptVisible && m_hostKeyTabId.isEmpty() && m_hostKeyAlgorithm.isEmpty()
        && m_hostKeyFingerprint.isEmpty())
    {
        return;
    }
    m_hostKeyPromptVisible = false;
    m_hostKeyChangedWarning = false;
    m_hostKeyTabId.clear();
    m_hostKeyAlgorithm.clear();
    m_hostKeyFingerprint.clear();
    emit hostKeyPromptChanged();
}

void AppController::loadHostProfiles()
{
    auto profiles = m_profileStore.load();
    if (!profiles)
    {
        qCWarning(appControllerLog) << "Unable to load SSH profiles; starting with an empty profile list";
        return;
    }
    m_profiles = std::move(*profiles);
}

} // namespace ztermy
