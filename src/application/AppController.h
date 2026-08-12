#pragma once

#include "application/actions/ActionRegistry.h"
#include "application/ai/AiActionToolDispatcher.h"
#include "application/ai/AiActivityModel.h"
#include "application/ai/AiConversationHistoryModel.h"
#include "application/ai/AiConversationModel.h"
#include "application/ai/AiNoteReadTool.h"
#include "application/ai/AiReadToolDispatcher.h"
#include "application/ai/AiSecretStore.h"
#include "application/ai/AiSftpListTool.h"
#include "application/ai/AiSftpReadTool.h"
#include "application/ai/AiTurnRunner.h"
#include "application/ai/McpRuntimeManager.h"
#include "application/forwarding/PortForwardingJob.h"
#include "application/security/CredentialVaultCoordinator.h"
#include "application/sftp/SftpDirectoryModel.h"
#include "application/sftp/TransferBatchCoordinator.h"
#include "application/sftp/TransferManager.h"
#include "application/ssh/SshTerminalSession.h"
#include "application/terminal/LocalTerminalSession.h"
#include "core/config/ApplicationPaths.h"
#include "core/config/ApplicationSettings.h"
#include "domain/ai/AiCommandTracker.h"
#include "domain/ai/AiContextBroker.h"
#include "domain/ai/AiTerminalFrameTracker.h"
#include "domain/ai/AiToolDispatchLedger.h"
#include "domain/terminal/SemanticTerminalObserver.h"
#include "domain/workbench/ScriptExecution.h"
#include "domain/workbench/ScriptRecorder.h"
#include "infrastructure/ai/ProviderModelCatalog.h"
#include "infrastructure/forwarding/PortForwardingRuleStore.h"
#include "infrastructure/logging/SessionLogWriter.h"
#include "infrastructure/ssh/SshProfileStore.h"
#include "infrastructure/workbench/NoteStore.h"
#include "infrastructure/workbench/PowerShellHistoryReader.h"
#include "infrastructure/workbench/ScriptStore.h"
#include "infrastructure/workbench/WorkspaceStateStore.h"

#include <QHash>
#include <QJsonObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ztermy::ui
{
class TerminalItem;
}

namespace ztermy::windowing
{
class WindowsProtectedClipboard;
}

namespace ztermy
{

using ShellHistoryEntries = std::vector<workbench::ShellHistoryEntry>;
using NoteSearchResults = std::vector<workbench::NoteSearchResult>;

} // namespace ztermy

Q_DECLARE_METATYPE(ztermy::ShellHistoryEntries)
Q_DECLARE_METATYPE(ztermy::NoteSearchResults)

namespace ztermy
{

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sshActive READ sshActive NOTIFY sshActiveChanged)
    Q_PROPERTY(QString startupRecoveryNotice READ startupRecoveryNotice NOTIFY startupRecoveryNoticeChanged)
    Q_PROPERTY(bool hostKeyPromptVisible READ hostKeyPromptVisible NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString hostKeyEndpoint READ hostKeyEndpoint NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString hostKeyAlgorithm READ hostKeyAlgorithm NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString hostKeyFingerprint READ hostKeyFingerprint NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(bool hostKeyChangedWarning READ hostKeyChangedWarning NOTIFY hostKeyPromptChanged)
    Q_PROPERTY(QString defaultPrivateKeyPath READ defaultPrivateKeyPath CONSTANT)
    Q_PROPERTY(QVariantList hostProfiles READ hostProfiles NOTIFY hostProfilesChanged)
    Q_PROPERTY(QVariantList recentHostProfiles READ recentHostProfiles NOTIFY hostProfilesChanged)
    Q_PROPERTY(QStringList hostProfileGroups READ hostProfileGroups NOTIFY hostProfilesChanged)
    Q_PROPERTY(QStringList collapsedHostSections READ collapsedHostSections NOTIFY hostWorkspaceChanged)
    Q_PROPERTY(QVariantList terminalTabs READ terminalTabs NOTIFY terminalTabsChanged)
    Q_PROPERTY(QVariantList quickCommands READ quickCommands NOTIFY quickCommandsChanged)
    Q_PROPERTY(QString quickCommandOperationError READ quickCommandOperationError NOTIFY quickCommandsChanged)
    Q_PROPERTY(QVariantList notes READ notes NOTIFY notesChanged)
    Q_PROPERTY(QVariantList noteSearchResults READ noteSearchResults NOTIFY notesChanged)
    Q_PROPERTY(QString activeNotePath READ activeNotePath NOTIFY notesChanged)
    Q_PROPERTY(QString activeNoteContent READ activeNoteContent NOTIFY notesChanged)
    Q_PROPERTY(bool activeNoteDirty READ activeNoteDirty NOTIFY notesChanged)
    Q_PROPERTY(QString noteSearchState READ noteSearchState NOTIFY notesChanged)
    Q_PROPERTY(QString noteOperationError READ noteOperationError NOTIFY notesChanged)
    Q_PROPERTY(QVariantList terminalHistory READ terminalHistory NOTIFY terminalHistoryChanged)
    Q_PROPERTY(QVariantList terminalGlobalHistory READ terminalGlobalHistory NOTIFY terminalHistoryChanged)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY actionRegistryChanged)
    Q_PROPERTY(QString terminalHistoryState READ terminalHistoryState NOTIFY terminalHistoryChanged)
    Q_PROPERTY(QString terminalHistoryError READ terminalHistoryError NOTIFY terminalHistoryChanged)
    Q_PROPERTY(QObject *activeSftpDirectoryModel READ activeSftpDirectoryModel NOTIFY sftpChanged)
    Q_PROPERTY(QString activeSftpPath READ activeSftpPath NOTIFY sftpChanged)
    Q_PROPERTY(QString activeSftpHomePath READ activeSftpHomePath NOTIFY sftpChanged)
    Q_PROPERTY(QVariantList recentSftpPaths READ recentSftpPaths NOTIFY sftpChanged)
    Q_PROPERTY(QVariantList bookmarkedSftpPaths READ bookmarkedSftpPaths NOTIFY sftpChanged)
    Q_PROPERTY(bool activeSftpPathBookmarked READ activeSftpPathBookmarked NOTIFY sftpChanged)
    Q_PROPERTY(QString activeSftpState READ activeSftpState NOTIFY sftpChanged)
    Q_PROPERTY(QString activeSftpError READ activeSftpError NOTIFY sftpChanged)
    Q_PROPERTY(QString activeSftpViewMode READ activeSftpViewMode NOTIFY sftpChanged)
    Q_PROPERTY(QString activeSftpSortColumn READ activeSftpSortColumn NOTIFY sftpChanged)
    Q_PROPERTY(bool activeSftpSortAscending READ activeSftpSortAscending NOTIFY sftpChanged)
    Q_PROPERTY(bool activeSftpDirectoriesFirst READ activeSftpDirectoriesFirst NOTIFY sftpChanged)
    Q_PROPERTY(bool activeSftpShowModifiedColumn READ activeSftpShowModifiedColumn NOTIFY sftpChanged)
    Q_PROPERTY(bool activeSftpShowSizeColumn READ activeSftpShowSizeColumn NOTIFY sftpChanged)
    Q_PROPERTY(bool activeSftpShowTypeColumn READ activeSftpShowTypeColumn NOTIFY sftpChanged)
    Q_PROPERTY(QString activeSftpFilenameEncoding READ activeSftpFilenameEncoding NOTIFY sftpChanged)
    Q_PROPERTY(bool activeSftpFollowTerminalDirectory READ activeSftpFollowTerminalDirectory NOTIFY sftpChanged)
    Q_PROPERTY(QString activeTerminalWorkingDirectory READ activeTerminalWorkingDirectory NOTIFY sftpChanged)
    Q_PROPERTY(QVariantList transferTasks READ transferTasks NOTIFY transferTasksChanged)
    Q_PROPERTY(QVariantList transferBatches READ transferBatches NOTIFY transferTasksChanged)
    Q_PROPERTY(int activeTransferCount READ activeTransferCount NOTIFY transferTasksChanged)
    Q_PROPERTY(QString activeTerminalTabId READ activeTerminalTabId NOTIFY activeTerminalTabChanged)
    Q_PROPERTY(QVariantMap activeTerminalWorkspace READ activeTerminalWorkspace NOTIFY terminalWorkspaceChanged)
    Q_PROPERTY(QVariantMap activeRemoteTelemetry READ activeRemoteTelemetry NOTIFY remoteTelemetryChanged)
    Q_PROPERTY(QString terminalSearchQuery READ terminalSearchQuery NOTIFY terminalSearchChanged)
    Q_PROPERTY(int terminalSearchCurrent READ terminalSearchCurrent NOTIFY terminalSearchChanged)
    Q_PROPERTY(int terminalSearchTotal READ terminalSearchTotal NOTIFY terminalSearchChanged)
    Q_PROPERTY(bool terminalSearchCaseSensitive READ terminalSearchCaseSensitive NOTIFY terminalSearchChanged)
    Q_PROPERTY(QString themePreference READ themePreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(qreal backdropOpacity READ backdropOpacity NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString backdropPreference READ backdropPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString accentPreference READ accentPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString customAccent READ customAccent NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString uiFontFamily READ uiFontFamily NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString terminalFontFamily READ terminalFontFamily NOTIFY applicationSettingsChanged)
    Q_PROPERTY(int terminalFontSize READ terminalFontSize NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool showAllTerminalFonts READ showAllTerminalFonts NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool terminalLigatures READ terminalLigatures NOTIFY applicationSettingsChanged)
    Q_PROPERTY(qreal terminalBackgroundOpacity READ terminalBackgroundOpacity NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString cursorPreference READ cursorPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool cursorBlink READ cursorBlink NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool copyOnSelect READ copyOnSelect NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool confirmMultilinePaste READ confirmMultilinePaste NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool sftpShowHiddenFiles READ sftpShowHiddenFiles NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool sftpConfirmDelete READ sftpConfirmDelete NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString languagePreference READ languagePreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString aiProviderPreference READ aiProviderPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString aiBaseUrl READ aiBaseUrl NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString aiEndpointPath READ aiEndpointPath NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString aiModel READ aiModel NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QStringList aiAvailableModels READ aiAvailableModels NOTIFY aiModelsChanged)
    Q_PROPERTY(bool aiModelsLoading READ aiModelsLoading NOTIFY aiModelsChanged)
    Q_PROPERTY(QString aiModelsError READ aiModelsError NOTIFY aiModelsChanged)
    Q_PROPERTY(bool aiAutomaticContext READ aiAutomaticContext NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString aiPermissionPreference READ aiPermissionPreference NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool aiConversationHistoryEnabled READ aiConversationHistoryEnabled NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool aiDebugTraceEnabled READ aiDebugTraceEnabled NOTIFY applicationSettingsChanged)
    Q_PROPERTY(QString aiDebugTracePath READ aiDebugTracePath NOTIFY applicationSettingsChanged)
    Q_PROPERTY(bool aiApiKeyConfigured READ aiApiKeyConfigured NOTIFY credentialVaultChanged)
    Q_PROPERTY(QObject *aiActivity READ aiActivity CONSTANT)
    Q_PROPERTY(QObject *aiConversationHistory READ aiConversationHistory CONSTANT)
    Q_PROPERTY(QObject *activeAiConversation READ activeAiConversation NOTIFY aiConversationChanged)
    Q_PROPERTY(QString activeAiState READ activeAiState NOTIFY aiConversationChanged)
    Q_PROPERTY(QString activeAiError READ activeAiError NOTIFY aiConversationChanged)
    Q_PROPERTY(QString activeAiControlOwner READ activeAiControlOwner NOTIFY aiConversationChanged)
    Q_PROPERTY(QString activeAiContextPreview READ activeAiContextPreview NOTIFY aiConversationChanged)
    Q_PROPERTY(QVariantList activeAiContextItems READ activeAiContextItems NOTIFY aiConversationChanged)
    Q_PROPERTY(QVariantMap activeAiToolApproval READ activeAiToolApproval NOTIFY aiConversationChanged)
    Q_PROPERTY(QVariantMap aiPrivacyDiagnostics READ aiPrivacyDiagnostics NOTIFY aiPrivacyDiagnosticsChanged)
    Q_PROPERTY(QVariantList mcpServers READ mcpServers NOTIFY mcpConfigurationChanged)
    Q_PROPERTY(QVariantList mcpTools READ mcpTools NOTIFY mcpConfigurationChanged)
    Q_PROPERTY(QString mcpOperationError READ mcpOperationError NOTIFY mcpConfigurationChanged)
    Q_PROPERTY(QString credentialStoragePreference READ credentialStoragePreference NOTIFY credentialVaultChanged)
    Q_PROPERTY(QString effectiveCredentialStorage READ effectiveCredentialStorage NOTIFY credentialVaultChanged)
    Q_PROPERTY(bool portableVaultInitialized READ portableVaultInitialized NOTIFY credentialVaultChanged)
    Q_PROPERTY(bool portableVaultLocked READ portableVaultLocked NOTIFY credentialVaultChanged)
    Q_PROPERTY(QString credentialOperationError READ credentialOperationError NOTIFY credentialVaultChanged)
    Q_PROPERTY(QVariantList portForwardingRules READ portForwardingRules NOTIFY portForwardingRulesChanged)
    Q_PROPERTY(QString portForwardingOperationError READ portForwardingOperationError NOTIFY portForwardingRulesChanged)

public:
    using LocalTerminalSessionFactory = std::function<std::unique_ptr<terminal::LocalTerminalSessionBackend>()>;

    explicit AppController(QObject *parent = nullptr);
    explicit AppController(const QString &profileStorePath, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, LocalTerminalSessionFactory localSessionFactory,
                  QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath,
                  LocalTerminalSessionFactory localSessionFactory, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QString credentialsPath,
                  config::StorageMode storageMode, QObject *parent = nullptr);
    AppController(QString profileStorePath, QString knownHostsPath, QString settingsPath, QString credentialsPath,
                  config::StorageMode storageMode, LocalTerminalSessionFactory localSessionFactory,
                  QObject *parent = nullptr);
    ~AppController() override;

    AppController(const AppController &) = delete;
    AppController &operator=(const AppController &) = delete;

    void attachTerminal(ui::TerminalItem *terminal);
    Q_INVOKABLE void attachTerminalViewport(const QString &paneId, QObject *viewport);
    Q_INVOKABLE void detachTerminalViewport(const QString &paneId, QObject *viewport);
    void shutdown() noexcept;
    Q_INVOKABLE void dismissStartupRecoveryNotice();

    [[nodiscard]] bool sshActive() const noexcept;
    [[nodiscard]] QString startupRecoveryNotice() const;
    [[nodiscard]] bool hostKeyPromptVisible() const noexcept;
    [[nodiscard]] QString hostKeyEndpoint() const;
    [[nodiscard]] QString hostKeyAlgorithm() const;
    [[nodiscard]] QString hostKeyFingerprint() const;
    [[nodiscard]] bool hostKeyChangedWarning() const noexcept;
    [[nodiscard]] QString defaultPrivateKeyPath() const;
    [[nodiscard]] QVariantList hostProfiles() const;
    [[nodiscard]] QVariantList recentHostProfiles() const;
    [[nodiscard]] QStringList hostProfileGroups() const;
    [[nodiscard]] QStringList collapsedHostSections() const;
    [[nodiscard]] QVariantList terminalTabs() const;
    [[nodiscard]] QVariantList quickCommands() const;
    [[nodiscard]] QString quickCommandOperationError() const;
    [[nodiscard]] QVariantList notes() const;
    [[nodiscard]] QVariantList noteSearchResults() const;
    [[nodiscard]] QString activeNotePath() const;
    [[nodiscard]] QString activeNoteContent() const;
    [[nodiscard]] bool activeNoteDirty() const noexcept;
    [[nodiscard]] QString noteSearchState() const;
    [[nodiscard]] QString noteOperationError() const;
    [[nodiscard]] QVariantList terminalHistory() const;
    [[nodiscard]] QVariantList terminalGlobalHistory() const;
    [[nodiscard]] QVariantList actions() const;
    [[nodiscard]] QString terminalHistoryState() const;
    [[nodiscard]] QString terminalHistoryError() const;
    [[nodiscard]] QObject *activeSftpDirectoryModel() const noexcept;
    [[nodiscard]] QString activeSftpPath() const;
    [[nodiscard]] QString activeSftpHomePath() const;
    [[nodiscard]] QVariantList recentSftpPaths() const;
    [[nodiscard]] QVariantList bookmarkedSftpPaths() const;
    [[nodiscard]] bool activeSftpPathBookmarked() const;
    [[nodiscard]] QString activeSftpState() const;
    [[nodiscard]] QString activeSftpError() const;
    [[nodiscard]] QString activeSftpViewMode() const;
    [[nodiscard]] QString activeSftpSortColumn() const;
    [[nodiscard]] bool activeSftpSortAscending() const noexcept;
    [[nodiscard]] bool activeSftpDirectoriesFirst() const noexcept;
    [[nodiscard]] bool activeSftpShowModifiedColumn() const noexcept;
    [[nodiscard]] bool activeSftpShowSizeColumn() const noexcept;
    [[nodiscard]] bool activeSftpShowTypeColumn() const noexcept;
    [[nodiscard]] QString activeSftpFilenameEncoding() const;
    [[nodiscard]] bool activeSftpFollowTerminalDirectory() const noexcept;
    [[nodiscard]] QString activeTerminalWorkingDirectory() const;
    [[nodiscard]] QVariantList transferTasks() const;
    [[nodiscard]] QVariantList transferBatches() const;
    [[nodiscard]] int activeTransferCount() const noexcept;
    [[nodiscard]] QString activeTerminalTabId() const;
    [[nodiscard]] QVariantMap activeTerminalWorkspace() const;
    [[nodiscard]] QVariantMap activeRemoteTelemetry() const;
    [[nodiscard]] QString terminalSearchQuery() const;
    [[nodiscard]] int terminalSearchCurrent() const noexcept;
    [[nodiscard]] int terminalSearchTotal() const noexcept;
    [[nodiscard]] bool terminalSearchCaseSensitive() const noexcept;
    [[nodiscard]] QString themePreference() const;
    [[nodiscard]] qreal backdropOpacity() const noexcept;
    [[nodiscard]] QString backdropPreference() const;
    [[nodiscard]] QString accentPreference() const;
    [[nodiscard]] QString customAccent() const;
    [[nodiscard]] QString uiFontFamily() const;
    [[nodiscard]] QString terminalFontFamily() const;
    [[nodiscard]] int terminalFontSize() const noexcept;
    [[nodiscard]] bool showAllTerminalFonts() const noexcept;
    [[nodiscard]] bool terminalLigatures() const noexcept;
    [[nodiscard]] qreal terminalBackgroundOpacity() const noexcept;
    [[nodiscard]] QString cursorPreference() const;
    [[nodiscard]] bool cursorBlink() const noexcept;
    [[nodiscard]] bool copyOnSelect() const noexcept;
    [[nodiscard]] bool confirmMultilinePaste() const noexcept;
    [[nodiscard]] bool sftpShowHiddenFiles() const noexcept;
    [[nodiscard]] bool sftpConfirmDelete() const noexcept;
    [[nodiscard]] QString languagePreference() const;
    [[nodiscard]] QString aiProviderPreference() const;
    [[nodiscard]] QString aiBaseUrl() const;
    [[nodiscard]] QString aiEndpointPath() const;
    [[nodiscard]] QString aiModel() const;
    [[nodiscard]] QStringList aiAvailableModels() const;
    [[nodiscard]] bool aiModelsLoading() const noexcept;
    [[nodiscard]] QString aiModelsError() const;
    [[nodiscard]] bool aiAutomaticContext() const noexcept;
    [[nodiscard]] QString aiPermissionPreference() const;
    [[nodiscard]] bool aiConversationHistoryEnabled() const noexcept;
    [[nodiscard]] bool aiDebugTraceEnabled() const noexcept;
    [[nodiscard]] QString aiDebugTracePath() const;
    [[nodiscard]] bool aiApiKeyConfigured() const;
    [[nodiscard]] QObject *aiActivity() noexcept;
    [[nodiscard]] QObject *aiConversationHistory() noexcept;
    [[nodiscard]] QObject *activeAiConversation() const noexcept;
    [[nodiscard]] QString activeAiState() const;
    [[nodiscard]] QString activeAiError() const;
    [[nodiscard]] QString activeAiControlOwner() const;
    [[nodiscard]] QString activeAiContextPreview() const;
    [[nodiscard]] QVariantList activeAiContextItems() const;
    [[nodiscard]] QVariantMap activeAiToolApproval() const;
    [[nodiscard]] QVariantMap aiPrivacyDiagnostics() const;
    [[nodiscard]] QVariantList mcpServers() const;
    [[nodiscard]] QVariantList mcpTools() const;
    [[nodiscard]] QString mcpOperationError() const;
    void retranslateUiState();
    [[nodiscard]] QString credentialStoragePreference() const;
    [[nodiscard]] QString effectiveCredentialStorage() const;
    [[nodiscard]] bool portableVaultInitialized() const noexcept;
    [[nodiscard]] bool portableVaultLocked() const noexcept;
    [[nodiscard]] QString credentialOperationError() const;
    [[nodiscard]] QVariantList portForwardingRules() const;
    [[nodiscard]] QString portForwardingOperationError() const;

    Q_INVOKABLE QString startLocalTerminal();
    Q_INVOKABLE bool activateTerminalTab(const QString &id);
    Q_INVOKABLE bool closeTerminalTab(const QString &id);
    Q_INVOKABLE bool activateTerminalPane(const QString &paneId);
    Q_INVOKABLE bool splitActiveTerminal(const QString &orientation, bool duplicateActive = false);
    Q_INVOKABLE bool closeActiveTerminalPane();
    Q_INVOKABLE bool focusRelativeTerminalPane(int offset);
    Q_INVOKABLE bool resizeActiveTerminalPane(qreal delta);
    Q_INVOKABLE bool setTerminalSplitRatio(const QString &splitNodeId, qreal ratio);
    Q_INVOKABLE bool swapActiveTerminalPane(int offset);
    Q_INVOKABLE void searchTerminal(const QString &query, bool backwards, bool caseSensitive);
    Q_INVOKABLE void clearTerminalSearch();
    Q_INVOKABLE bool toggleTerminalWorkbench(const QString &page);
    Q_INVOKABLE void closeTerminalWorkbench();
    Q_INVOKABLE void setTerminalWorkbenchWidth(qreal width);
    Q_INVOKABLE void moveTerminalWorkbench();
    Q_INVOKABLE void toggleTerminalComposer();
    Q_INVOKABLE void setTerminalComposerHeight(qreal height);
    Q_INVOKABLE bool copyActiveTerminalAddress();
    Q_INVOKABLE bool copyActiveSftpPath();
    Q_INVOKABLE bool insertTerminalCommand(const QString &command);
    Q_INVOKABLE bool runTerminalCommand(const QString &command);
    Q_INVOKABLE bool startTerminalLog(const QString &localFileUrl);
    Q_INVOKABLE void stopTerminalLog();
    Q_INVOKABLE bool setActiveKeywordHighlightEnabled(bool enabled);
    Q_INVOKABLE bool saveActiveKeywordHighlightRule(const QString &id, const QString &pattern,
                                                    const QString &foreground, const QString &background, bool enabled,
                                                    bool caseSensitive);
    Q_INVOKABLE bool deleteActiveKeywordHighlightRule(const QString &id);
    Q_INVOKABLE bool setActiveTerminalEncoding(const QString &encoding);
    Q_INVOKABLE bool setActiveTerminalAppearance(const QString &fontFamily, int fontSize, bool ligatures,
                                                 qreal backgroundOpacity, const QString &cursor,
                                                 const QString &foreground, const QString &background);
    Q_INVOKABLE bool resetActiveTerminalAppearance();
    Q_INVOKABLE bool startTerminalScriptRecording();
    Q_INVOKABLE bool pauseTerminalScriptRecording();
    Q_INVOKABLE bool resumeTerminalScriptRecording();
    Q_INVOKABLE bool stopTerminalScriptRecording();
    Q_INVOKABLE bool replayTerminalScriptRecording();
    Q_INVOKABLE bool copyTerminalScriptRecording();
    Q_INVOKABLE bool saveTerminalScriptRecordingAsScript(const QString &name, const QString &description = {});
    Q_INVOKABLE void clearTerminalScriptRecording();
    Q_INVOKABLE bool saveQuickCommand(const QString &id, const QString &name, const QString &command,
                                      const QString &description, const QString &shellScope);
    Q_INVOKABLE bool saveScript(const QVariantMap &script);
    [[nodiscard]] Q_INVOKABLE QVariantMap renderScript(const QString &id, const QVariantMap &values) const;
    Q_INVOKABLE bool runScript(const QString &id, const QVariantMap &values, const QString &targetSessionId);
    Q_INVOKABLE bool cancelScript(const QString &targetSessionId);
    Q_INVOKABLE bool deleteQuickCommand(const QString &id);
    Q_INVOKABLE bool moveQuickCommand(const QString &id, int targetIndex);
    Q_INVOKABLE bool importQuickCommands(const QString &localFileUrl);
    Q_INVOKABLE bool exportQuickCommands(const QString &localFileUrl);
    Q_INVOKABLE void refreshNotes();
    Q_INVOKABLE bool openNote(const QString &relativePath, bool discardUnsavedChanges = false);
    Q_INVOKABLE void updateActiveNoteContent(const QString &content);
    Q_INVOKABLE bool saveActiveNote();
    Q_INVOKABLE bool discardActiveNoteChanges();
    Q_INVOKABLE bool createNote(const QString &relativePath);
    Q_INVOKABLE bool createNoteFolder(const QString &relativePath);
    Q_INVOKABLE bool renameNoteEntry(const QString &sourceRelativePath, const QString &destinationRelativePath);
    Q_INVOKABLE bool deleteNoteEntry(const QString &relativePath);
    Q_INVOKABLE void searchNotes(const QString &query);
    Q_INVOKABLE bool importNote(const QString &localFileUrl, const QString &destinationFolder = {});
    Q_INVOKABLE bool exportActiveNote(const QString &localFileUrl);
    Q_INVOKABLE void refreshTerminalHistory();
    Q_INVOKABLE void setTerminalTelemetryVisible(bool visible);
    Q_INVOKABLE void refreshRemoteTelemetry();
    Q_INVOKABLE void refreshSftpDirectory();
    Q_INVOKABLE bool navigateSftpDirectory(const QString &remotePath);
    Q_INVOKABLE bool navigateSftpHome();
    Q_INVOKABLE bool navigateSftpParent();
    Q_INVOKABLE bool toggleActiveSftpBookmark();
    Q_INVOKABLE bool setSftpViewMode(const QString &mode);
    Q_INVOKABLE bool setSftpSort(const QString &column, bool ascending);
    Q_INVOKABLE bool setSftpDirectoriesFirst(bool enabled);
    Q_INVOKABLE bool setSftpVisibleColumns(bool modified, bool size, bool type);
    Q_INVOKABLE bool setSftpFilenameEncoding(const QString &encoding);
    Q_INVOKABLE bool navigateSftpToTerminalDirectory();
    Q_INVOKABLE bool setSftpFollowTerminalDirectory(bool enabled);
    Q_INVOKABLE bool createSftpDirectory(const QString &name);
    Q_INVOKABLE bool createSftpFile(const QString &name);
    Q_INVOKABLE bool renameSftpEntry(const QString &remotePath, const QString &newName);
    Q_INVOKABLE bool removeSftpEntry(const QString &remotePath, bool directory);
    Q_INVOKABLE bool enqueueSftpDownload(const QString &remotePath, const QString &localFileUrl, qulonglong totalBytes,
                                         qlonglong modifiedUtcSeconds = -1);
    Q_INVOKABLE bool enqueueSftpUpload(const QString &localFileUrl);
    Q_INVOKABLE bool enqueueSftpUploadBatch(const QStringList &localFileUrls);
    Q_INVOKABLE bool enqueueSftpDownloadBatch(const QStringList &remotePaths, const QString &localDirectoryUrl);
    Q_INVOKABLE void cancelTransferBatch(const QString &batchId);
    Q_INVOKABLE void pauseTransferBatch(const QString &batchId);
    Q_INVOKABLE void resumeTransferBatch(const QString &batchId);
    Q_INVOKABLE void retryTransferBatch(const QString &batchId);
    Q_INVOKABLE void dismissTransferBatch(const QString &batchId);
    Q_INVOKABLE void cancelTransfer(const QString &taskId);
    Q_INVOKABLE void pauseTransfer(const QString &taskId);
    Q_INVOKABLE void resumeTransfer(const QString &taskId);
    Q_INVOKABLE void retryTransfer(const QString &taskId);
    Q_INVOKABLE void pauseAllTransfers();
    Q_INVOKABLE void resumeAllTransfers();
    Q_INVOKABLE void cancelAllTransfers();
    Q_INVOKABLE bool copyTransferPath(const QString &taskId);
    Q_INVOKABLE bool openTransferTarget(const QString &taskId);
    Q_INVOKABLE void dismissTransfer(const QString &taskId);
    Q_INVOKABLE void clearFinishedTransfers();
    Q_INVOKABLE void resolveTransferConflict(const QString &taskId, const QString &action,
                                             const QString &renamedDestinationPath = {},
                                             bool applyToRemainingBatch = false);
    Q_INVOKABLE bool triggerAction(const QString &actionId);
    [[nodiscard]] Q_INVOKABLE QVariantMap setActionShortcut(const QString &actionId, const QString &shortcut);
    [[nodiscard]] Q_INVOKABLE QVariantMap setActionShortcutFromKey(const QString &actionId, int key, int modifiers);
    Q_INVOKABLE bool resetActionShortcut(const QString &actionId);
    Q_INVOKABLE bool resetAllActionShortcuts();
    Q_INVOKABLE bool connectPrivateKey(const QString &host, int port, const QString &username,
                                       const QString &privateKeyPath, const QString &passphrase);
    Q_INVOKABLE bool connectPassword(const QString &host, int port, const QString &username, const QString &password);
    Q_INVOKABLE bool saveHostProfile(const QString &id, const QString &name, const QString &host, int port,
                                     const QString &username, const QString &authentication,
                                     const QString &privateKeyPath, bool privateKeyPassphraseRequired,
                                     const QString &group);
    Q_INVOKABLE bool
    saveHostProfileWithCredential(const QString &id, const QString &name, const QString &host, int port,
                                  const QString &username, const QString &authentication, const QString &privateKeyPath,
                                  bool privateKeyPassphraseRequired, const QString &group, const QString &secret,
                                  bool rememberCredential, const QVariantMap &sessionOptions = {},
                                  const QVariantMap &proxyOptions = {}, const QString &proxySecret = {},
                                  bool rememberProxyCredential = false, const QVariantMap &routeOptions = {});
    Q_INVOKABLE bool saveAndConnectHostProfile(const QString &id, const QString &name, const QString &host, int port,
                                               const QString &username, const QString &authentication,
                                               const QString &privateKeyPath, bool privateKeyPassphraseRequired,
                                               const QString &group, const QString &secret, bool rememberCredential,
                                               const QVariantMap &sessionOptions = {},
                                               const QVariantMap &proxyOptions = {}, const QString &proxySecret = {},
                                               bool rememberProxyCredential = false,
                                               const QVariantMap &routeOptions = {});
    Q_INVOKABLE bool duplicateHostProfile(const QString &id);
    Q_INVOKABLE bool deleteHostProfile(const QString &id);
    Q_INVOKABLE bool clearRecentHostProfiles();
    Q_INVOKABLE bool setHostSectionCollapsed(const QString &sectionId, bool collapsed);
    Q_INVOKABLE bool forgetHostCredential(const QString &id);
    Q_INVOKABLE bool saveHostCredential(const QString &id, const QString &secret);
    Q_INVOKABLE bool saveProxyCredential(const QString &id, const QString &secret);
    [[nodiscard]] Q_INVOKABLE QString readHostCredential(const QString &id);
    [[nodiscard]] Q_INVOKABLE QString readProxyCredential(const QString &id);
    Q_INVOKABLE bool connectHostProfile(const QString &id, const QString &secret, const QString &proxySecret = {});
    Q_INVOKABLE bool reconnectTerminalTab(const QString &id);
    Q_INVOKABLE bool cancelTerminalReconnect(const QString &id);
    [[nodiscard]] Q_INVOKABLE QVariantMap parseQuickConnectTarget(const QString &target) const;
    Q_INVOKABLE bool connectQuick(const QString &target, const QString &authentication, const QString &privateKeyPath,
                                  bool privateKeyPassphraseRequired, const QString &secret, bool saveProfile,
                                  const QString &profileName, const QString &group);
    Q_INVOKABLE bool saveApplicationSettings(const QString &theme, qreal backdropOpacity, const QString &backdrop,
                                             const QString &accent, const QString &customAccent,
                                             const QString &uiFontFamily, const QString &fontFamily, int fontSize,
                                             bool showAllFonts, bool ligatures, qreal terminalBackgroundOpacity,
                                             const QString &cursor, bool cursorShouldBlink, bool shouldCopyOnSelect,
                                             bool shouldConfirmMultilinePaste, const QString &language,
                                             bool shouldShowHiddenSftpFiles, bool shouldConfirmSftpDelete);
    Q_INVOKABLE bool saveAiProviderSettings(const QString &provider, const QString &baseUrl,
                                            const QString &endpointPath, const QString &model, bool automaticContext,
                                            const QString &permissionMode);
    Q_INVOKABLE bool saveAiProviderConfiguration(const QString &provider, const QString &baseUrl, const QString &model,
                                                 bool automaticContext, const QString &permissionMode,
                                                 const QString &apiKey, bool debugTraceEnabled);
    Q_INVOKABLE void refreshAiModels(const QString &provider, const QString &baseUrl, const QString &apiKey);
    [[nodiscard]] Q_INVOKABLE QString aiProviderEndpointPreview(const QString &provider, const QString &baseUrl) const;
    Q_INVOKABLE bool saveAiApiKey(const QString &apiKey);
    Q_INVOKABLE bool removeAiApiKey();
    Q_INVOKABLE bool saveMcpServer(const QString &id, const QString &nameSpace, const QString &program,
                                   const QStringList &arguments, const QString &workingDirectory, const QString &trust,
                                   bool enabled);
    Q_INVOKABLE bool removeMcpServer(const QString &id);
    Q_INVOKABLE bool restartMcpServer(const QString &id);
    Q_INVOKABLE bool setMcpToolApproved(const QString &serverId, const QString &exposedName,
                                        const QString &schemaDigest, bool approved);
    Q_INVOKABLE bool setAiConversationHistoryEnabled(bool enabled);
    Q_INVOKABLE bool restoreAiConversationHistory(const QString &conversationId);
    Q_INVOKABLE bool sendAiMessage(const QString &prompt);
    Q_INVOKABLE bool sendAiCommandRequest(const QString &prompt);
    Q_INVOKABLE bool setAiPermissionMode(const QString &mode);
    Q_INVOKABLE bool explainAiLastFailure();
    Q_INVOKABLE bool cancelAiMessage();
    Q_INVOKABLE bool approveAiTool();
    Q_INVOKABLE bool denyAiTool();
    Q_INVOKABLE bool takeAiControl();
    Q_INVOKABLE bool resumeAiAgentControl();
    Q_INVOKABLE bool retryAiMessage();
    Q_INVOKABLE void clearAiConversation();
    Q_INVOKABLE void clearAiActivity();
    Q_INVOKABLE [[nodiscard]] bool exportAiActivity(const QString &localFileUrl) const;
    Q_INVOKABLE bool copyAiText(const QString &text);
    Q_INVOKABLE bool attachAiSelection();
    Q_INVOKABLE bool removeAiContextItem(const QString &itemId);
    Q_INVOKABLE bool setAiContextItemPinned(const QString &itemId, bool pinned);
    Q_INVOKABLE void resetAiContextItems();
    Q_INVOKABLE bool resetApplicationSettings();
    Q_INVOKABLE bool initializePortableCredentialVault(const QString &masterPassword);
    Q_INVOKABLE bool unlockPortableCredentialVault(const QString &masterPassword);
    Q_INVOKABLE bool changePortableVaultMasterPassword(const QString &masterPassword);
    Q_INVOKABLE void lockPortableCredentialVault();
    Q_INVOKABLE bool migrateCredentialStorage(const QString &target, bool removeSource);
    Q_INVOKABLE bool removeAllSavedCredentials();
    Q_INVOKABLE bool clearCredentialStorage(const QString &target);
    Q_INVOKABLE bool savePortForwardingRule(const QString &id, const QString &label, const QString &profileId,
                                            const QString &type, const QString &bindHost, int bindPort,
                                            const QString &destinationHost, int destinationPort, bool autoStart);
    Q_INVOKABLE bool duplicatePortForwardingRule(const QString &id);
    Q_INVOKABLE bool copyPortForwardingBindEndpoint(const QString &id);
    Q_INVOKABLE bool deletePortForwardingRule(const QString &id);
    Q_INVOKABLE bool startPortForwardingRule(const QString &id);
    Q_INVOKABLE void stopPortForwardingRule(const QString &id);
    Q_INVOKABLE void acceptHostKey(bool remember);
    Q_INVOKABLE void rejectHostKey();

signals:
    void sshActiveChanged();
    void hostKeyPromptChanged();
    void hostProfilesChanged();
    void hostWorkspaceChanged();
    void terminalTabsChanged();
    void quickCommandsChanged();
    void notesChanged();
    void terminalHistoryChanged();
    void sftpChanged();
    void transferTasksChanged();
    void transferNotificationRequested(const QVariantMap &notification);
    void transferConflictRequested(const QString &taskId, const QVariantMap &conflict);
    void actionRegistryChanged();
    void actionRequested(const QString &actionId);
    void activeTerminalTabChanged();
    void terminalWorkspaceChanged();
    void terminalSearchChanged();
    void remoteTelemetryChanged();
    void applicationSettingsChanged();
    void credentialVaultChanged();
    void aiConversationChanged();
    void aiModelsChanged();
    void aiPrivacyDiagnosticsChanged();
    void mcpConfigurationChanged();
    void portForwardingRulesChanged();
    void startupRecoveryNoticeChanged();

private:
    Q_SIGNAL void terminalHistoryTaskCompleted(const QString &tabId, quint64 requestId, ShellHistoryEntries entries,
                                               const QString &error);
    Q_SIGNAL void noteSearchTaskCompleted(quint64 requestId, NoteSearchResults results, const QString &error);
    Q_SIGNAL void aiNoteReadTaskCompleted(const QString &tabId, quint64 requestId, quint64 generation,
                                          const QString &relativePath, const QByteArray &outputJson);
    Q_SIGNAL void scriptOutputObserved(const QString &tabId, const QByteArray &bytes);
    Q_SIGNAL void portForwardingSnapshotReady(const QString &ruleId, int state, int failure, qulonglong activeClients,
                                              qulonglong bytesFromClients, qulonglong bytesToClients,
                                              qulonglong rejectedClients);
    Q_SIGNAL void portForwardingHostKeyPromptRequested(const QString &ruleId, const QString &endpoint,
                                                       const QString &algorithm, const QString &fingerprint,
                                                       bool changed);

    enum class TerminalTabKind : std::uint8_t
    {
        Local,
        Ssh,
    };

    struct TerminalTab final
    {
        struct PendingAiSftpRead final
        {
            quint64 requestId = 0;
            ai::AiToolCall call;
            ai::AiSftpReadRequest request;
        };

        struct PendingAiSftpList final
        {
            quint64 requestId = 0;
            ai::AiToolCall call;
            ai::AiSftpListRequest request;
        };

        struct PendingAiNoteRead final
        {
            quint64 requestId = 0;
            ai::AiToolCall call;
            ai::AiNoteReadRequest request;
        };

        struct PendingAiMcpCall final
        {
            ai::AiToolCall call;
            ai::AiToolDispatchKey dispatchKey;
            ai::McpRegisteredTool tool;
            std::optional<ai::McpCallHandle> activeCall;
        };

        std::unique_ptr<terminal::LocalTerminalSessionBackend> local;
        std::unique_ptr<ssh::SshTerminalSession> ssh;
        std::unique_ptr<sftp::SftpSession> sftpSession;
        std::unique_ptr<sftp::SftpDirectoryModel> sftpModel;
        std::unique_ptr<ai::AiConversationModel> aiConversation;
        std::unique_ptr<ai::AiTurnRunner> aiTurnRunner;
        std::unique_ptr<ai::AiAgentTurnBudget> aiTurnBudget;
        std::optional<ai::AiTerminalAction> pendingAiAction;
        std::optional<PendingAiSftpRead> pendingAiSftpRead;
        std::optional<PendingAiSftpList> pendingAiSftpList;
        std::optional<PendingAiNoteRead> pendingAiNoteRead;
        std::optional<PendingAiMcpCall> pendingAiMcpCall;
        qint64 connectedUtcMs = 0;
        qreal sessionBackgroundOpacity = -1.0;
        qint64 recordingStartedUtcMs = 0;
        std::uint64_t scriptPlaybackGeneration = 0;
        std::uint64_t reconnectGeneration = 0;
        std::uint64_t historyRequestId = 0;
        std::uint64_t sftpRequestId = 0;
        std::uint64_t aiSftpReadRequestId = 0;
        std::uint64_t aiSftpListRequestId = 0;
        std::uint64_t aiNoteReadRequestId = 0;
        std::uint64_t sftpGeneration = 0;
        std::uint64_t sftpTreeRequestId = 0;
        qreal workbenchWidth = 520.0;
        qreal composerHeight = 132.0;
        terminal::TerminalSnapshotPtr snapshot;
        std::shared_ptr<logging::SessionLogWriter> sessionLog;
        std::shared_ptr<terminal::TerminalOutputSink> outputSink;
        std::shared_ptr<terminal::SemanticTerminalObserver> semanticObserver;
        std::shared_ptr<ai::AiTerminalFrameTracker> aiFrameTracker;
        QString id;
        QString workspaceId;
        QString paneId;
        QString title;
        QString status;
        QString searchQuery;
        QString sourceProfileId;
        QString identity;
        QString address;
        QString terminalEncoding = QStringLiteral("utf-8");
        QString sessionFontFamily;
        QString sessionCursor;
        QString sessionForeground;
        QString sessionBackground;
        QString workbenchPage = QStringLiteral("history");
        QString workbenchSide = QStringLiteral("left");
        QString historyState = QStringLiteral("idle");
        QString historyError;
        QString sftpPath = QStringLiteral("/");
        QString sftpRequestedPath = QStringLiteral("/");
        QString sftpHomePath;
        QString sftpState = QStringLiteral("idle");
        QString sftpError;
        QString sftpViewMode = QStringLiteral("list");
        QString sftpSortColumn = QStringLiteral("name");
        QString sftpFilenameEncoding = QStringLiteral("utf-8");
        QString terminalWorkingDirectory;
        QByteArray inputHistoryBuffer;
        QString telemetryState = QStringLiteral("paused");
        QString aiState = QStringLiteral("idle");
        QString aiError;
        QString aiLastPrompt;
        QString aiContextPreview;
        QString aiConversationId;
        QVariantList aiContextItems;
        std::unordered_set<std::string> aiExcludedContextIds;
        std::unordered_set<std::string> aiPinnedContextIds;
        std::vector<ai::AiExplicitContext> aiExplicitContextItems;
        std::vector<ssh::SshKeywordHighlightRule> keywordHighlightRules;
        std::vector<workbench::ShellHistoryEntry> history;
        std::vector<workbench::ShellHistoryEntry> capturedHistory;
        std::deque<telemetry::Sample> telemetryHistory;
        workbench::ScriptRecorder scriptRecorder;
        workbench::ScriptExecution scriptExecution;
        std::optional<telemetry::Sample> telemetrySample;
        std::optional<ai::AiTokenUsage> aiUsage;
        std::uint64_t aiAssistantMessageId = 0;
        std::uint32_t searchCurrent = 0;
        std::uint32_t searchTotal = 0;
        std::uint8_t reconnectAttempt = 0;
        int sessionFontSize = 0;
        TerminalTabKind kind = TerminalTabKind::Local;
        ssh::SshConnectionPhase sshPhase = ssh::SshConnectionPhase::Disconnected;
        bool searchCaseSensitive = false;
        bool keywordHighlightEnabled = true;
        bool sessionLigatures = true;
        bool scriptPlaybackActive = false;
        bool followTerminalDirectory = false;
        bool sftpSortAscending = true;
        bool sftpDirectoriesFirst = true;
        bool sftpShowModifiedColumn = true;
        bool sftpShowSizeColumn = true;
        bool sftpShowTypeColumn = false;
        bool sftpHasListing = false;
        bool inputHistoryBufferReliable = true;
        bool aiLastPreferFailure = false;
        bool aiLastCommandRequest = false;
        bool aiFirstWriteApproved = false;
        bool workbenchOpen = false;
        bool composerOpen = false;
        bool running = false;
        bool recentConnectionRecorded = false;
        bool reconnectPending = false;
        std::optional<ssh::SshFailureKind> sshFailure;
    };

    struct PortForwardingRuntime final
    {
        std::string ruleId;
        std::unique_ptr<forwarding::PortForwardingJob> job;
        std::mutex hostKeyMutex;
        std::condition_variable hostKeyAvailable;
        std::optional<ssh::UnknownHostKeyDecision> hostKeyDecision;
        bool awaitingHostKey = false;
    };

    void connectTerminalSignals(ui::TerminalItem &terminal, const QString &paneId);
    void connectLocalTabSignals(TerminalTab &tab);
    void connectSshTabSignals(TerminalTab &tab);
    void connectSftpTabSignals(TerminalTab &tab);
    void initializeSessionLog(TerminalTab &tab);
    void initializeTerminalOutputSink(TerminalTab &tab);
    void initializeAiRuntime(TerminalTab &tab);
    void initializeAiPrivacySignals();
    void initializeAiDebugTrace();
    void configureAiDebugTrace();
    void appendAiDebugTrace(const QString &event, const QJsonObject &payload = {});
    void initializeAiConversationHistory();
    void persistAiConversation(const TerminalTab &tab);
    [[nodiscard]] ai::AiContextBundle buildAiContext(TerminalTab &tab, bool preferLastFailure);
    [[nodiscard]] ai::AiTerminalReadSnapshot aiReadSnapshot(const TerminalTab &tab) const;
    [[nodiscard]] std::vector<ai::AiTerminalReadSnapshot> aiReadSnapshots(const TerminalTab &tab) const;
    void acceptAiSelectedText(TerminalTab &tab, const QString &text);
    [[nodiscard]] bool sendAiMessage(TerminalTab &tab, const QString &prompt, bool preferLastFailure,
                                     bool appendPrompt = true, bool commandRequest = false);
    [[nodiscard]] ai::AiTurnRunner::ToolHandlingResult handleAiWaitCommand(TerminalTab &tab, const QString &tabId,
                                                                           const ai::AiToolCall &call);
    [[nodiscard]] ai::AiTurnRunner::ToolHandlingResult
    handleAiTerminalFrameTool(TerminalTab &owner, const QString &ownerTabId, const ai::AiToolCall &call,
                              std::span<const ai::AiTerminalReadSnapshot> allowedTargets);
    [[nodiscard]] std::string executeAiTerminalAction(TerminalTab &tab, const ai::AiTerminalAction &action);
    [[nodiscard]] std::string executeAiRunCommand(TerminalTab &tab, const ai::AiTerminalAction &action);
    [[nodiscard]] std::string executeAiWriteToPty(TerminalTab &tab, const ai::AiTerminalAction &action);
    [[nodiscard]] std::string executeAiInterruptCommand(TerminalTab &tab, const ai::AiTerminalAction &action);
    [[nodiscard]] std::string executeAiTransferControl(const ai::AiTerminalAction &action);
    [[nodiscard]] std::string executeAiSaveRunbook(const ai::AiTerminalAction &action);
    [[nodiscard]] std::string executeAiSftpTransfer(TerminalTab &tab, const ai::AiTerminalAction &action);
    [[nodiscard]] bool approveAiMcpTool(TerminalTab &tab);
    [[nodiscard]] bool denyAiMcpTool(TerminalTab &tab);
    void completeAiMcpTool(const QString &tabId, std::uint64_t generation, const ai::AiToolDispatchKey &dispatchKey,
                           const ai::AiToolCall &call, std::expected<std::string, QString> result);
    void recordAiActivity(const TerminalTab &tab, const ai::AiToolCall &call, const QString &state,
                          const QString &resultCode, bool sideEffecting, bool highRisk = false);
    [[nodiscard]] bool handoffAiControlToUser(TerminalTab &tab, bool cancelTurn);
    void observeScriptOutput(const QString &tabId, const QByteArray &bytes);
    void dispatchScriptCommands(TerminalTab &tab, const std::vector<std::string> &commands);
    void initializeScriptExecutionTimer();
    void queueInput(const QByteArray &bytes);
    void queuePaste(const QByteArray &bytes);
    void dispatchInput(TerminalTab &tab, const QByteArray &bytes);
    void dispatchPaste(TerminalTab &tab, const QByteArray &bytes);
    void observeTerminalInput(TerminalTab &tab, const QByteArray &bytes);
    void appendCapturedHistory(TerminalTab &tab, const QString &command);
    void requestResize(quint16 columns, quint16 rows, quint32 cellWidthPixels, quint32 cellHeightPixels);
    void requestScroll(int rows);
    void requestSelection(quint16 startColumn, quint16 startRow, quint16 endColumn, quint16 endRow, bool rectangular);
    void clearSelection();
    void copySelection();
    void setHostKeyPrompt(QString endpoint, QString algorithm, QString fingerprint, bool changed);
    void clearHostKeyPrompt();
    void recordPersistenceRecovery();
    void loadHostProfiles();
    void loadPortForwardingRules();
    void initializePortForwardingSignalBridges();
    [[nodiscard]] bool persistPortForwardingRules(const std::vector<forwarding::PortForwardingRule> &rules);
    void applyPortForwardingSnapshot(const std::string &ruleId, const forwarding::PortForwardingJobSnapshot &snapshot);
    [[nodiscard]] PortForwardingRuntime *findPortForwardingRuntime(std::string_view ruleId) noexcept;
    [[nodiscard]] const PortForwardingRuntime *findPortForwardingRuntime(std::string_view ruleId) const noexcept;
    void resolvePortForwardingHostKey(PortForwardingRuntime &runtime, ssh::UnknownHostKeyDecision decision) noexcept;
    void stopAllPortForwardingRules() noexcept;
    void loadApplicationSettings();
    void loadQuickCommands();
    void loadWorkspaceState();
    void restoreTerminalWorkspaces();
    void initializeActionRegistry();
    [[nodiscard]] QVariantMap shortcutResult(const actions::ShortcutValidation &validation) const;
    void applyTerminalHistoryTaskResult(const QString &tabId, quint64 requestId, ShellHistoryEntries entries,
                                        const QString &error);
    void applyNoteSearchTaskResult(quint64 requestId, const NoteSearchResults &results, const QString &error);
    void applyAiNoteReadTaskResult(const QString &tabId, quint64 requestId, quint64 generation,
                                   const QString &relativePath, const QByteArray &outputJson);
    void setNoteOperationError(QString message);
    void setQuickCommandOperationError(QString message);
    [[nodiscard]] bool persistApplicationSettings(const config::ApplicationSettings &settings);
    [[nodiscard]] bool saveHostProfileInternal(const QString &id, const QString &name, const QString &host, int port,
                                               const QString &username, const QString &authentication,
                                               const QString &privateKeyPath, bool privateKeyPassphraseRequired,
                                               const QString &group, const QString &secret, bool rememberCredential,
                                               bool manageCredential, const QVariantMap &sessionOptions = {},
                                               const QVariantMap &proxyOptions = {}, const QString &proxySecret = {},
                                               bool rememberProxyCredential = false, bool manageProxyCredential = false,
                                               const QVariantMap &routeOptions = {});
    void setCredentialOperationError(QString message);
    [[nodiscard]] bool startSshConnection(ssh::SshConnectionRequest request, QString sourceProfileId = {});
    [[nodiscard]] std::optional<ssh::SshConnectionRequest> connectionRequestForProfile(const ssh::SshProfile &profile,
                                                                                       const QString &secret = {},
                                                                                       const QString &proxySecret = {});
    void scheduleSshReconnect(TerminalTab &tab, ssh::SshFailureKind failure);
    void attemptSshReconnect(const QString &tabId, std::uint64_t generation);
    [[nodiscard]] bool startSftpSession(TerminalTab &tab);
    [[nodiscard]] std::expected<ssh::SshConnectionRequest, sftp::TransferCredentialError>
    sftpConnectionRequest(const TerminalTab &tab);
    [[nodiscard]] sftp::TransferRequestProvider transferRequestProvider(const QString &profileId);
    void initializeTransferManager();
    void applyTransferSnapshot(const sftp::TransferTasksPtr &tasks);
    void applyTransferBatchSnapshot(const sftp::TransferBatchesPtr &batches);
    void stopSftpSession(TerminalTab &tab);
    void deferSftpSessionStop(std::unique_ptr<sftp::SftpSession> session);
    void reapStoppedSftpSession(sftp::SftpSession *session);
    void requestSftpDirectory(TerminalTab &tab, const QString &remotePath);
    void applyWorkspaceState(TerminalTab &tab) const;
    void persistWorkspaceState(const TerminalTab &tab, bool recordRemotePath = false);
    void recordRecentConnection(TerminalTab &tab);
    [[nodiscard]] TerminalTab *activeTab();
    [[nodiscard]] const TerminalTab *activeTab() const;
    [[nodiscard]] TerminalTab *findTab(const QString &id);
    [[nodiscard]] const TerminalTab *findTab(const QString &id) const;
    [[nodiscard]] TerminalTab *findTabForPane(const QString &paneId);
    [[nodiscard]] const TerminalTab *findTabForPane(const QString &paneId) const;
    [[nodiscard]] workbench::TerminalWorkspaceLayout *findTerminalWorkspace(const QString &id);
    [[nodiscard]] const workbench::TerminalWorkspaceLayout *findTerminalWorkspace(const QString &id) const;
    [[nodiscard]] QString firstTabIdForWorkspace(const workbench::TerminalWorkspaceLayout &workspace) const;
    [[nodiscard]] QVariantMap terminalTabValue(const TerminalTab &tab, const QString &publicId) const;
    [[nodiscard]] QVariantMap terminalLayoutNodeValue(const workbench::TerminalWorkspaceLayout &workspace,
                                                      std::string_view nodeId) const;
    [[nodiscard]] bool persistTerminalWorkspaces();
    [[nodiscard]] bool saveWorkspaceStateCandidate(const workbench::WorkspaceState &candidate);
    void emitActiveTerminalContextChanged();
    void showTabInViewport(const TerminalTab &tab);
    void showAllTerminalViewports();
    void showActiveTab();
    [[nodiscard]] QVariantList keywordRulesVariant(const TerminalTab &tab) const;
    [[nodiscard]] bool persistKeywordRules(TerminalTab &tab);
    [[nodiscard]] QVariantList recordedScriptStepsVariant(const TerminalTab &tab) const;
    void replayRecordedScriptStep(const QString &tabId, std::size_t index, std::uint64_t generation);
    void updateTelemetryVisibility();

    static constexpr std::size_t maximumTerminalTabs = 32;

    ui::TerminalItem *m_terminal = nullptr;
    QHash<QString, QPointer<ui::TerminalItem>> m_terminalViewports;
    LocalTerminalSessionFactory m_localSessionFactory;
    std::unique_ptr<windowing::WindowsProtectedClipboard> m_aiClipboard;
    ssh::SshProfileStore m_profileStore;
    forwarding::PortForwardingRuleStore m_portForwardingStore;
    config::ApplicationSettingsStore m_settingsStore;
    config::ApplicationSettings m_settings;
    QString m_startupRecoveryNotice;
    actions::ActionRegistry m_actionRegistry;
    workbench::ScriptStore m_scriptStore;
    QString m_legacyQuickCommandPath;
    workbench::NoteStore m_noteStore;
    workbench::WorkspaceStateStore m_workspaceStateStore;
    ai::AiActivityModel m_aiActivity;
    ai::McpRuntimeManager m_mcpRuntime;
    workbench::WorkspaceState m_workspaceState;
    std::vector<workbench::ScriptDefinition> m_scripts;
    QString m_quickCommandOperationError;
    QVariantList m_notes;
    QVariantList m_noteSearchResults;
    QString m_activeNotePath;
    QString m_activeNoteContent;
    QString m_noteSearchState = QStringLiteral("idle");
    QString m_noteOperationError;
    std::uint64_t m_noteSearchRequestId = 0;
    bool m_activeNoteDirty = false;
    std::unique_ptr<security::CredentialVaultCoordinator> m_credentialVaults;
    std::unique_ptr<ai::AiConversationHistoryModel> m_aiConversationHistory;
    std::unique_ptr<sftp::TransferManager> m_transferManager;
    std::unique_ptr<sftp::TransferBatchCoordinator> m_transferBatchCoordinator;
    QVariantList m_transferTasks;
    QVariantList m_transferBatches;
    security::CredentialStorage m_defaultCredentialStorage = security::CredentialStorage::Session;
    QString m_credentialOperationError;
    QString m_knownHostsPath;
    std::vector<ssh::SshProfile> m_profiles;
    std::vector<forwarding::PortForwardingRule> m_portForwardingRules;
    std::vector<std::unique_ptr<PortForwardingRuntime>> m_portForwardingRuntimes;
    QString m_portForwardingOperationError;
    ai::ProviderHttpClient m_aiProviderClient;
    std::shared_ptr<logging::SessionLogWriter> m_aiDebugTrace;
    QString m_aiDebugTracePath;
    QNetworkAccessManager m_aiModelNetwork;
    QPointer<QNetworkReply> m_aiModelsReply;
    QStringList m_aiAvailableModels;
    QString m_aiModelsError;
    quint64 m_aiModelsRequestGeneration = 0;
    bool m_aiModelsLoading = false;
    ai::AiContextBroker m_aiContextBroker;
    ai::AiCommandTracker m_aiCommandTracker;
    ai::AiActionToolDispatcher m_aiActionToolDispatcher;
    ai::AiReadToolDispatcher m_aiReadToolDispatcher;
    ai::AiToolDispatchLedger m_mcpDispatchLedger;
    std::vector<std::unique_ptr<TerminalTab>> m_tabs;
    std::vector<std::unique_ptr<sftp::SftpSession>> m_stoppingSftpSessions;
    QString m_activeTabId;
    QString m_focusedTabId;
    QString m_hostKeyTabId;
    QString m_hostKeyEndpoint;
    QString m_hostKeyAlgorithm;
    QString m_hostKeyFingerprint;
    std::uint32_t m_nextLocalTabNumber = 1;
    bool m_hostKeyPromptVisible = false;
    bool m_hostKeyChangedWarning = false;
    bool m_hostKeyForSftp = false;
    bool m_shutdownStarted = false;
    bool m_terminalTelemetryVisible = false;
    QTimer m_scriptExecutionTimer;
    QString m_hostKeyTransferTaskId;
    QString m_hostKeyForwardingRuleId;
};

} // namespace ztermy
