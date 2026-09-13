#pragma once

#include "application/ai/AiActionToolDispatcher.h"
#include "application/ai/AiConversationModel.h"
#include "application/ai/AiNoteReadTool.h"
#include "application/ai/AiSftpListTool.h"
#include "application/ai/AiSftpReadTool.h"
#include "application/ai/AiTurnRunner.h"
#include "application/ai/McpRuntimeManager.h"
#include "application/sftp/SftpDirectoryModel.h"
#include "application/sftp/SftpSession.h"
#include "application/sftp/TransferManager.h"
#include "application/ssh/SshTerminalSession.h"
#include "application/terminal/LocalTerminalSession.h"
#include "domain/ai/AiContextBroker.h"
#include "domain/ai/AiTerminalFrameTracker.h"
#include "domain/terminal/SemanticTerminalObserver.h"
#include "domain/workbench/ScriptExecution.h"
#include "domain/workbench/ScriptRecorder.h"
#include "domain/workbench/ShellHistory.h"
#include "infrastructure/logging/SessionLogWriter.h"

#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <deque>
#include <memory>
#include <optional>
#include <stop_token>
#include <unordered_set>
#include <vector>

namespace ztermy
{
// One independently running backend and its session-scoped state. AppController
// owns these objects; moving their pane never replaces this session object.
enum class TerminalSessionKind : std::uint8_t
{
    Local,
    Ssh,
};

enum class TerminalSelectionAction : std::uint8_t
{
    None,
    AttachAi,
    Search,
    Highlight,
    Unhighlight,
};

struct TerminalSessionState final
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
    QString localShellId;
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
    QStringList pendingDropLocalFiles;
    QByteArray inputHistoryBuffer;
    QString telemetryState = QStringLiteral("paused");
    QString aiState = QStringLiteral("idle");
    QString aiError;
    std::optional<ai::AiProviderErrorCode> aiProviderErrorCode;
    std::optional<std::uint64_t> aiProviderRetryAfterMilliseconds;
    QString aiLastPrompt;
    QStringList aiLastSelectedSkillIds;
    QHash<QString, QString> aiWebSearchQueries;
    QString aiContextPreview;
    QString aiConversationId;
    QString aiManualCompactionCheckpoint;
    qsizetype aiManualCompactionCutoff = 0;
    QVariantList aiContextItems;
    QVariantMap aiCompaction;
    std::unordered_set<std::string> aiExcludedContextIds;
    std::unordered_set<std::string> aiPinnedContextIds;
    std::vector<ai::AiExplicitContext> aiExplicitContextItems;
    std::vector<ai::AiImageAttachment> aiImageAttachments;
    std::vector<ssh::SshKeywordHighlightRule> keywordHighlightRules;
    std::vector<workbench::ShellHistoryEntry> history;
    std::vector<workbench::ShellHistoryEntry> capturedHistory;
    std::shared_ptr<std::stop_source> historyCancellation;
    sftp::TransferRequestProvider remoteRequestProvider;
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
    TerminalSessionKind kind = TerminalSessionKind::Local;
    TerminalSelectionAction pendingSelectionAction = TerminalSelectionAction::None;
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
    bool aiLastWebSearchEnabled = false;
    bool workbenchOpen = false;
    bool composerOpen = false;
    bool running = false;
    bool recentConnectionRecorded = false;
    bool reconnectPending = false;
    bool restoreQuarantined = false;
    std::optional<ssh::SshFailureKind> sshFailure;
};

} // namespace ztermy
