cmake_minimum_required(VERSION 3.28)

project(
    ztermy
VERSION 0.3.0
    DESCRIPTION "A native Windows SSH terminal"
    LANGUAGES C CXX RC
)

# Some localized MSVC installations emit /showIncludes text as UTF-8 while
# CMake's compiler probe decodes the prefix using the active Windows code page.
# The resulting mojibake prevents Ninja from recording header dependencies.
# Correct only the known corrupted Simplified Chinese prefix so other locales
# keep the value detected by CMake.
if(MSVC
   AND CMAKE_GENERATOR MATCHES "^Ninja"
   AND CMAKE_CL_SHOWINCLUDES_PREFIX MATCHES "^娉")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
endif()

include(CTest)
include(FetchContent)

option(ZTERMY_STATIC_BUILD "Build ztermy with the locally provided static Qt package" OFF)

set(OPENSSL_USE_STATIC_LIBS "${ZTERMY_STATIC_BUILD}")
set(OPENSSL_MSVC_STATIC_RT "${ZTERMY_STATIC_BUILD}")
if(ZTERMY_STATIC_BUILD)
    # Qt's package discovery may have cached the OpenSSL DLL import libraries
    # before this project requests a fully static release. Force FindOpenSSL to
    # resolve the matching MT static archive instead.
    unset(LIB_EAY_DEBUG CACHE)
    unset(LIB_EAY_RELEASE CACHE)
    unset(SSL_EAY_DEBUG CACHE)
    unset(SSL_EAY_RELEASE CACHE)
endif()
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

set(
    GHOSTTY_ZIG_BUILD_FLAGS
    "-Dsimd=false"
    CACHE STRING
    "Additional flags passed to the libghostty-vt Zig build"
    FORCE
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND GHOSTTY_ZIG_BUILD_FLAGS "-Doptimize=ReleaseFast")
endif()

FetchContent_Declare(
    ghostty
    URL https://github.com/ghostty-org/ghostty/archive/ae8727401d8c549671c36cdc326a94f47c94b635.tar.gz
    URL_HASH SHA256=da5a400b145aa9d1a4c7e44219d3b74d49fb7f0973666f0eb3933894f308fb02
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(ghostty)

FetchContent_Declare(
    libssh2
    URL https://github.com/libssh2/libssh2/releases/download/libssh2-1.11.1/libssh2-1.11.1.tar.gz
    URL_HASH SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

function(ztermy_add_libssh2)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    set(CRYPTO_BACKEND OpenSSL)
    set(ENABLE_DEBUG_LOGGING OFF)
    FetchContent_MakeAvailable(libssh2)
endfunction()

ztermy_add_libssh2()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui LinguistTools Network Quick QuickControls2 Svg)

get_target_property(ZTERMY_QT_CORE_TARGET_TYPE Qt6::Core TYPE)
if(ZTERMY_STATIC_BUILD AND NOT ZTERMY_QT_CORE_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR
        "ZTERMY_STATIC_BUILD requires a static Qt package, but Qt6::Core is "
        "${ZTERMY_QT_CORE_TARGET_TYPE}. Set ZTERMY_QT_STATIC_ROOT before configuring."
    )
elseif(NOT ZTERMY_STATIC_BUILD AND NOT ZTERMY_QT_CORE_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR
        "The dynamic developer presets require a shared Qt package, but Qt6::Core is "
        "${ZTERMY_QT_CORE_TARGET_TYPE}. Set ZTERMY_QT_DYNAMIC_ROOT before configuring."
    )
endif()

qt_standard_project_setup(REQUIRES 6.8)

set(ZTERMY_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ZTERMY_GENERATED_DIRECTORY}")
configure_file(
    cmake/ztermy_version.h.in
    "${ZTERMY_GENERATED_DIRECTORY}/ztermy_version.h"
    @ONLY
)

qt_add_executable(ztermy_windows_icon_generator
    tools/WindowsIconGenerator.cpp
)
target_compile_features(ztermy_windows_icon_generator PRIVATE cxx_std_23)
target_link_libraries(ztermy_windows_icon_generator PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
set_target_properties(ztermy_windows_icon_generator PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    WIN32_EXECUTABLE FALSE
)

set(ZTERMY_BRANDING_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding")
set(ZTERMY_APP_ICON_SOURCE "${ZTERMY_BRANDING_SOURCE_DIRECTORY}/ztermy-app-icon.svg")
set(ZTERMY_APP_ICON_SMALL_SOURCE "${ZTERMY_BRANDING_SOURCE_DIRECTORY}/ztermy-app-icon-small.svg")
set(ZTERMY_GENERATED_BRANDING_DIRECTORY "${ZTERMY_GENERATED_DIRECTORY}/branding")
set(ZTERMY_WINDOWS_ICON_PATH "${ZTERMY_GENERATED_DIRECTORY}/ztermy.ico")
set(ZTERMY_BRANDING_PNG_PATHS)
foreach(icon_size 16 20 24 32 40 48 64 128 256)
    list(APPEND ZTERMY_BRANDING_PNG_PATHS
        "${ZTERMY_GENERATED_BRANDING_DIRECTORY}/ztermy-${icon_size}.png"
    )
endforeach()
add_custom_command(
    OUTPUT
        "${ZTERMY_WINDOWS_ICON_PATH}"
        ${ZTERMY_BRANDING_PNG_PATHS}
    COMMAND "$<TARGET_FILE:ztermy_windows_icon_generator>"
            "${ZTERMY_APP_ICON_SOURCE}"
            "${ZTERMY_APP_ICON_SMALL_SOURCE}"
            "${ZTERMY_WINDOWS_ICON_PATH}"
            "${ZTERMY_GENERATED_BRANDING_DIRECTORY}"
    DEPENDS
        ztermy_windows_icon_generator
        "${ZTERMY_APP_ICON_SOURCE}"
        "${ZTERMY_APP_ICON_SMALL_SOURCE}"
    COMMENT "Generating ztermy PNG and Windows ICO branding assets from SVG masters"
    VERBATIM
)
add_custom_target(ztermy_branding_assets
    DEPENDS
        "${ZTERMY_WINDOWS_ICON_PATH}"
        ${ZTERMY_BRANDING_PNG_PATHS}
)

configure_file(
    resources/windows/ztermy.rc.in
    "${ZTERMY_GENERATED_DIRECTORY}/ztermy.rc"
    @ONLY
)
set_property(
    SOURCE "${ZTERMY_GENERATED_DIRECTORY}/ztermy.rc"
    APPEND PROPERTY OBJECT_DEPENDS "${ZTERMY_WINDOWS_ICON_PATH}"
)

add_library(ztermy_core_config STATIC
    src/core/config/ApplicationPaths.cpp
    src/core/config/ApplicationPaths.h
    src/core/config/ApplicationSettings.cpp
    src/core/config/ApplicationSettings.h
)

add_library(ztermy_core_persistence STATIC
    src/core/persistence/LastKnownGoodFile.cpp
    src/core/persistence/LastKnownGoodFile.h
)

add_library(ztermy_application_actions STATIC
    src/application/actions/ActionRegistry.cpp
    src/application/actions/ActionRegistry.h
)

target_compile_features(ztermy_application_actions PUBLIC cxx_std_23)
target_include_directories(ztermy_application_actions PUBLIC src)
target_link_libraries(ztermy_application_actions PUBLIC Qt6::Core Qt6::Gui)

add_library(ztermy_core_diagnostics STATIC
    src/core/diagnostics/LatencyHistogram.cpp
    src/core/diagnostics/LatencyHistogram.h
)

add_library(ztermy_application_diagnostics STATIC
    src/application/diagnostics/DiagnosticReporter.cpp
    src/application/diagnostics/DiagnosticReporter.h
)
target_compile_features(ztermy_application_diagnostics PUBLIC cxx_std_23)
target_include_directories(ztermy_application_diagnostics PUBLIC src)
target_link_libraries(ztermy_application_diagnostics
    PUBLIC
        ztermy_core_config
        Qt6::Core
        Qt6::Gui
)

add_library(ztermy_credential_vault STATIC
    src/application/ai/AiSecretStore.cpp
    src/application/ai/AiSecretStore.h
    src/application/security/CredentialVaultCoordinator.cpp
    src/application/security/CredentialVaultCoordinator.h
    src/core/security/CredentialVault.cpp
    src/core/security/CredentialVault.h
    src/core/security/SensitiveByteArray.h
    src/infrastructure/security/InMemoryCredentialVault.cpp
    src/infrastructure/security/InMemoryCredentialVault.h
    src/infrastructure/security/PortableCredentialVault.cpp
    src/infrastructure/security/PortableCredentialVault.h
    src/platform/windows/WindowsCredentialVault.cpp
    src/platform/windows/WindowsCredentialVault.h
)

target_compile_features(ztermy_credential_vault PUBLIC cxx_std_23)
target_include_directories(ztermy_credential_vault PUBLIC src)
target_compile_definitions(ztermy_credential_vault PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
)
target_link_libraries(ztermy_credential_vault
    PUBLIC Qt6::Core
    PRIVATE OpenSSL::Crypto Advapi32
)

target_compile_features(ztermy_core_diagnostics PUBLIC cxx_std_23)
target_include_directories(ztermy_core_diagnostics PUBLIC src)

target_compile_features(ztermy_core_config PUBLIC cxx_std_23)
target_include_directories(ztermy_core_config PUBLIC src)
target_link_libraries(ztermy_core_config PUBLIC ztermy_core_persistence Qt6::Core)

target_compile_features(ztermy_core_persistence PUBLIC cxx_std_23)
target_include_directories(ztermy_core_persistence PUBLIC src)
target_link_libraries(ztermy_core_persistence PUBLIC Qt6::Core)

add_library(ztermy_terminal_transport STATIC
    src/infrastructure/terminal/ConPtyProcess.cpp
    src/infrastructure/terminal/ConPtyProcess.h
)

target_compile_features(ztermy_terminal_transport PUBLIC cxx_std_23)
target_include_directories(ztermy_terminal_transport PUBLIC src)
target_compile_definitions(ztermy_terminal_transport PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
)

add_library(ztermy_ssh_transport STATIC
    src/infrastructure/ssh/ExplicitProxyTunnel.cpp
    src/infrastructure/ssh/ExplicitProxyTunnel.h
    src/infrastructure/ssh/Libssh2Runtime.cpp
    src/infrastructure/ssh/Libssh2Runtime.h
    src/infrastructure/ssh/Libssh2Session.cpp
    src/infrastructure/ssh/Libssh2Session.h
    src/infrastructure/ssh/SshByteTransport.h
    src/infrastructure/ssh/SshDirectTcpipTransport.cpp
    src/infrastructure/ssh/SshDirectTcpipTransport.h
    src/infrastructure/ssh/WindowsTcpSocket.cpp
    src/infrastructure/ssh/WindowsTcpSocket.h
    src/infrastructure/ssh/WindowsTcpListener.cpp
    src/infrastructure/ssh/WindowsTcpListener.h
)

add_library(ztermy_ssh_domain STATIC
    src/domain/ssh/SshConnectionState.cpp
    src/domain/ssh/SshConnectionState.h
    src/domain/ssh/SshHostKey.cpp
    src/domain/ssh/SshHostKey.h
    src/domain/ssh/SshProfile.cpp
    src/domain/ssh/SshProfile.h
    src/domain/ssh/SshTarget.cpp
    src/domain/ssh/SshTarget.h
)

add_library(ztermy_remote_telemetry STATIC
    src/domain/telemetry/RemoteTelemetry.cpp
    src/domain/telemetry/RemoteTelemetry.h
)

add_library(ztermy_sftp_domain STATIC
    src/domain/sftp/SftpTypes.cpp
    src/domain/sftp/SftpTypes.h
    src/domain/sftp/TransferBatch.cpp
    src/domain/sftp/TransferBatch.h
    src/domain/sftp/TransferPlanner.cpp
    src/domain/sftp/TransferPlanner.h
    src/domain/sftp/TransferQueue.cpp
    src/domain/sftp/TransferQueue.h
)

add_library(ztermy_forwarding_domain STATIC
    src/domain/forwarding/PortForwardingRule.cpp
    src/domain/forwarding/PortForwardingRule.h
    src/domain/forwarding/Socks5Protocol.cpp
    src/domain/forwarding/Socks5Protocol.h
)

add_library(ztermy_forwarding_persistence STATIC
    src/infrastructure/forwarding/PortForwardingRuleStore.cpp
    src/infrastructure/forwarding/PortForwardingRuleStore.h
)

add_library(ztermy_forwarding_session STATIC
    src/application/forwarding/PortForwardingJob.cpp
    src/application/forwarding/PortForwardingJob.h
)

add_library(ztermy_ssh_persistence STATIC
    src/infrastructure/ssh/KnownHostsStore.cpp
    src/infrastructure/ssh/KnownHostsStore.h
    src/infrastructure/ssh/SshProfileStore.cpp
    src/infrastructure/ssh/SshProfileStore.h
)

add_library(ztermy_workbench_domain STATIC
    src/domain/workbench/QuickCommand.cpp
    src/domain/workbench/QuickCommand.h
    src/domain/workbench/ScriptDefinition.cpp
    src/domain/workbench/ScriptDefinition.h
    src/domain/workbench/ScriptExecution.cpp
    src/domain/workbench/ScriptExecution.h
    src/domain/workbench/ShellHistory.cpp
    src/domain/workbench/ShellHistory.h
    src/domain/workbench/ScriptRecorder.cpp
    src/domain/workbench/ScriptRecorder.h
    src/domain/workbench/WorkspaceState.cpp
    src/domain/workbench/WorkspaceState.h
)

add_library(ztermy_workbench_persistence STATIC
    src/infrastructure/workbench/PowerShellHistoryReader.cpp
    src/infrastructure/workbench/PowerShellHistoryReader.h
    src/infrastructure/workbench/QuickCommandStore.cpp
    src/infrastructure/workbench/QuickCommandStore.h
    src/infrastructure/workbench/NoteStore.cpp
    src/infrastructure/workbench/NoteStore.h
    src/infrastructure/workbench/ScriptStore.cpp
    src/infrastructure/workbench/ScriptStore.h
    src/infrastructure/workbench/WorkspaceStateStore.cpp
    src/infrastructure/workbench/WorkspaceStateStore.h
)

target_compile_features(ztermy_workbench_domain PUBLIC cxx_std_23)
target_include_directories(ztermy_workbench_domain PUBLIC src)

target_compile_features(ztermy_workbench_persistence PUBLIC cxx_std_23)
target_include_directories(ztermy_workbench_persistence PUBLIC src)
target_link_libraries(ztermy_workbench_persistence
    PUBLIC ztermy_workbench_domain
    PRIVATE ztermy_core_persistence Qt6::Core
)

target_compile_features(ztermy_ssh_domain PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_domain PUBLIC src)
target_compile_features(ztermy_remote_telemetry PUBLIC cxx_std_23)
target_include_directories(ztermy_remote_telemetry PUBLIC src)

target_compile_features(ztermy_sftp_domain PUBLIC cxx_std_23)
target_include_directories(ztermy_sftp_domain PUBLIC src)

target_compile_features(ztermy_forwarding_domain PUBLIC cxx_std_23)
target_include_directories(ztermy_forwarding_domain PUBLIC src)

target_compile_features(ztermy_forwarding_persistence PUBLIC cxx_std_23)
target_include_directories(ztermy_forwarding_persistence PUBLIC src)
target_link_libraries(ztermy_forwarding_persistence
    PUBLIC ztermy_forwarding_domain
    PRIVATE ztermy_core_persistence Qt6::Core
)

target_compile_features(ztermy_forwarding_session PUBLIC cxx_std_23)
target_include_directories(ztermy_forwarding_session PUBLIC src)
target_compile_definitions(ztermy_forwarding_session PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
)
target_link_libraries(ztermy_forwarding_session
    PUBLIC
        ztermy_forwarding_domain
        ztermy_ssh_connection
    PRIVATE
        ztermy_ssh_transport
        Qt6::Core
)

target_compile_features(ztermy_ssh_persistence PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_persistence PUBLIC src)
target_link_libraries(ztermy_ssh_persistence
    PUBLIC ztermy_ssh_domain
    PRIVATE ztermy_core_persistence Qt6::Core
)

target_compile_features(ztermy_ssh_transport PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_transport PUBLIC src)
target_link_libraries(ztermy_ssh_transport
    PUBLIC
        ztermy_sftp_domain
        ztermy_ssh_domain
    PRIVATE
        libssh2::libssh2_static
        ws2_32
)

add_library(ztermy_terminal_engine STATIC
    src/domain/terminal/CommandBlockAssembler.cpp
    src/domain/terminal/CommandBlockAssembler.h
    src/domain/terminal/CommandBlockStore.cpp
    src/domain/terminal/CommandBlockStore.h
    src/domain/terminal/GhosttyTerminalEngine.cpp
    src/domain/terminal/GhosttyTerminalEngine.h
    src/domain/terminal/TerminalEngine.h
    src/domain/terminal/TerminalOutputSink.h
    src/domain/terminal/ShellIntegrationDecoder.cpp
    src/domain/terminal/ShellIntegrationDecoder.h
    src/domain/terminal/SemanticTerminalObserver.cpp
    src/domain/terminal/SemanticTerminalObserver.h
)

add_library(ztermy_ai_domain STATIC
    src/domain/ai/AiAgentGuard.cpp
    src/domain/ai/AiAgentGuard.h
    src/domain/ai/AiPermissionPolicy.cpp
    src/domain/ai/AiPermissionPolicy.h
    src/domain/ai/AiProviderRetryPolicy.cpp
    src/domain/ai/AiProviderRetryPolicy.h
    src/domain/ai/AiProviderTypes.h
    src/domain/ai/AiToolDispatchLedger.cpp
    src/domain/ai/AiToolDispatchLedger.h
    src/domain/ai/AiUsageReporting.cpp
    src/domain/ai/AiUsageReporting.h
    src/domain/ai/McpToolRegistry.cpp
    src/domain/ai/McpToolRegistry.h
    src/domain/ai/NdjsonParser.cpp
    src/domain/ai/NdjsonParser.h
    src/domain/ai/ServerSentEventParser.cpp
    src/domain/ai/ServerSentEventParser.h
)

target_compile_features(ztermy_ai_domain PUBLIC cxx_std_23)
target_include_directories(ztermy_ai_domain PUBLIC src)
target_link_libraries(ztermy_ai_domain PRIVATE Qt6::Core)

add_library(ztermy_ai_context STATIC
    src/domain/ai/AiCommandTracker.cpp
    src/domain/ai/AiCommandTracker.h
    src/domain/ai/AiReadTools.cpp
    src/domain/ai/AiReadTools.h
    src/domain/ai/AiTerminalCapabilityAdapter.cpp
    src/domain/ai/AiTerminalCapabilityAdapter.h
    src/domain/ai/AiTerminalFrameTracker.cpp
    src/domain/ai/AiTerminalFrameTracker.h
    src/domain/ai/AiContextBroker.cpp
    src/domain/ai/AiContextBroker.h
    src/domain/ai/AiContextRedactor.cpp
    src/domain/ai/AiContextRedactor.h
    src/domain/ai/AiContextSerializer.cpp
    src/domain/ai/AiContextSerializer.h
)

target_compile_features(ztermy_ai_context PUBLIC cxx_std_23)
target_include_directories(ztermy_ai_context PUBLIC src)
target_link_libraries(ztermy_ai_context
    PUBLIC ztermy_ai_domain ztermy_terminal_engine
    PRIVATE Qt6::Core
)

add_library(ztermy_ai_provider STATIC
    src/infrastructure/ai/ProviderHttpClient.cpp
    src/infrastructure/ai/ProviderHttpClient.h
    src/infrastructure/ai/ProviderRequestFactory.cpp
    src/infrastructure/ai/ProviderRequestFactory.h
    src/infrastructure/ai/ProviderStreamMapper.cpp
    src/infrastructure/ai/ProviderStreamMapper.h
)

target_compile_features(ztermy_ai_provider PUBLIC cxx_std_23)
target_include_directories(ztermy_ai_provider PUBLIC src)
target_link_libraries(ztermy_ai_provider
    PUBLIC ztermy_ai_domain Qt6::Network
    PRIVATE Qt6::Core
)

add_library(ztermy_ai_evaluation STATIC
    src/infrastructure/ai/AiEvaluationHarness.cpp
    src/infrastructure/ai/AiEvaluationHarness.h
)

target_compile_features(ztermy_ai_evaluation PUBLIC cxx_std_23)
target_include_directories(ztermy_ai_evaluation PUBLIC src)
target_link_libraries(ztermy_ai_evaluation PUBLIC Qt6::Core)

add_library(ztermy_mcp_protocol STATIC
    src/infrastructure/ai/McpJsonRpcProtocol.cpp
    src/infrastructure/ai/McpJsonRpcProtocol.h
    src/infrastructure/ai/McpServerStore.cpp
    src/infrastructure/ai/McpServerStore.h
    src/infrastructure/ai/McpStdioClient.cpp
    src/infrastructure/ai/McpStdioClient.h
)

target_compile_features(ztermy_mcp_protocol PUBLIC cxx_std_23)
target_include_directories(ztermy_mcp_protocol PUBLIC src)
target_link_libraries(ztermy_mcp_protocol
    PUBLIC ztermy_ai_domain Qt6::Core
    PRIVATE ztermy_core_persistence
)

add_library(ztermy_ai_conversation_store STATIC
    src/infrastructure/ai/AiConversationStore.cpp
    src/infrastructure/ai/AiConversationStore.h
)

target_compile_features(ztermy_ai_conversation_store PUBLIC cxx_std_23)
target_include_directories(ztermy_ai_conversation_store PUBLIC src)
target_link_libraries(ztermy_ai_conversation_store
    PUBLIC ztermy_credential_vault Qt6::Core
    PRIVATE ztermy_core_persistence OpenSSL::Crypto
)

add_library(ztermy_ai_application STATIC
    src/application/ai/AiActivityModel.cpp
    src/application/ai/AiActivityModel.h
    src/application/ai/AiActionToolDispatcher.cpp
    src/application/ai/AiActionToolDispatcher.h
    src/application/ai/AiConversationModel.cpp
    src/application/ai/AiConversationModel.h
    src/application/ai/AiConversationHistoryModel.cpp
    src/application/ai/AiConversationHistoryModel.h
    src/application/ai/AiNoteReadTool.cpp
    src/application/ai/AiNoteReadTool.h
    src/application/ai/AiPrivacyDiagnostics.cpp
    src/application/ai/AiPrivacyDiagnostics.h
    src/application/ai/McpRuntimeManager.cpp
    src/application/ai/McpRuntimeManager.h
    src/application/ai/AiReadToolDispatcher.cpp
    src/application/ai/AiReadToolDispatcher.h
    src/application/ai/AiSftpReadTool.cpp
    src/application/ai/AiSftpReadTool.h
    src/application/ai/AiSftpListTool.cpp
    src/application/ai/AiSftpListTool.h
    src/application/ai/AiTurnRunner.cpp
    src/application/ai/AiTurnRunner.h
    src/application/ai/AiTerminalFrameTool.cpp
    src/application/ai/AiTerminalFrameTool.h
    src/application/ai/AiWaitCommandTool.cpp
    src/application/ai/AiWaitCommandTool.h
)

target_compile_features(ztermy_ai_application PUBLIC cxx_std_23)
target_include_directories(ztermy_ai_application PUBLIC src)
target_link_libraries(ztermy_ai_application
    PUBLIC ztermy_ai_context ztermy_ai_domain ztermy_ai_provider ztermy_mcp_protocol Qt6::Core
    PRIVATE ztermy_ai_conversation_store ztermy_core_persistence ztermy_sftp_domain
)

add_library(ztermy_session_logging STATIC
    src/infrastructure/logging/SessionLogWriter.cpp
    src/infrastructure/logging/SessionLogWriter.h
)

target_compile_features(ztermy_session_logging PUBLIC cxx_std_23)
target_include_directories(ztermy_session_logging PUBLIC src)
target_link_libraries(ztermy_session_logging PUBLIC Qt6::Core)

target_compile_features(ztermy_terminal_engine PUBLIC cxx_std_23)
target_include_directories(ztermy_terminal_engine PUBLIC src)
target_link_libraries(ztermy_terminal_engine PRIVATE ghostty-vt-static)

add_library(ztermy_terminal_session STATIC
    src/application/terminal/LocalTerminalSession.cpp
    src/application/terminal/LocalTerminalSession.h
    src/application/terminal/PowerShellShellIntegration.cpp
    src/application/terminal/PowerShellShellIntegration.h
)

add_library(ztermy_ssh_session STATIC
    src/application/ssh/SshTerminalSession.cpp
    src/application/ssh/SshTerminalSession.h
    src/platform/windows/WindowsTerminalTextCodec.cpp
    src/platform/windows/WindowsTerminalTextCodec.h
)

add_library(ztermy_ssh_connection STATIC
    src/application/ssh/SshConnectionBootstrap.cpp
    src/application/ssh/SshConnectionBootstrap.h
    src/application/ssh/SshConnectionRequest.cpp
    src/application/ssh/SshConnectionRequest.h
)

add_library(ztermy_sftp_session STATIC
    src/application/sftp/TransferBatchCoordinator.cpp
    src/application/sftp/TransferBatchCoordinator.h
    src/application/sftp/TransferBatchMaterializer.cpp
    src/application/sftp/TransferBatchMaterializer.h
    src/application/sftp/SftpClient.cpp
    src/application/sftp/SftpFilenameCodec.cpp
    src/application/sftp/SftpClient.h
    src/application/sftp/SftpDirectoryModel.cpp
    src/application/sftp/SftpDirectoryModel.h
    src/application/sftp/SftpSession.cpp
    src/application/sftp/SftpSession.h
    src/application/sftp/TransferExecutor.cpp
    src/application/sftp/TransferExecutor.h
    src/application/sftp/TransferManager.cpp
    src/application/sftp/TransferManager.h
    src/application/sftp/TransferSourceAdapters.cpp
    src/application/sftp/TransferSourceAdapters.h
    src/infrastructure/sftp/TransferRecoveryStore.cpp
    src/infrastructure/sftp/TransferRecoveryStore.h
    src/infrastructure/sftp/TransferBatchRecoveryStore.cpp
    src/infrastructure/sftp/TransferBatchRecoveryStore.h
)

target_compile_features(ztermy_terminal_session PUBLIC cxx_std_23)
target_include_directories(ztermy_terminal_session PUBLIC src)
target_compile_definitions(ztermy_terminal_session PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
)
target_link_libraries(ztermy_terminal_session
    PUBLIC
        ztermy_core_diagnostics
        ztermy_terminal_engine
    PRIVATE
        ztermy_terminal_transport
        Qt6::Core
)

target_compile_features(ztermy_ssh_session PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_session PUBLIC src)
target_compile_definitions(ztermy_ssh_session PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
)
target_compile_features(ztermy_ssh_connection PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_connection PUBLIC src)
target_link_libraries(ztermy_ssh_connection
    PUBLIC
        ztermy_ssh_domain
        Qt6::Core
    PRIVATE
        ztermy_ssh_persistence
        ztermy_ssh_transport
)
target_compile_features(ztermy_sftp_session PUBLIC cxx_std_23)
target_include_directories(ztermy_sftp_session PUBLIC src)
target_link_libraries(ztermy_sftp_session
    PUBLIC
        ztermy_sftp_domain
        ztermy_ssh_connection
        ztermy_ssh_transport
        Qt6::Core
)
target_link_libraries(ztermy_ssh_session
    PUBLIC
        ztermy_core_diagnostics
        ztermy_remote_telemetry
        ztermy_ssh_connection
        ztermy_ssh_domain
        ztermy_terminal_engine
        Qt6::Core
    PRIVATE
        ztermy_ssh_persistence
        ztermy_ssh_transport
)

add_library(ztermy_terminal_ui STATIC
    src/ui/terminal/TerminalItem.cpp
    src/ui/terminal/TerminalItem.h
    src/ui/terminal/TerminalKeywordHighlighter.cpp
    src/ui/terminal/TerminalKeywordHighlighter.h
    src/ui/terminal/TerminalTextLayout.cpp
    src/ui/terminal/TerminalTextLayout.h
)

add_library(ztermy_windows_clipboard STATIC
    src/platform/windows/WindowsProtectedClipboard.cpp
    src/platform/windows/WindowsProtectedClipboard.h
)
target_compile_features(ztermy_windows_clipboard PUBLIC cxx_std_23)
target_include_directories(ztermy_windows_clipboard PUBLIC src)
target_compile_definitions(ztermy_windows_clipboard PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
)
target_link_libraries(ztermy_windows_clipboard
    PUBLIC
        Qt6::Core
        Qt6::Gui
    PRIVATE
        user32
)

target_compile_features(ztermy_terminal_ui PUBLIC cxx_std_23)
target_include_directories(ztermy_terminal_ui PUBLIC src)
target_link_libraries(ztermy_terminal_ui
    PUBLIC
        ztermy_terminal_engine
        Qt6::Core
        Qt6::Gui
    Qt6::Quick
)

add_library(ztermy_icon_rendering STATIC
    src/ui/icons/SvgIconImageProvider.cpp
    src/ui/icons/SvgIconImageProvider.h
)
target_compile_features(ztermy_icon_rendering PUBLIC cxx_std_23)
target_include_directories(ztermy_icon_rendering PUBLIC src)
target_link_libraries(ztermy_icon_rendering
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Svg
)

qt_add_executable(ztermy WIN32
    src/main.cpp
    src/application/AppController.cpp
    src/application/AppController.h
    src/application/FontCatalog.cpp
    src/application/FontCatalog.h
    src/application/LocalizationManager.cpp
    src/application/LocalizationManager.h
    src/core/logging/Logging.cpp
    src/core/logging/Logging.h
    src/platform/windows/CrashDiagnostics.cpp
    src/platform/windows/CrashDiagnostics.h
    src/platform/windows/NativeWindow.cpp
    src/platform/windows/NativeWindow.h
    src/platform/windows/WindowHitTest.cpp
    src/platform/windows/WindowHitTest.h
    src/platform/windows/WindowsUiSettings.cpp
    src/platform/windows/WindowsUiSettings.h
    resources/windows/ztermy.manifest
    "${ZTERMY_GENERATED_DIRECTORY}/ztermy.rc"
)
add_dependencies(ztermy ztermy_branding_assets)

set_source_files_properties(
    resources/branding/ztermy-app-icon.svg
    PROPERTIES QT_RESOURCE_ALIAS app-icon.svg
)
set_source_files_properties(
    resources/branding/ztermy-app-icon-small.svg
    PROPERTIES QT_RESOURCE_ALIAS app-icon-small.svg
)
set_source_files_properties(
    resources/branding/ztermy-lockup-horizontal.svg
    PROPERTIES QT_RESOURCE_ALIAS lockup-horizontal.svg
)
set_source_files_properties(
    resources/branding/ztermy-lockup-about.svg
    PROPERTIES QT_RESOURCE_ALIAS lockup-about.svg
)
qt_add_resources(ztermy ztermy_branding
    PREFIX "/ztermy/branding"
    FILES
        resources/branding/ztermy-app-icon.svg
        resources/branding/ztermy-app-icon-small.svg
        resources/branding/ztermy-lockup-horizontal.svg
        resources/branding/ztermy-lockup-about.svg
)

set(ZTERMY_INTERFACE_ICON_FILES
    resources/icons/appearance.svg
    resources/icons/bookmark.svg
    resources/icons/application.svg
    resources/icons/check.svg
    resources/icons/chevron-down.svg
    resources/icons/chevron-left.svg
    resources/icons/chevron-right.svg
    resources/icons/chevron-up.svg
    resources/icons/close.svg
    resources/icons/commands.svg
    resources/icons/cpu.svg
    resources/icons/compose.svg
    resources/icons/copy.svg
    resources/icons/download.svg
    resources/icons/disk.svg
    resources/icons/edit.svg
    resources/icons/eye-off.svg
    resources/icons/eye.svg
    resources/icons/file.svg
    resources/icons/folder.svg
    resources/icons/follow.svg
    resources/icons/history.svg
    resources/icons/highlight.svg
    resources/icons/home.svg
    resources/icons/hosts.svg
    resources/icons/lock.svg
    resources/icons/list.svg
    resources/icons/memory.svg
    resources/icons/locate.svg
    resources/icons/minus.svg
    resources/icons/more.svg
    resources/icons/new-folder.svg
    resources/icons/new-file.svg
    resources/icons/network.svg
    resources/icons/play.svg
    resources/icons/pause.svg
    resources/icons/plus.svg
    resources/icons/refresh.svg
    resources/icons/save.svg
    resources/icons/search.svg
    resources/icons/security.svg
    resources/icons/settings.svg
    resources/icons/shortcuts.svg
    resources/icons/split-horizontal.svg
    resources/icons/split-vertical.svg
    resources/icons/swap-horizontal.svg
    resources/icons/terminal.svg
    resources/icons/transfer.svg
    resources/icons/tree.svg
    resources/icons/trash.svg
    resources/icons/upload.svg
    resources/icons/warning.svg
    resources/icons/activity.svg
    resources/icons/ai.svg
)
foreach(icon_file IN LISTS ZTERMY_INTERFACE_ICON_FILES)
    get_filename_component(icon_filename "${icon_file}" NAME)
    set_source_files_properties(
        "${icon_file}"
        PROPERTIES QT_RESOURCE_ALIAS "${icon_filename}"
    )
endforeach()
qt_add_resources(ztermy ztermy_interface_icons
    PREFIX "/ztermy/icons"
    FILES ${ZTERMY_INTERFACE_ICON_FILES}
)

set_source_files_properties(
    src/ui/qml/ActionButton.qml
    PROPERTIES QT_RESOURCE_ALIAS ActionButton.qml
)
set_source_files_properties(
    src/ui/qml/AppTextField.qml
    PROPERTIES QT_RESOURCE_ALIAS AppTextField.qml
)
set_source_files_properties(
    src/ui/qml/EditableSuggestionField.qml
    PROPERTIES QT_RESOURCE_ALIAS EditableSuggestionField.qml
)
set_source_files_properties(
    src/ui/qml/AppCheckBox.qml
    PROPERTIES QT_RESOURCE_ALIAS AppCheckBox.qml
)
set_source_files_properties(
    src/ui/qml/AppComboBox.qml
    PROPERTIES QT_RESOURCE_ALIAS AppComboBox.qml
)
set_source_files_properties(
    src/ui/qml/AppIcon.qml
    PROPERTIES QT_RESOURCE_ALIAS AppIcon.qml
)
set_source_files_properties(
    src/ui/qml/BrandIcon.qml
    PROPERTIES QT_RESOURCE_ALIAS BrandIcon.qml
)
set_source_files_properties(
    src/ui/qml/AppMenu.qml
    PROPERTIES QT_RESOURCE_ALIAS AppMenu.qml
)
set_source_files_properties(
    src/ui/qml/AppMenuItem.qml
    PROPERTIES QT_RESOURCE_ALIAS AppMenuItem.qml
)
set_source_files_properties(
    src/ui/qml/AppMenuSeparator.qml
    PROPERTIES QT_RESOURCE_ALIAS AppMenuSeparator.qml
)
set_source_files_properties(
    src/ui/qml/AppSlider.qml
    PROPERTIES QT_RESOURCE_ALIAS AppSlider.qml
)
set_source_files_properties(
    src/ui/qml/AppSpinBox.qml
    PROPERTIES QT_RESOURCE_ALIAS AppSpinBox.qml
)
set_source_files_properties(
    src/ui/qml/AppSwitch.qml
    PROPERTIES QT_RESOURCE_ALIAS AppSwitch.qml
)
set_source_files_properties(
    src/ui/qml/AppToolTip.qml
    PROPERTIES QT_RESOURCE_ALIAS AppToolTip.qml
)
set_source_files_properties(
    src/ui/qml/CaptionButton.qml
    PROPERTIES QT_RESOURCE_ALIAS CaptionButton.qml
)
set_source_files_properties(
    src/ui/qml/ConfirmationDialog.qml
    PROPERTIES QT_RESOURCE_ALIAS ConfirmationDialog.qml
)
set_source_files_properties(
    src/ui/qml/CommandPalette.qml
    PROPERTIES QT_RESOURCE_ALIAS CommandPalette.qml
)
set_source_files_properties(
    src/ui/qml/Main.qml
    PROPERTIES QT_RESOURCE_ALIAS Main.qml
)
set_source_files_properties(
    src/ui/qml/KeyboardAction.qml
    PROPERTIES QT_RESOURCE_ALIAS KeyboardAction.qml
)
set_source_files_properties(
    src/ui/qml/SettingsPane.qml
    PROPERTIES QT_RESOURCE_ALIAS SettingsPane.qml
)
set_source_files_properties(
    src/ui/qml/SftpBrowser.qml
    PROPERTIES QT_RESOURCE_ALIAS SftpBrowser.qml
)
set_source_files_properties(
    src/ui/qml/ShortcutSettings.qml
    PROPERTIES QT_RESOURCE_ALIAS ShortcutSettings.qml
)
set_source_files_properties(
    src/ui/qml/ReleaseIdentityCard.qml
    PROPERTIES QT_RESOURCE_ALIAS ReleaseIdentityCard.qml
)
set_source_files_properties(
    src/ui/qml/SectionCard.qml
    PROPERTIES QT_RESOURCE_ALIAS SectionCard.qml
)
set_source_files_properties(
    src/ui/qml/SideNavigationItem.qml
    PROPERTIES QT_RESOURCE_ALIAS SideNavigationItem.qml
)
set_source_files_properties(
    src/ui/qml/StatusMessage.qml
    PROPERTIES QT_RESOURCE_ALIAS StatusMessage.qml
)
set_source_files_properties(
    src/ui/qml/StatePanel.qml
    PROPERTIES QT_RESOURCE_ALIAS StatePanel.qml
)
set_source_files_properties(
    src/ui/qml/HostConnectionPane.qml
    PROPERTIES QT_RESOURCE_ALIAS HostConnectionPane.qml
)
set_source_files_properties(
    src/ui/qml/PortForwardingPane.qml
    PROPERTIES QT_RESOURCE_ALIAS PortForwardingPane.qml
)
set_source_files_properties(
    src/ui/qml/HostKeyPrompt.qml
    PROPERTIES QT_RESOURCE_ALIAS HostKeyPrompt.qml
)
set_source_files_properties(
    src/ui/qml/Theme.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
        QT_RESOURCE_ALIAS Theme.qml
)
set_source_files_properties(
    src/ui/qml/TerminalTabAction.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalTabAction.qml
)
set_source_files_properties(
    src/ui/qml/RemoteTelemetryStrip.qml
    PROPERTIES QT_RESOURCE_ALIAS RemoteTelemetryStrip.qml
)
set_source_files_properties(
    src/ui/qml/TelemetrySparkline.qml
    PROPERTIES QT_RESOURCE_ALIAS TelemetrySparkline.qml
)
set_source_files_properties(
    src/ui/qml/AiAssistantPane.qml
    PROPERTIES QT_RESOURCE_ALIAS AiAssistantPane.qml
)
set_source_files_properties(
    src/ui/qml/NotesPane.qml
    PROPERTIES QT_RESOURCE_ALIAS NotesPane.qml
)
set_source_files_properties(
    src/ui/qml/ScriptEditor.qml
    PROPERTIES QT_RESOURCE_ALIAS ScriptEditor.qml
)
set_source_files_properties(
    src/ui/qml/ScriptRunPane.qml
    PROPERTIES QT_RESOURCE_ALIAS ScriptRunPane.qml
)
set_source_files_properties(
    src/ui/qml/TerminalWorkbench.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalWorkbench.qml
)
set_source_files_properties(
    src/ui/qml/TerminalComposer.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalComposer.qml
)
set_source_files_properties(
    src/ui/qml/TerminalKeywordPopover.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalKeywordPopover.qml
)
set_source_files_properties(
    src/ui/qml/TerminalSessionSettingsPopover.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalSessionSettingsPopover.qml
)
set_source_files_properties(
    src/ui/qml/TerminalRecordingPopover.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalRecordingPopover.qml
)
set_source_files_properties(
    src/ui/qml/TransferCenter.qml
    PROPERTIES QT_RESOURCE_ALIAS TransferCenter.qml
)
set_source_files_properties(
    src/ui/qml/TransferToast.qml
    PROPERTIES QT_RESOURCE_ALIAS TransferToast.qml
)

qt_add_qml_module(ztermy
    URI Ztermy
    VERSION 1.0
    DEPENDENCIES
        QtQuick
    QML_FILES
        src/ui/qml/ActionButton.qml
        src/ui/qml/AiAssistantPane.qml
        src/ui/qml/AppCheckBox.qml
        src/ui/qml/AppComboBox.qml
        src/ui/qml/AppIcon.qml
        src/ui/qml/BrandIcon.qml
        src/ui/qml/AppMenu.qml
        src/ui/qml/AppMenuItem.qml
        src/ui/qml/AppMenuSeparator.qml
        src/ui/qml/AppSlider.qml
        src/ui/qml/AppSpinBox.qml
        src/ui/qml/AppSwitch.qml
        src/ui/qml/AppTextField.qml
        src/ui/qml/AppToolTip.qml
        src/ui/qml/EditableSuggestionField.qml
        src/ui/qml/CaptionButton.qml
        src/ui/qml/ConfirmationDialog.qml
        src/ui/qml/CommandPalette.qml
        src/ui/qml/FontPicker.qml
        src/ui/qml/HostConnectionPane.qml
        src/ui/qml/PortForwardingPane.qml
        src/ui/qml/HostKeyPrompt.qml
        src/ui/qml/KeyboardAction.qml
        src/ui/qml/Main.qml
        src/ui/qml/NotesPane.qml
        src/ui/qml/ReleaseIdentityCard.qml
        src/ui/qml/RemoteTelemetryStrip.qml
        src/ui/qml/ScriptEditor.qml
        src/ui/qml/ScriptRunPane.qml
        src/ui/qml/SectionCard.qml
        src/ui/qml/SettingsPane.qml
        src/ui/qml/SftpBrowser.qml
        src/ui/qml/ShortcutSettings.qml
        src/ui/qml/SideNavigationItem.qml
        src/ui/qml/StatePanel.qml
        src/ui/qml/StatusMessage.qml
        src/ui/qml/TerminalTabAction.qml
        src/ui/qml/TelemetrySparkline.qml
        src/ui/qml/TerminalComposer.qml
        src/ui/qml/TerminalKeywordPopover.qml
        src/ui/qml/TerminalSessionSettingsPopover.qml
        src/ui/qml/TerminalSplitNode.qml
        src/ui/qml/TerminalRecordingPopover.qml
        src/ui/qml/TerminalWorkbench.qml
        src/ui/qml/TransferCenter.qml
        src/ui/qml/TransferToast.qml
        src/ui/qml/Theme.qml
)

set(ZTERMY_TRANSLATION_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/translations/ztermy_zh_CN.ts"
)
qt_add_translations(ztermy
    SOURCE_TARGETS
        ztermy
        ztermy_terminal_session
        ztermy_ssh_session
        ztermy_terminal_ui
        ztermy_application_actions
        ztermy_application_diagnostics
    TS_FILES ${ZTERMY_TRANSLATION_FILES}
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete
    LRELEASE_OPTIONS -nounfinished
    NO_GENERATE_PLURALS_TS_FILE
)

find_program(ZTERMY_PWSH_EXECUTABLE NAMES pwsh REQUIRED)
add_custom_target(ztermy_translation_gate
    COMMAND
        "${ZTERMY_PWSH_EXECUTABLE}" -NoProfile -File
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_translations.ps1"
        -CatalogPath "${ZTERMY_TRANSLATION_FILES}"
        -SourceRoot "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS update_translations
    COMMENT "Verifying translation freshness, completeness, placeholders, and localization call sites"
    VERBATIM
)
if(BUILD_TESTING)
    add_test(
        NAME translation-catalog
        COMMAND
            "${ZTERMY_PWSH_EXECUTABLE}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_translations.ps1"
            -CatalogPath "${ZTERMY_TRANSLATION_FILES}"
            -SourceRoot "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

qt_generate_foreign_qml_types(ztermy_terminal_ui ztermy)

target_compile_features(ztermy PRIVATE cxx_std_23)
target_include_directories(ztermy PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${ZTERMY_GENERATED_DIRECTORY}"
    src
    src/ui/terminal
)

target_compile_definitions(ztermy PRIVATE
    NOMINMAX
    UNICODE
    WIN32_LEAN_AND_MEAN
    _UNICODE
    $<$<CONFIG:Debug>:ZTERMY_DEBUG_BUILD=1>
)

if(ZTERMY_STATIC_BUILD)
    target_compile_definitions(ztermy PRIVATE ZTERMY_STATIC_BUILD=1)
endif()

target_link_libraries(ztermy PRIVATE
    ztermy_ai_application
    ztermy_ai_context
    ztermy_application_actions
    ztermy_application_diagnostics
    ztermy_credential_vault
    ztermy_core_config
    ztermy_forwarding_persistence
    ztermy_forwarding_session
    ztermy_ssh_persistence
    ztermy_ssh_session
    ztermy_sftp_session
    ztermy_session_logging
    ztermy_terminal_session
    ztermy_terminal_transport
    ztermy_terminal_ui
    ztermy_windows_clipboard
    ztermy_icon_rendering
    ztermy_workbench_persistence
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    dbghelp
    dwmapi
)

add_custom_target(ztermy_window_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/window-runtime-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --window-runtime-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/window-runtime-smoke"
    DEPENDS ztermy
    COMMENT "Checking the real maximized client area against the Windows monitor work area"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_window_appearance_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/window-appearance-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --window-appearance-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/window-appearance-smoke"
    DEPENDS ztermy
    COMMENT "Checking Windows DWM theme, opacity, and backdrop state"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_window_resize_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/window-resize-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --window-resize-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/window-resize-smoke"
    DEPENDS ztermy
    COMMENT "Checking native resize hit areas and system cursors"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_window_dpi_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/window-dpi-smoke"
    COMMAND "${CMAKE_COMMAND}"
            "-DZTERMY_DPI_SMOKE_EXECUTABLE=$<TARGET_FILE:ztermy>"
            "-DZTERMY_DPI_SMOKE_DATA_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-data/window-dpi-smoke"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunWindowDpiSmoke.cmake"
    DEPENDS ztermy
    COMMENT "Checking logical geometry, physical pixels, and native caption hit testing at 100%-200% scale"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_ui_layout_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/ui-layout-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --ui-layout-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/ui-layout-smoke"
    DEPENDS ztermy
    COMMENT "Checking compact and regular QML layout breakpoints in the real window"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_ui_keyboard_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/ui-keyboard-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --ui-keyboard-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/ui-keyboard-smoke"
    DEPENDS ztermy
    COMMENT "Checking the real-window keyboard route and shared control behavior"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_terminal_render_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/terminal-render-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --terminal-render-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/terminal-render-smoke"
    DEPENDS ztermy
    COMMENT "Checking terminal rendering and GUI responsiveness under large output"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_lifecycle_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/lifecycle-runtime-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --lifecycle-runtime-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/lifecycle-runtime-smoke"
    DEPENDS ztermy
    COMMENT "Checking repeated ConPTY tab startup, close, and orderly multi-session shutdown"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_real_host_ui_runtime_smoke
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/real-host-ui-smoke"
    COMMAND "$<TARGET_FILE:ztermy>"
            --real-host-ui-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/real-host-ui-smoke"
    DEPENDS ztermy
    COMMENT "Checking the integrated SSH and SFTP UI against an explicitly configured real host"
    USES_TERMINAL
    VERBATIM
)

if(ZTERMY_STATIC_BUILD)
    install(TARGETS ztermy RUNTIME DESTINATION . COMPONENT Runtime)
else()
    install(TARGETS ztermy RUNTIME DESTINATION bin COMPONENT Runtime)
    install(
        FILES ${RUNTIME_DEPENDENCIES}
        DESTINATION bin
        COMPONENT Runtime
    )

    set(ZTERMY_DYNAMIC_DEPLOY_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/package/dynamic"
    )
    qt_generate_deploy_qml_app_script(
        TARGET ztermy
        OUTPUT_SCRIPT ZTERMY_DYNAMIC_DEPLOY_SCRIPT
        NO_TRANSLATIONS
    )
    install(
        SCRIPT "${ZTERMY_DYNAMIC_DEPLOY_SCRIPT}"
        COMPONENT Runtime
    )

    add_custom_target(ztermy_dynamic_deploy
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ZTERMY_DYNAMIC_DEPLOY_ROOT}"
        COMMAND "${CMAKE_COMMAND}"
                --install "${CMAKE_CURRENT_BINARY_DIR}"
                --prefix "${ZTERMY_DYNAMIC_DEPLOY_ROOT}"
                --component Runtime
        DEPENDS ztermy
        COMMENT "Creating an isolated dynamic Qt developer deployment"
        VERBATIM
    )

    add_custom_target(ztermy_dynamic_deploy_smoke
        COMMAND "${CMAKE_COMMAND}"
                "-DZTERMY_DEPLOYED_EXECUTABLE=${ZTERMY_DYNAMIC_DEPLOY_ROOT}/bin/ztermy.exe"
                "-DZTERMY_DEPLOYED_DATA_DIR=${ZTERMY_DYNAMIC_DEPLOY_ROOT}/smoke-data"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunDynamicDeploymentSmoke.cmake"
        DEPENDS ztermy_dynamic_deploy
        COMMENT "Starting ztermy from the isolated dynamic Qt deployment"
        VERBATIM
    )
endif()

if(ZTERMY_STATIC_BUILD)
    set(ZTERMY_PORTABLE_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/package/portable")
    file(MAKE_DIRECTORY "${ZTERMY_PORTABLE_PACKAGE_ROOT}")
    add_custom_target(ztermy_portable_package
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "ztermy-${PROJECT_VERSION}-windows-x64"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "ztermy-${PROJECT_VERSION}-windows-x64"
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:ztermy>"
                "ztermy-${PROJECT_VERSION}-windows-x64/ztermy.exe"
        COMMAND "${CMAKE_COMMAND}" -E touch
                "ztermy-${PROJECT_VERSION}-windows-x64/portable.flag"
        COMMAND "${CMAKE_COMMAND}" -E tar cf
                "ztermy-${PROJECT_VERSION}-windows-x64-portable.zip"
                --format=zip
                "ztermy-${PROJECT_VERSION}-windows-x64"
        WORKING_DIRECTORY "${ZTERMY_PORTABLE_PACKAGE_ROOT}"
        DEPENDS ztermy
        COMMENT "Creating the self-contained ztermy portable package"
        VERBATIM
    )
endif()

if(MSVC)
    target_compile_options(ztermy_windows_icon_generator PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_core_diagnostics PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_application_diagnostics PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_core_config PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_credential_vault PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_terminal_transport PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_terminal_engine PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_session_logging PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_ssh_transport PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_ssh_domain PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_remote_telemetry PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_ssh_persistence PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_terminal_session PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_ssh_session PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy_terminal_ui PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )

    target_compile_options(ztermy PRIVATE
        /permissive-
        /utf-8
        /W4
        /Zc:__cplusplus
    )
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Network Test)

    qt_add_executable(ztermy_terminal_text_codec_tests
        tests/terminal_text_codec_tests.cpp
    )
    target_compile_features(ztermy_terminal_text_codec_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_terminal_text_codec_tests PRIVATE
        ztermy_ssh_session
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(ztermy_terminal_text_codec_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()
    add_test(NAME terminal-text-codec COMMAND ztermy_terminal_text_codec_tests)

    qt_add_executable(ztermy_remote_telemetry_tests
        tests/remote_telemetry_tests.cpp
    )
    target_compile_features(ztermy_remote_telemetry_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_remote_telemetry_tests PRIVATE
        ztermy_remote_telemetry
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(ztermy_remote_telemetry_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()
    add_test(NAME remote-telemetry COMMAND ztermy_remote_telemetry_tests)

    qt_add_executable(ztermy_diagnostic_reporter_tests
        tests/diagnostic_reporter_tests.cpp
    )
    target_compile_features(ztermy_diagnostic_reporter_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_diagnostic_reporter_tests PRIVATE
        ztermy_application_diagnostics
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(ztermy_diagnostic_reporter_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()
    add_test(NAME diagnostic-reporter COMMAND ztermy_diagnostic_reporter_tests)

    qt_add_executable(ztermy_application_paths_tests
        tests/application_paths_tests.cpp
    )

    target_compile_features(ztermy_application_paths_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_application_paths_tests PRIVATE
        ztermy_core_config
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_application_paths_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME application-paths COMMAND ztermy_application_paths_tests)

    qt_add_executable(ztermy_session_log_writer_tests
        tests/session_log_writer_tests.cpp
    )

    target_compile_features(ztermy_session_log_writer_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_session_log_writer_tests PRIVATE
        ztermy_session_logging
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_session_log_writer_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME session-log-writer COMMAND ztermy_session_log_writer_tests)

    qt_add_executable(ztermy_application_settings_tests
        tests/application_settings_tests.cpp
    )

    target_compile_features(ztermy_application_settings_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_application_settings_tests PRIVATE
        ztermy_core_config
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_application_settings_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME application-settings COMMAND ztermy_application_settings_tests)

    qt_add_executable(ztermy_last_known_good_file_tests
        tests/last_known_good_file_tests.cpp
    )

    target_compile_features(ztermy_last_known_good_file_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_last_known_good_file_tests PRIVATE
        ztermy_core_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_last_known_good_file_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME last-known-good-file COMMAND ztermy_last_known_good_file_tests)

    qt_add_executable(ztermy_action_registry_tests
        tests/action_registry_tests.cpp
    )

    target_compile_features(ztermy_action_registry_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_action_registry_tests PRIVATE
        ztermy_application_actions
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_action_registry_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME action-registry COMMAND ztermy_action_registry_tests)

    qt_add_executable(ztermy_sftp_types_tests
        tests/sftp_types_tests.cpp
    )

    target_compile_features(ztermy_sftp_types_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_sftp_types_tests PRIVATE
        ztermy_sftp_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_sftp_types_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME sftp-types COMMAND ztermy_sftp_types_tests)

    qt_add_executable(ztermy_port_forwarding_rule_store_tests
        tests/port_forwarding_rule_store_tests.cpp
    )

    target_compile_features(ztermy_port_forwarding_rule_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_port_forwarding_rule_store_tests PRIVATE
        ztermy_forwarding_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_port_forwarding_rule_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME port-forwarding-rule-store COMMAND ztermy_port_forwarding_rule_store_tests)

    qt_add_executable(ztermy_socks5_protocol_tests
        tests/socks5_protocol_tests.cpp
    )

    target_compile_features(ztermy_socks5_protocol_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_socks5_protocol_tests PRIVATE
        ztermy_forwarding_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_socks5_protocol_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME socks5-protocol COMMAND ztermy_socks5_protocol_tests)

    qt_add_executable(ztermy_port_forwarding_job_tests
        tests/port_forwarding_job_tests.cpp
    )

    target_compile_features(ztermy_port_forwarding_job_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_port_forwarding_job_tests PRIVATE
        ztermy_forwarding_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_port_forwarding_job_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME port-forwarding-job COMMAND ztermy_port_forwarding_job_tests)

    qt_add_executable(ztermy_port_forwarding_real_host_tests
        tests/port_forwarding_real_host_tests.cpp
    )

    target_compile_features(ztermy_port_forwarding_real_host_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_port_forwarding_real_host_tests PRIVATE
        ztermy_forwarding_session
        Qt6::Network
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_port_forwarding_real_host_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME port-forwarding-real-host COMMAND ztermy_port_forwarding_real_host_tests)

    qt_add_executable(ztermy_transfer_queue_tests
        tests/transfer_queue_tests.cpp
    )

    target_compile_features(ztermy_transfer_queue_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_queue_tests PRIVATE
        ztermy_sftp_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_queue_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-queue COMMAND ztermy_transfer_queue_tests)

    qt_add_executable(ztermy_transfer_batch_tests
        tests/transfer_batch_tests.cpp
    )

    target_compile_features(ztermy_transfer_batch_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_batch_tests PRIVATE
        ztermy_sftp_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_batch_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-batch COMMAND ztermy_transfer_batch_tests)

    qt_add_executable(ztermy_transfer_planner_tests
        tests/transfer_planner_tests.cpp
    )

    target_compile_features(ztermy_transfer_planner_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_planner_tests PRIVATE
        ztermy_sftp_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_planner_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-planner COMMAND ztermy_transfer_planner_tests)

    qt_add_executable(ztermy_transfer_source_adapters_tests
        tests/transfer_source_adapters_tests.cpp
    )

    target_compile_features(ztermy_transfer_source_adapters_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_source_adapters_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_source_adapters_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-source-adapters COMMAND ztermy_transfer_source_adapters_tests)

    qt_add_executable(ztermy_transfer_batch_materializer_tests
        tests/transfer_batch_materializer_tests.cpp
    )

    target_compile_features(ztermy_transfer_batch_materializer_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_batch_materializer_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_batch_materializer_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-batch-materializer COMMAND ztermy_transfer_batch_materializer_tests)

    qt_add_executable(ztermy_transfer_batch_coordinator_tests
        tests/transfer_batch_coordinator_tests.cpp
    )

    target_compile_features(ztermy_transfer_batch_coordinator_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_batch_coordinator_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_batch_coordinator_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-batch-coordinator COMMAND ztermy_transfer_batch_coordinator_tests)

    qt_add_executable(ztermy_sftp_session_tests
        tests/sftp_session_tests.cpp
    )

    target_compile_features(ztermy_sftp_session_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_sftp_session_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_sftp_session_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME sftp-session COMMAND ztermy_sftp_session_tests)

    qt_add_executable(ztermy_sftp_filename_codec_tests
        tests/sftp_filename_codec_tests.cpp
    )
    target_compile_features(ztermy_sftp_filename_codec_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_sftp_filename_codec_tests PRIVATE ztermy_sftp_session Qt6::Test)
    if(MSVC)
        target_compile_options(ztermy_sftp_filename_codec_tests PRIVATE /permissive- /utf-8 /W4 /Zc:__cplusplus)
    endif()
    add_test(NAME sftp-filename-codec COMMAND ztermy_sftp_filename_codec_tests)

    qt_add_executable(ztermy_sftp_directory_model_tests
        tests/sftp_directory_model_tests.cpp
    )

    target_compile_features(ztermy_sftp_directory_model_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_sftp_directory_model_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_sftp_directory_model_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME sftp-directory-model COMMAND ztermy_sftp_directory_model_tests)

    qt_add_executable(ztermy_transfer_executor_tests
        tests/transfer_executor_tests.cpp
    )

    target_compile_features(ztermy_transfer_executor_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_executor_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_executor_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-executor COMMAND ztermy_transfer_executor_tests)

    qt_add_executable(ztermy_transfer_manager_tests
        tests/transfer_manager_tests.cpp
    )

    target_compile_features(ztermy_transfer_manager_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_manager_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_manager_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-manager COMMAND ztermy_transfer_manager_tests)

    qt_add_executable(ztermy_transfer_recovery_store_tests
        tests/transfer_recovery_store_tests.cpp
    )

    target_compile_features(ztermy_transfer_recovery_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_recovery_store_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_recovery_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-recovery-store COMMAND ztermy_transfer_recovery_store_tests)

    qt_add_executable(ztermy_transfer_batch_recovery_store_tests
        tests/transfer_batch_recovery_store_tests.cpp
    )

    target_compile_features(ztermy_transfer_batch_recovery_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_transfer_batch_recovery_store_tests PRIVATE
        ztermy_sftp_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_transfer_batch_recovery_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME transfer-batch-recovery-store COMMAND ztermy_transfer_batch_recovery_store_tests)

    qt_add_executable(ztermy_quick_command_store_tests
        tests/quick_command_store_tests.cpp
    )

    target_compile_features(ztermy_quick_command_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_quick_command_store_tests PRIVATE
        ztermy_workbench_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_quick_command_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME quick-command-store COMMAND ztermy_quick_command_store_tests)

    qt_add_executable(ztermy_script_definition_tests
        tests/script_definition_tests.cpp
    )

    target_compile_features(ztermy_script_definition_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_script_definition_tests PRIVATE
        ztermy_workbench_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_script_definition_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /WX
        )
    endif()

    add_test(NAME script-definition COMMAND ztermy_script_definition_tests)

    qt_add_executable(ztermy_script_store_tests
        tests/script_store_tests.cpp
    )

    target_compile_features(ztermy_script_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_script_store_tests PRIVATE
        ztermy_workbench_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_script_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /WX
        )
    endif()

    add_test(NAME script-store COMMAND ztermy_script_store_tests)

    qt_add_executable(ztermy_note_store_tests
        tests/note_store_tests.cpp
    )

    target_compile_features(ztermy_note_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_note_store_tests PRIVATE
        ztermy_workbench_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_note_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /WX
        )
    endif()

    add_test(NAME note-store COMMAND ztermy_note_store_tests)

    qt_add_executable(ztermy_script_execution_tests
        tests/script_execution_tests.cpp
    )

    target_compile_features(ztermy_script_execution_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_script_execution_tests PRIVATE
        ztermy_workbench_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_script_execution_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /WX
        )
    endif()

    add_test(NAME script-execution COMMAND ztermy_script_execution_tests)

    qt_add_executable(ztermy_workspace_state_store_tests
        tests/workspace_state_store_tests.cpp
    )

    target_compile_features(ztermy_workspace_state_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_workspace_state_store_tests PRIVATE
        ztermy_workbench_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_workspace_state_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME workspace-state-store COMMAND ztermy_workspace_state_store_tests)

    qt_add_executable(ztermy_workbench_domain_tests
        tests/workbench_domain_tests.cpp
    )

    target_compile_features(ztermy_workbench_domain_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_workbench_domain_tests PRIVATE
        ztermy_workbench_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_workbench_domain_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME workbench-domain COMMAND ztermy_workbench_domain_tests)

    qt_add_executable(ztermy_localization_manager_tests
        tests/localization_manager_tests.cpp
        src/application/LocalizationManager.cpp
        src/application/LocalizationManager.h
    )

    target_compile_features(ztermy_localization_manager_tests PRIVATE cxx_std_23)
    target_include_directories(ztermy_localization_manager_tests PRIVATE src)
    target_link_libraries(ztermy_localization_manager_tests PRIVATE
        ztermy_core_config
        Qt6::Qml
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_localization_manager_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME localization-manager COMMAND ztermy_localization_manager_tests)

    qt_add_executable(ztermy_font_catalog_tests
        tests/font_catalog_tests.cpp
        src/application/FontCatalog.cpp
        src/application/FontCatalog.h
    )

    target_compile_features(ztermy_font_catalog_tests PRIVATE cxx_std_23)
    target_include_directories(ztermy_font_catalog_tests PRIVATE src)
    target_link_libraries(ztermy_font_catalog_tests PRIVATE
        Qt6::Gui
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_font_catalog_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME font-catalog COMMAND ztermy_font_catalog_tests)

    qt_add_executable(ztermy_credential_vault_tests
        tests/credential_vault_tests.cpp
    )

    target_compile_features(ztermy_credential_vault_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_credential_vault_tests PRIVATE
        ztermy_credential_vault
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_credential_vault_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME credential-vault COMMAND ztermy_credential_vault_tests)

    qt_add_executable(ztermy_latency_histogram_tests
        tests/latency_histogram_tests.cpp
    )

    target_compile_features(ztermy_latency_histogram_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_latency_histogram_tests PRIVATE
        ztermy_core_diagnostics
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_latency_histogram_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME latency-histogram COMMAND ztermy_latency_histogram_tests)

    qt_add_executable(ztermy_window_hit_test_tests
        tests/window_hit_test_tests.cpp
        src/platform/windows/WindowHitTest.cpp
        src/platform/windows/WindowHitTest.h
        src/platform/windows/WindowsUiSettings.cpp
        src/platform/windows/WindowsUiSettings.h
    )

    target_compile_features(ztermy_window_hit_test_tests PRIVATE cxx_std_23)
    target_include_directories(ztermy_window_hit_test_tests PRIVATE src)
    target_link_libraries(ztermy_window_hit_test_tests PRIVATE Qt6::Test dwmapi user32)

    if(MSVC)
        target_compile_options(ztermy_window_hit_test_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME window-hit-test COMMAND ztermy_window_hit_test_tests)

    qt_add_executable(ztermy_conpty_process_tests
        tests/conpty_process_tests.cpp
    )

    target_compile_features(ztermy_conpty_process_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_conpty_process_tests PRIVATE
        ztermy_terminal_transport
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_conpty_process_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME conpty-process COMMAND ztermy_conpty_process_tests)

    qt_add_executable(ztermy_ssh_runtime_tests
        tests/ssh_runtime_tests.cpp
    )

    target_compile_features(ztermy_ssh_runtime_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_runtime_tests PRIVATE
        ztermy_ssh_transport
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_runtime_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-runtime COMMAND ztermy_ssh_runtime_tests)

    qt_add_executable(ztermy_ssh_connection_state_tests
        tests/ssh_connection_state_tests.cpp
    )

    target_compile_features(ztermy_ssh_connection_state_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_connection_state_tests PRIVATE
        ztermy_ssh_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_connection_state_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-connection-state COMMAND ztermy_ssh_connection_state_tests)

    qt_add_executable(ztermy_ssh_host_key_tests
        tests/ssh_host_key_tests.cpp
    )

    target_compile_features(ztermy_ssh_host_key_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_host_key_tests PRIVATE
        ztermy_ssh_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_host_key_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-host-key COMMAND ztermy_ssh_host_key_tests)

    qt_add_executable(ztermy_known_hosts_store_tests
        tests/known_hosts_store_tests.cpp
    )

    target_compile_features(ztermy_known_hosts_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_known_hosts_store_tests PRIVATE
        ztermy_ssh_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_known_hosts_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME known-hosts-store COMMAND ztermy_known_hosts_store_tests)

    qt_add_executable(ztermy_ssh_profile_store_tests
        tests/ssh_profile_store_tests.cpp
    )

    target_compile_features(ztermy_ssh_profile_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_profile_store_tests PRIVATE
        ztermy_ssh_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_profile_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-profile-store COMMAND ztermy_ssh_profile_store_tests)

    qt_add_executable(ztermy_ssh_target_tests
        tests/ssh_target_tests.cpp
    )

    target_compile_features(ztermy_ssh_target_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_target_tests PRIVATE
        ztermy_ssh_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_target_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-target COMMAND ztermy_ssh_target_tests)

    qt_add_executable(ztermy_windows_tcp_socket_tests
        tests/windows_tcp_socket_tests.cpp
    )

    target_compile_features(ztermy_windows_tcp_socket_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_windows_tcp_socket_tests PRIVATE
        ztermy_ssh_transport
        Qt6::Network
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_windows_tcp_socket_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME windows-tcp-socket COMMAND ztermy_windows_tcp_socket_tests)

    qt_add_executable(ztermy_explicit_proxy_tunnel_tests
        tests/explicit_proxy_tunnel_tests.cpp
    )

    target_compile_features(ztermy_explicit_proxy_tunnel_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_explicit_proxy_tunnel_tests PRIVATE
        ztermy_ssh_transport
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_explicit_proxy_tunnel_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME explicit-proxy-tunnel COMMAND ztermy_explicit_proxy_tunnel_tests)

    qt_add_executable(ztermy_ssh_handshake_tests
        tests/ssh_handshake_tests.cpp
    )

    target_compile_features(ztermy_ssh_handshake_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_handshake_tests PRIVATE
        ztermy_ssh_transport
        Qt6::Network
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_handshake_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-handshake COMMAND ztermy_ssh_handshake_tests)

    qt_add_executable(ztermy_ssh_connection_bootstrap_tests
        tests/ssh_connection_bootstrap_tests.cpp
    )

    target_compile_features(ztermy_ssh_connection_bootstrap_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_connection_bootstrap_tests PRIVATE
        ztermy_ssh_connection
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_connection_bootstrap_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-connection-bootstrap COMMAND ztermy_ssh_connection_bootstrap_tests)

    qt_add_executable(ztermy_ssh_real_host_tests
        tests/ssh_real_host_tests.cpp
    )

    target_compile_features(ztermy_ssh_real_host_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_real_host_tests PRIVATE
        ztermy_ssh_connection
        ztermy_ssh_transport
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_real_host_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-real-host COMMAND ztermy_ssh_real_host_tests)

    qt_add_executable(ztermy_ssh_terminal_session_tests
        tests/ssh_terminal_session_tests.cpp
    )

    target_compile_features(ztermy_ssh_terminal_session_tests PRIVATE cxx_std_23)
    set_target_properties(ztermy_ssh_terminal_session_tests PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
    target_link_libraries(ztermy_ssh_terminal_session_tests PRIVATE
        ztermy_ssh_session
        Qt6::Network
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ssh_terminal_session_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ssh-terminal-session COMMAND ztermy_ssh_terminal_session_tests -o -,txt)

    qt_add_executable(ztermy_terminal_engine_tests
        tests/terminal_engine_tests.cpp
    )

    target_compile_features(ztermy_terminal_engine_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_terminal_engine_tests PRIVATE
        ztermy_terminal_engine
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_terminal_engine_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME terminal-engine COMMAND ztermy_terminal_engine_tests)

    qt_add_executable(ztermy_command_block_store_tests
        tests/command_block_store_tests.cpp
    )

    target_compile_features(ztermy_command_block_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_command_block_store_tests PRIVATE
        ztermy_terminal_engine
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_command_block_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME command-block-store COMMAND ztermy_command_block_store_tests)

    qt_add_executable(ztermy_shell_integration_decoder_tests
        tests/shell_integration_decoder_tests.cpp
    )

    target_compile_features(ztermy_shell_integration_decoder_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_shell_integration_decoder_tests PRIVATE
        ztermy_terminal_engine
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_shell_integration_decoder_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME shell-integration-decoder COMMAND ztermy_shell_integration_decoder_tests)

    qt_add_executable(ztermy_command_block_assembler_tests
        tests/command_block_assembler_tests.cpp
    )

    target_compile_features(ztermy_command_block_assembler_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_command_block_assembler_tests PRIVATE
        ztermy_terminal_engine
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_command_block_assembler_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME command-block-assembler COMMAND ztermy_command_block_assembler_tests)

    qt_add_executable(ztermy_semantic_terminal_observer_tests
        tests/semantic_terminal_observer_tests.cpp
    )

    target_compile_features(ztermy_semantic_terminal_observer_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_semantic_terminal_observer_tests PRIVATE
        ztermy_terminal_engine
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_semantic_terminal_observer_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME semantic-terminal-observer COMMAND ztermy_semantic_terminal_observer_tests)

    qt_add_executable(ztermy_ai_stream_parser_tests
        tests/ai_stream_parser_tests.cpp
    )

    target_compile_features(ztermy_ai_stream_parser_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_stream_parser_tests PRIVATE
        ztermy_ai_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_stream_parser_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-stream-parser COMMAND ztermy_ai_stream_parser_tests)

    qt_add_executable(ztermy_mcp_tool_registry_tests
        tests/mcp_tool_registry_tests.cpp
    )

    target_compile_features(ztermy_mcp_tool_registry_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_mcp_tool_registry_tests PRIVATE
        ztermy_ai_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_mcp_tool_registry_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME mcp-tool-registry COMMAND ztermy_mcp_tool_registry_tests)

    qt_add_executable(ztermy_mcp_json_rpc_protocol_tests
        tests/mcp_json_rpc_protocol_tests.cpp
    )

    target_compile_features(ztermy_mcp_json_rpc_protocol_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_mcp_json_rpc_protocol_tests PRIVATE
        ztermy_mcp_protocol
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_mcp_json_rpc_protocol_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME mcp-json-rpc-protocol COMMAND ztermy_mcp_json_rpc_protocol_tests)

    add_executable(ztermy_mcp_test_server tests/mcp_test_server.cpp)
    target_compile_features(ztermy_mcp_test_server PRIVATE cxx_std_23)

    qt_add_executable(ztermy_mcp_stdio_client_tests
        tests/mcp_stdio_client_tests.cpp
    )

    target_compile_features(ztermy_mcp_stdio_client_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_mcp_stdio_client_tests PRIVATE
        ztermy_mcp_protocol
        Qt6::Test
    )
    add_dependencies(ztermy_mcp_stdio_client_tests ztermy_mcp_test_server)

    if(MSVC)
        target_compile_options(ztermy_mcp_stdio_client_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME mcp-stdio-client COMMAND ztermy_mcp_stdio_client_tests)

    qt_add_executable(ztermy_mcp_server_store_tests
        tests/mcp_server_store_tests.cpp
    )

    target_compile_features(ztermy_mcp_server_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_mcp_server_store_tests PRIVATE
        ztermy_mcp_protocol
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_mcp_server_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME mcp-server-store COMMAND ztermy_mcp_server_store_tests)

    qt_add_executable(ztermy_mcp_runtime_manager_tests
        tests/mcp_runtime_manager_tests.cpp
    )

    target_compile_features(ztermy_mcp_runtime_manager_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_mcp_runtime_manager_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )
    add_dependencies(ztermy_mcp_runtime_manager_tests ztermy_mcp_test_server)

    if(MSVC)
        target_compile_options(ztermy_mcp_runtime_manager_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME mcp-runtime-manager COMMAND ztermy_mcp_runtime_manager_tests)

    qt_add_executable(ztermy_ai_mcp_lifecycle_stress_tests
        tests/ai_mcp_lifecycle_stress_tests.cpp
    )

    target_compile_features(ztermy_ai_mcp_lifecycle_stress_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_mcp_lifecycle_stress_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )
    add_dependencies(ztermy_ai_mcp_lifecycle_stress_tests ztermy_mcp_test_server)

    if(MSVC)
        target_compile_options(ztermy_ai_mcp_lifecycle_stress_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-mcp-lifecycle-stress COMMAND ztermy_ai_mcp_lifecycle_stress_tests)
    set_tests_properties(ai-mcp-lifecycle-stress PROPERTIES LABELS "ai;stress" TIMEOUT 120)

    set(ZTERMY_AI_SOAK_SECONDS "120" CACHE STRING "Duration of the provider-independent AI concurrency soak")
    add_custom_target(ztermy_ai_concurrency_soak
        COMMAND
            "${ZTERMY_PWSH_EXECUTABLE}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_ai_concurrency_soak.ps1"
            -BuildDirectory "${CMAKE_CURRENT_BINARY_DIR}"
            -DurationSeconds "${ZTERMY_AI_SOAK_SECONDS}"
            -ReportPath "${CMAKE_CURRENT_BINARY_DIR}/ai-concurrency-soak.json"
        DEPENDS
            ztermy_ai_mcp_lifecycle_stress_tests
            ztermy_ai_turn_runner_tests
            ztermy_ai_agent_guard_tests
            ztermy_ai_tool_dispatch_ledger_tests
            ztermy_ai_read_tool_dispatcher_tests
            ztermy_ai_terminal_frame_tracker_tests
            ztermy_provider_http_client_tests
            ztermy_mcp_stdio_client_tests
            ztermy_mcp_runtime_manager_tests
        COMMENT "Running the bounded provider-independent AI concurrency and cancellation soak"
        USES_TERMINAL
        VERBATIM
    )

    qt_add_executable(ztermy_provider_stream_mapper_tests
        tests/provider_stream_mapper_tests.cpp
    )

    target_compile_features(ztermy_provider_stream_mapper_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_provider_stream_mapper_tests PRIVATE
        ztermy_ai_provider
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_provider_stream_mapper_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME provider-stream-mapper COMMAND ztermy_provider_stream_mapper_tests)

    qt_add_executable(ztermy_provider_request_factory_tests
        tests/provider_request_factory_tests.cpp
    )

    target_compile_features(ztermy_provider_request_factory_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_provider_request_factory_tests PRIVATE
        ztermy_ai_provider
        Qt6::Network
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_provider_request_factory_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME provider-request-factory COMMAND ztermy_provider_request_factory_tests)

    qt_add_executable(ztermy_ai_evaluation_harness_tests
        tests/ai_evaluation_harness_tests.cpp
    )

    target_compile_features(ztermy_ai_evaluation_harness_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_evaluation_harness_tests PRIVATE
        ztermy_ai_evaluation
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_evaluation_harness_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-evaluation-harness COMMAND ztermy_ai_evaluation_harness_tests)

    qt_add_executable(ztermy_ai_privacy_diagnostics_tests
        tests/ai_privacy_diagnostics_tests.cpp
    )

    target_compile_features(ztermy_ai_privacy_diagnostics_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_privacy_diagnostics_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_privacy_diagnostics_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-privacy-diagnostics COMMAND ztermy_ai_privacy_diagnostics_tests)

    qt_add_executable(ztermy_provider_http_client_tests
        tests/provider_http_client_tests.cpp
    )

    target_compile_features(ztermy_provider_http_client_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_provider_http_client_tests PRIVATE
        ztermy_ai_provider
        ztermy_credential_vault
        Qt6::Network
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_provider_http_client_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME provider-http-client COMMAND ztermy_provider_http_client_tests)

    qt_add_executable(ztermy_ai_provider_retry_policy_tests
        tests/ai_provider_retry_policy_tests.cpp
    )

    target_compile_features(ztermy_ai_provider_retry_policy_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_provider_retry_policy_tests PRIVATE
        ztermy_ai_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_provider_retry_policy_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-provider-retry-policy COMMAND ztermy_ai_provider_retry_policy_tests)

    qt_add_executable(ztermy_ai_permission_policy_tests
        tests/ai_permission_policy_tests.cpp
    )

    target_compile_features(ztermy_ai_permission_policy_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_permission_policy_tests PRIVATE
        ztermy_ai_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_permission_policy_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-permission-policy COMMAND ztermy_ai_permission_policy_tests)

    qt_add_executable(ztermy_ai_tool_dispatch_ledger_tests
        tests/ai_tool_dispatch_ledger_tests.cpp
    )

    target_compile_features(ztermy_ai_tool_dispatch_ledger_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_tool_dispatch_ledger_tests PRIVATE
        ztermy_ai_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_tool_dispatch_ledger_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-tool-dispatch-ledger COMMAND ztermy_ai_tool_dispatch_ledger_tests)

    qt_add_executable(ztermy_ai_agent_guard_tests
        tests/ai_agent_guard_tests.cpp
    )

    target_compile_features(ztermy_ai_agent_guard_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_agent_guard_tests PRIVATE
        ztermy_ai_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_agent_guard_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-agent-guard COMMAND ztermy_ai_agent_guard_tests)

    qt_add_executable(ztermy_ai_command_tracker_tests
        tests/ai_command_tracker_tests.cpp
    )

    target_compile_features(ztermy_ai_command_tracker_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_command_tracker_tests PRIVATE
        ztermy_ai_context
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_command_tracker_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-command-tracker COMMAND ztermy_ai_command_tracker_tests)

    qt_add_executable(ztermy_ai_terminal_frame_tracker_tests
        tests/ai_terminal_frame_tracker_tests.cpp
    )

    target_compile_features(ztermy_ai_terminal_frame_tracker_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_terminal_frame_tracker_tests PRIVATE
        ztermy_ai_context
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_terminal_frame_tracker_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-terminal-frame-tracker COMMAND ztermy_ai_terminal_frame_tracker_tests)

    qt_add_executable(ztermy_ai_terminal_capability_adapter_tests
        tests/ai_terminal_capability_adapter_tests.cpp
    )

    target_compile_features(ztermy_ai_terminal_capability_adapter_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_terminal_capability_adapter_tests PRIVATE
        ztermy_ai_context
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_terminal_capability_adapter_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-terminal-capability-adapter COMMAND ztermy_ai_terminal_capability_adapter_tests)

    qt_add_executable(ztermy_ai_terminal_frame_tool_tests
        tests/ai_terminal_frame_tool_tests.cpp
    )

    target_compile_features(ztermy_ai_terminal_frame_tool_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_terminal_frame_tool_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_terminal_frame_tool_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-terminal-frame-tool COMMAND ztermy_ai_terminal_frame_tool_tests)

    qt_add_executable(ztermy_ai_usage_reporting_tests
        tests/ai_usage_reporting_tests.cpp
    )

    target_compile_features(ztermy_ai_usage_reporting_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_usage_reporting_tests PRIVATE
        ztermy_ai_domain
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_usage_reporting_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-usage-reporting COMMAND ztermy_ai_usage_reporting_tests)

    qt_add_executable(ztermy_ai_turn_runner_tests
        tests/ai_turn_runner_tests.cpp
    )

    target_compile_features(ztermy_ai_turn_runner_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_turn_runner_tests PRIVATE
        ztermy_ai_application
        Qt6::Network
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_turn_runner_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-turn-runner COMMAND ztermy_ai_turn_runner_tests)

    qt_add_executable(ztermy_ai_action_tool_dispatcher_tests
        tests/ai_action_tool_dispatcher_tests.cpp
    )

    target_compile_features(ztermy_ai_action_tool_dispatcher_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_action_tool_dispatcher_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_action_tool_dispatcher_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-action-tool-dispatcher COMMAND ztermy_ai_action_tool_dispatcher_tests)

    qt_add_executable(ztermy_ai_wait_command_tool_tests
        tests/ai_wait_command_tool_tests.cpp
    )

    target_compile_features(ztermy_ai_wait_command_tool_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_wait_command_tool_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_wait_command_tool_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-wait-command-tool COMMAND ztermy_ai_wait_command_tool_tests)

    qt_add_executable(ztermy_ai_sftp_read_tool_tests
        tests/ai_sftp_read_tool_tests.cpp
    )

    target_compile_features(ztermy_ai_sftp_read_tool_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_sftp_read_tool_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(ztermy_ai_sftp_read_tool_tests PRIVATE
            /W4 /WX /permissive- /utf-8 /Zc:__cplusplus
        )
    endif()
    add_test(NAME ai-sftp-read-tool COMMAND ztermy_ai_sftp_read_tool_tests)

    qt_add_executable(ztermy_ai_sftp_list_tool_tests
        tests/ai_sftp_list_tool_tests.cpp
    )
    target_compile_features(ztermy_ai_sftp_list_tool_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_sftp_list_tool_tests PRIVATE ztermy_ai_application Qt6::Test)
    if(MSVC)
        target_compile_options(ztermy_ai_sftp_list_tool_tests PRIVATE
            /W4 /WX /permissive- /utf-8 /Zc:__cplusplus
        )
    endif()
    add_test(NAME ai-sftp-list-tool COMMAND ztermy_ai_sftp_list_tool_tests)

    qt_add_executable(ztermy_ai_note_read_tool_tests
        tests/ai_note_read_tool_tests.cpp
    )

    target_compile_features(ztermy_ai_note_read_tool_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_note_read_tool_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(ztermy_ai_note_read_tool_tests PRIVATE
            /W4 /WX /permissive- /utf-8 /Zc:__cplusplus
        )
    endif()
    add_test(NAME ai-note-read-tool COMMAND ztermy_ai_note_read_tool_tests)

    qt_add_executable(ztermy_ai_activity_model_tests
        tests/ai_activity_model_tests.cpp
    )

    target_compile_features(ztermy_ai_activity_model_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_activity_model_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_activity_model_tests PRIVATE
            /permissive-
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-activity-model COMMAND ztermy_ai_activity_model_tests)

    qt_add_executable(ztermy_ai_conversation_store_tests
        tests/ai_conversation_store_tests.cpp
    )

    target_compile_features(ztermy_ai_conversation_store_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_conversation_store_tests PRIVATE
        ztermy_ai_conversation_store
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_conversation_store_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-conversation-store COMMAND ztermy_ai_conversation_store_tests)

    qt_add_executable(ztermy_ai_conversation_history_model_tests
        tests/ai_conversation_history_model_tests.cpp
    )

    target_compile_features(ztermy_ai_conversation_history_model_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_conversation_history_model_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_conversation_history_model_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-conversation-history-model COMMAND ztermy_ai_conversation_history_model_tests)

    qt_add_executable(ztermy_ai_conversation_model_tests
        tests/ai_conversation_model_tests.cpp
    )

    target_compile_features(ztermy_ai_conversation_model_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_conversation_model_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_conversation_model_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-conversation-model COMMAND ztermy_ai_conversation_model_tests)

    qt_add_executable(ztermy_ai_read_tool_dispatcher_tests
        tests/ai_read_tool_dispatcher_tests.cpp
    )

    target_compile_features(ztermy_ai_read_tool_dispatcher_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_read_tool_dispatcher_tests PRIVATE
        ztermy_ai_application
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_read_tool_dispatcher_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-read-tool-dispatcher COMMAND ztermy_ai_read_tool_dispatcher_tests)

    qt_add_executable(ztermy_ai_context_broker_tests
        tests/ai_context_broker_tests.cpp
    )

    target_compile_features(ztermy_ai_context_broker_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_context_broker_tests PRIVATE
        ztermy_ai_context
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_context_broker_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-context-broker COMMAND ztermy_ai_context_broker_tests)

    qt_add_executable(ztermy_ai_read_tools_tests
        tests/ai_read_tools_tests.cpp
    )

    target_compile_features(ztermy_ai_read_tools_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_read_tools_tests PRIVATE
        ztermy_ai_context
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_read_tools_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-read-tools COMMAND ztermy_ai_read_tools_tests)

    qt_add_executable(ztermy_ai_context_redactor_tests
        tests/ai_context_redactor_tests.cpp
    )

    target_compile_features(ztermy_ai_context_redactor_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_context_redactor_tests PRIVATE
        ztermy_ai_context
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_context_redactor_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-context-redactor COMMAND ztermy_ai_context_redactor_tests)

    qt_add_executable(ztermy_ai_context_serializer_tests
        tests/ai_context_serializer_tests.cpp
    )

    target_compile_features(ztermy_ai_context_serializer_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ai_context_serializer_tests PRIVATE
        ztermy_ai_context
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_ai_context_serializer_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME ai-context-serializer COMMAND ztermy_ai_context_serializer_tests)

    qt_add_executable(ztermy_local_terminal_session_tests
        tests/local_terminal_session_tests.cpp
    )

    target_compile_features(ztermy_local_terminal_session_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_local_terminal_session_tests PRIVATE
        ztermy_terminal_session
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_local_terminal_session_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME local-terminal-session COMMAND ztermy_local_terminal_session_tests)
    set_tests_properties(local-terminal-session PROPERTIES
        TIMEOUT 3900
        ENVIRONMENT "QTEST_FUNCTION_TIMEOUT=3600000"
    )

    set(ZTERMY_TERMINAL_SOAK_SECONDS "1800" CACHE STRING
        "Duration of the local terminal stability soak")
    add_custom_target(ztermy_terminal_stability_soak
        COMMAND
            "${ZTERMY_PWSH_EXECUTABLE}" -NoProfile -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_terminal_stability_soak.ps1"
            -BuildDirectory "${CMAKE_CURRENT_BINARY_DIR}"
            -DurationSeconds "${ZTERMY_TERMINAL_SOAK_SECONDS}"
            -ReportPath "${CMAKE_CURRENT_BINARY_DIR}/terminal-stability-soak.json"
        DEPENDS ztermy_local_terminal_session_tests
        COMMENT "Running the bounded local terminal latency and lifecycle soak"
        USES_TERMINAL
        VERBATIM
    )

    qt_add_executable(ztermy_app_controller_tests
        tests/app_controller_tests.cpp
        src/application/AppController.cpp
        src/application/AppController.h
    )

    target_compile_features(ztermy_app_controller_tests PRIVATE cxx_std_23)
    target_include_directories(ztermy_app_controller_tests PRIVATE src)
    target_compile_definitions(ztermy_app_controller_tests PRIVATE
        NOMINMAX
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
    )
    target_link_libraries(ztermy_app_controller_tests PRIVATE
        ztermy_ai_application
        ztermy_ai_context
        ztermy_application_actions
        ztermy_credential_vault
        ztermy_core_config
        ztermy_forwarding_persistence
        ztermy_forwarding_session
        ztermy_ssh_persistence
        ztermy_ssh_session
        ztermy_sftp_session
        ztermy_session_logging
        ztermy_terminal_session
        ztermy_terminal_ui
        ztermy_windows_clipboard
        ztermy_workbench_persistence
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_app_controller_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME app-controller COMMAND ztermy_app_controller_tests)

    qt_add_executable(ztermy_windows_protected_clipboard_tests
        tests/windows_protected_clipboard_tests.cpp
    )
    target_compile_features(ztermy_windows_protected_clipboard_tests PRIVATE cxx_std_23)
    target_compile_definitions(ztermy_windows_protected_clipboard_tests PRIVATE
        NOMINMAX
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
    )
    target_link_libraries(ztermy_windows_protected_clipboard_tests PRIVATE
        ztermy_windows_clipboard
        Qt6::Test
        user32
    )
    if(MSVC)
        target_compile_options(ztermy_windows_protected_clipboard_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()
    add_test(NAME windows-protected-clipboard COMMAND ztermy_windows_protected_clipboard_tests)
    set_tests_properties(windows-protected-clipboard PROPERTIES RUN_SERIAL TRUE)

    qt_add_executable(ztermy_terminal_item_tests
        tests/terminal_item_tests.cpp
    )

    target_compile_features(ztermy_terminal_item_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_terminal_item_tests PRIVATE
        ztermy_terminal_ui
        Qt6::Test
    )

    if(MSVC)
        target_compile_options(ztermy_terminal_item_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME terminal-item COMMAND ztermy_terminal_item_tests)

    qt_add_executable(ztermy_svg_icon_image_provider_tests
        tests/svg_icon_image_provider_tests.cpp
    )
    target_compile_features(ztermy_svg_icon_image_provider_tests PRIVATE cxx_std_23)
    target_compile_definitions(ztermy_svg_icon_image_provider_tests PRIVATE
        ZTERMY_TEST_BRANDING_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources/branding"
        ZTERMY_TEST_ICON_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    )
    target_link_libraries(ztermy_svg_icon_image_provider_tests PRIVATE
        ztermy_icon_rendering
        Qt6::Test
    )
    if(MSVC)
        target_compile_options(ztermy_svg_icon_image_provider_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()
    add_test(NAME svg-icon-image-provider COMMAND ztermy_svg_icon_image_provider_tests)

    qt_add_executable(ztermy_windows_executable_metadata_tests
        tests/windows_executable_metadata_tests.cpp
    )

    target_compile_features(ztermy_windows_executable_metadata_tests PRIVATE cxx_std_23)
    target_compile_definitions(ztermy_windows_executable_metadata_tests PRIVATE
        NOMINMAX
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
        ZTERMY_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        ZTERMY_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        ZTERMY_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        ZTERMY_VERSION_STRING="${PROJECT_VERSION}"
    )
    target_link_libraries(ztermy_windows_executable_metadata_tests PRIVATE
        Qt6::Test
        version
    )
    add_dependencies(ztermy_windows_executable_metadata_tests ztermy)

    if(MSVC)
        target_compile_options(ztermy_windows_executable_metadata_tests PRIVATE
            /permissive-
            /utf-8
            /W4
            /Zc:__cplusplus
        )
    endif()

    add_test(NAME windows-executable-metadata COMMAND ztermy_windows_executable_metadata_tests)

    add_test(
        NAME qml-native-window-smoke
        COMMAND ztermy
            --smoke-test
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/qml-native-window-smoke"
    )
endif()

if(ZTERMY_STATIC_BUILD)
    if(CMAKE_VERSION VERSION_LESS 4.3)
        message(FATAL_ERROR
            "The per-user WiX installer requires CMake 4.3 or newer. "
            "Use a current CMake release for the msvc-static-release preset."
        )
    endif()

    set(CPACK_GENERATOR "WIX")
    set(CPACK_WIX_VERSION "4")
    set(CPACK_PACKAGE_NAME "ztermy")
    set(CPACK_PACKAGE_VENDOR "ztermy")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A native Windows 11 SSH terminal")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "ztermy")
    set(CPACK_PACKAGE_FILE_NAME "ztermy-${PROJECT_VERSION}-windows-x64")
    set(CPACK_PACKAGE_EXECUTABLES "ztermy" "ztermy")
    set(CPACK_INSTALL_CMAKE_PROJECTS
        "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/"
    )
    set(CPACK_WIX_UPGRADE_GUID "5E502533-5081-4401-9549-C5403DFD123A")
    set(CPACK_WIX_ARCHITECTURE "x64")
    set(CPACK_WIX_INSTALL_SCOPE "perUser")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER ".")
    set(CPACK_WIX_TEMPLATE
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackWixNoLicense.template.in"
    )
    set(CPACK_WIX_SKIP_WIX_UI_EXTENSION TRUE)
    set(CPACK_WIX_ROOT_FEATURE_TITLE "ztermy")
    set(CPACK_WIX_ROOT_FEATURE_DESCRIPTION "Native SSH terminal application")
    set(CPACK_WIX_PROPERTY_ARPCOMMENTS "Windows 11 native SSH terminal")
    set(CPACK_WIX_PRODUCT_ICON "${ZTERMY_WINDOWS_ICON_PATH}")

    include(CPack)

    set(ZTERMY_WIX_DEFAULT_ROOT "$ENV{WIX}")
    if(ZTERMY_WIX_DEFAULT_ROOT STREQUAL ""
       AND EXISTS "${CMAKE_SOURCE_DIR}/build/tools/wix/wix.exe")
        set(ZTERMY_WIX_DEFAULT_ROOT "${CMAKE_SOURCE_DIR}/build/tools/wix")
    endif()
    set(
        ZTERMY_WIX_ROOT
        "${ZTERMY_WIX_DEFAULT_ROOT}"
        CACHE PATH
        "Directory containing the WiX 4 wix.exe used for installer packaging and verification"
    )
    unset(ZTERMY_WIX_DEFAULT_ROOT)

    add_custom_target(ztermy_installer
        COMMAND "${CMAKE_COMMAND}" -E env
                "WIX=${ZTERMY_WIX_ROOT}"
                "${CMAKE_CPACK_COMMAND}"
                --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
                -G WIX
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        DEPENDS ztermy
        COMMENT "Creating the per-user ztermy MSI installer"
        VERBATIM
    )

    add_custom_target(ztermy_installer_contract_smoke
        COMMAND "${CMAKE_COMMAND}"
                "-DZTERMY_WIX_EXECUTABLE=${ZTERMY_WIX_ROOT}/wix.exe"
                "-DZTERMY_INSTALLER=${CMAKE_BINARY_DIR}/ztermy-${PROJECT_VERSION}-windows-x64.msi"
                "-DZTERMY_INSTALLER_INSPECTION_ROOT=${CMAKE_BINARY_DIR}/test-data/installer-contract"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyWixInstaller.cmake"
        DEPENDS ztermy_installer
        COMMENT "Validating and inspecting the per-user ztermy MSI contract"
        VERBATIM
    )

    set(ZTERMY_RELEASE_BUNDLE_ROOT
        "${CMAKE_BINARY_DIR}/package/release/ztermy-${PROJECT_VERSION}-windows-x64"
    )
    add_custom_target(ztermy_release_bundle
        COMMAND "${CMAKE_COMMAND}"
                "-DZTERMY_BUILD_ROOT=${CMAKE_BINARY_DIR}"
                "-DZTERMY_RELEASE_BUNDLE_ROOT=${ZTERMY_RELEASE_BUNDLE_ROOT}"
                "-DZTERMY_RELEASE_VERSION=${PROJECT_VERSION}"
                "-DZTERMY_PORTABLE_ARCHIVE=${ZTERMY_PORTABLE_PACKAGE_ROOT}/ztermy-${PROJECT_VERSION}-windows-x64-portable.zip"
                "-DZTERMY_INSTALLER=${CMAKE_BINARY_DIR}/ztermy-${PROJECT_VERSION}-windows-x64.msi"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssembleReleaseBundle.cmake"
        DEPENDS
            ztermy_portable_package
            ztermy_installer_contract_smoke
        COMMENT "Assembling the checksummed ztermy release bundle"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_test(
        NAME interface-icon-assets
        COMMAND "${CMAKE_COMMAND}"
                "-DZTERMY_ICON_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyInterfaceIcons.cmake"
    )

    add_test(
        NAME branding-assets
        COMMAND "${CMAKE_COMMAND}"
                "-DZTERMY_WINDOWS_ICON=${ZTERMY_WINDOWS_ICON_PATH}"
                "-DZTERMY_BRANDING_PNG_DIRECTORY=${ZTERMY_GENERATED_BRANDING_DIRECTORY}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyBrandingAssets.cmake"
    )

    add_custom_target(ztermy_test_binaries
        DEPENDS
            ztermy_remote_telemetry_tests
            ztermy_action_registry_tests
            ztermy_diagnostic_reporter_tests
            ztermy_application_paths_tests
            ztermy_application_settings_tests
            ztermy_last_known_good_file_tests
            ztermy_localization_manager_tests
            ztermy_latency_histogram_tests
            ztermy_window_hit_test_tests
            ztermy_conpty_process_tests
            ztermy_ssh_runtime_tests
            ztermy_ssh_connection_state_tests
            ztermy_ssh_host_key_tests
            ztermy_known_hosts_store_tests
            ztermy_ssh_profile_store_tests
            ztermy_ssh_target_tests
            ztermy_windows_tcp_socket_tests
            ztermy_explicit_proxy_tunnel_tests
            ztermy_ssh_handshake_tests
            ztermy_ssh_connection_bootstrap_tests
            ztermy_ssh_real_host_tests
            ztermy_ssh_terminal_session_tests
            ztermy_sftp_types_tests
            ztermy_port_forwarding_rule_store_tests
            ztermy_socks5_protocol_tests
            ztermy_port_forwarding_job_tests
            ztermy_port_forwarding_real_host_tests
            ztermy_session_log_writer_tests
            ztermy_transfer_queue_tests
            ztermy_transfer_batch_tests
            ztermy_transfer_planner_tests
            ztermy_transfer_source_adapters_tests
            ztermy_transfer_batch_materializer_tests
            ztermy_transfer_batch_coordinator_tests
            ztermy_sftp_session_tests
            ztermy_sftp_directory_model_tests
            ztermy_transfer_executor_tests
            ztermy_transfer_manager_tests
            ztermy_transfer_recovery_store_tests
            ztermy_transfer_batch_recovery_store_tests
            ztermy_workspace_state_store_tests
            ztermy_terminal_engine_tests
            ztermy_command_block_store_tests
            ztermy_shell_integration_decoder_tests
            ztermy_command_block_assembler_tests
            ztermy_semantic_terminal_observer_tests
            ztermy_ai_stream_parser_tests
            ztermy_provider_stream_mapper_tests
            ztermy_provider_request_factory_tests
            ztermy_provider_http_client_tests
            ztermy_ai_provider_retry_policy_tests
            ztermy_ai_permission_policy_tests
            ztermy_ai_tool_dispatch_ledger_tests
            ztermy_ai_agent_guard_tests
            ztermy_ai_command_tracker_tests
            ztermy_ai_action_tool_dispatcher_tests
            ztermy_ai_wait_command_tool_tests
            ztermy_ai_sftp_read_tool_tests
            ztermy_ai_sftp_list_tool_tests
            ztermy_ai_note_read_tool_tests
            ztermy_ai_turn_runner_tests
            ztermy_ai_conversation_store_tests
            ztermy_ai_conversation_history_model_tests
            ztermy_ai_conversation_model_tests
            ztermy_ai_read_tool_dispatcher_tests
            ztermy_ai_context_broker_tests
            ztermy_ai_read_tools_tests
            ztermy_ai_context_redactor_tests
            ztermy_ai_context_serializer_tests
            ztermy_local_terminal_session_tests
            ztermy_app_controller_tests
            ztermy_windows_protected_clipboard_tests
            ztermy_terminal_item_tests
            ztermy_font_catalog_tests
            ztermy_svg_icon_image_provider_tests
            ztermy_windows_executable_metadata_tests
    )
endif()

find_program(ZTERMY_CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
find_program(ZTERMY_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)

cmake_host_system_information(
    RESULT ZTERMY_PHYSICAL_CORE_COUNT
    QUERY NUMBER_OF_PHYSICAL_CORES
)
if(NOT ZTERMY_PHYSICAL_CORE_COUNT MATCHES "^[1-9][0-9]*$")
    set(ZTERMY_PHYSICAL_CORE_COUNT 2)
endif()
math(EXPR ZTERMY_CLANG_TIDY_DEFAULT_JOBS
    "(${ZTERMY_PHYSICAL_CORE_COUNT} + 1) / 2"
)
if(ZTERMY_CLANG_TIDY_DEFAULT_JOBS GREATER 4)
    set(ZTERMY_CLANG_TIDY_DEFAULT_JOBS 4)
endif()
set(
    ZTERMY_CLANG_TIDY_JOBS
    "${ZTERMY_CLANG_TIDY_DEFAULT_JOBS}"
    CACHE STRING
    "Maximum concurrent clang-tidy translation units"
)
if(NOT ZTERMY_CLANG_TIDY_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "ZTERMY_CLANG_TIDY_JOBS must be a positive integer; found: "
        "${ZTERMY_CLANG_TIDY_JOBS}"
    )
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS
    "ztermy_clang_tidy_pool=${ZTERMY_CLANG_TIDY_JOBS}"
)

execute_process(
    COMMAND "${ZTERMY_CLANG_FORMAT_EXECUTABLE}" --version
    RESULT_VARIABLE ZTERMY_CLANG_FORMAT_VERSION_RESULT
    OUTPUT_VARIABLE ZTERMY_CLANG_FORMAT_VERSION_OUTPUT
    ERROR_VARIABLE ZTERMY_CLANG_FORMAT_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ZTERMY_CLANG_FORMAT_VERSION_RESULT EQUAL 0
   OR NOT ZTERMY_CLANG_FORMAT_VERSION_OUTPUT MATCHES "version ([0-9]+\\.[0-9]+(\\.[0-9]+)?)"
   OR CMAKE_MATCH_1 VERSION_LESS "22.1")
    message(FATAL_ERROR
        "ztermy requires clang-format 22.1 or newer for its formatting gate; "
        "found: ${ZTERMY_CLANG_FORMAT_VERSION_OUTPUT}"
    )
endif()

execute_process(
    COMMAND "${ZTERMY_CLANG_TIDY_EXECUTABLE}" --version
    RESULT_VARIABLE ZTERMY_CLANG_TIDY_VERSION_RESULT
    OUTPUT_VARIABLE ZTERMY_CLANG_TIDY_VERSION_OUTPUT
    ERROR_VARIABLE ZTERMY_CLANG_TIDY_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT ZTERMY_CLANG_TIDY_VERSION_RESULT EQUAL 0
   OR NOT ZTERMY_CLANG_TIDY_VERSION_OUTPUT MATCHES "version ([0-9]+\\.[0-9]+(\\.[0-9]+)?)"
   OR CMAKE_MATCH_1 VERSION_LESS "22.1")
    message(FATAL_ERROR
        "ztermy requires clang-tidy 22.1 or newer for its static-analysis gate; "
        "found: ${ZTERMY_CLANG_TIDY_VERSION_OUTPUT}"
    )
endif()

file(GLOB_RECURSE ZTERMY_FORMAT_SOURCES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.h"
)
file(GLOB_RECURSE ZTERMY_TIDY_SOURCES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
if(BUILD_TESTING)
    file(GLOB_RECURSE ZTERMY_TIDY_TEST_SOURCES
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES FALSE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    list(APPEND ZTERMY_TIDY_SOURCES ${ZTERMY_TIDY_TEST_SOURCES})
endif()
file(GLOB_RECURSE ZTERMY_TIDY_TOOL_SOURCES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES FALSE
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp"
)
list(APPEND ZTERMY_TIDY_SOURCES ${ZTERMY_TIDY_TOOL_SOURCES})
list(SORT ZTERMY_FORMAT_SOURCES)
list(SORT ZTERMY_TIDY_SOURCES)

add_custom_target(ztermy_format_check
    COMMAND "${ZTERMY_CLANG_FORMAT_EXECUTABLE}"
            --dry-run
            --Werror
            ${ZTERMY_FORMAT_SOURCES}
    COMMENT "Checking C++ formatting"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_clang_tidy_prerequisites)
add_dependencies(ztermy_clang_tidy_prerequisites ztermy)
if(BUILD_TESTING)
    add_dependencies(ztermy_clang_tidy_prerequisites ztermy_test_binaries)
endif()

set(ZTERMY_CLANG_TIDY_TARGETS)
set(ZTERMY_CLANG_TIDY_INDEX 0)
foreach(tidy_source IN LISTS ZTERMY_TIDY_SOURCES)
    math(EXPR ZTERMY_CLANG_TIDY_INDEX "${ZTERMY_CLANG_TIDY_INDEX} + 1")
    set(tidy_target "ztermy_clang_tidy_tu_${ZTERMY_CLANG_TIDY_INDEX}")
    file(RELATIVE_PATH tidy_source_display
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${tidy_source}"
    )
    add_custom_target(${tidy_target}
        COMMAND "${ZTERMY_CLANG_TIDY_EXECUTABLE}"
                -p "${CMAKE_BINARY_DIR}"
                --warnings-as-errors=*
                --quiet
                "${tidy_source}"
        COMMENT "clang-tidy ${tidy_source_display}"
        JOB_POOL ztermy_clang_tidy_pool
        VERBATIM
    )
    add_dependencies(${tidy_target} ztermy_clang_tidy_prerequisites)
    list(APPEND ZTERMY_CLANG_TIDY_TARGETS ${tidy_target})
endforeach()

add_custom_target(ztermy_clang_tidy_check
    DEPENDS ${ZTERMY_CLANG_TIDY_TARGETS}
)

unset(ZTERMY_CLANG_TIDY_DEFAULT_JOBS)
unset(ZTERMY_CLANG_TIDY_INDEX)
unset(ZTERMY_CLANG_TIDY_TARGETS)
unset(ZTERMY_PHYSICAL_CORE_COUNT)

if(NOT TARGET Qt6::qmlformat)
    message(FATAL_ERROR "Qt6::qmlformat is required for the QML formatting gate")
endif()

set(ZTERMY_QML_FORMAT_WORK_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/test-data/qml-format-check"
)
add_custom_target(ztermy_qml_format_check
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${ZTERMY_QML_FORMAT_WORK_ROOT}"
    COMMAND "${CMAKE_COMMAND}"
            "-DZTERMY_QMLFORMAT_EXECUTABLE=$<TARGET_FILE:Qt6::qmlformat>"
            "-DZTERMY_QML_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qml"
            "-DZTERMY_QML_FORMAT_WORK_ROOT=${ZTERMY_QML_FORMAT_WORK_ROOT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckQmlFormat.cmake"
    COMMENT "Checking QML formatting with Qt qmlformat"
    USES_TERMINAL
    VERBATIM
)

add_custom_target(ztermy_qml_quality_check
    DEPENDS
        ztermy_qml_format_check
        ztermy_qmllint
)

if(BUILD_TESTING)
    set(ZTERMY_V1_PREFLIGHT_PREREQUISITES
        ztermy_test_binaries
        ztermy_qml_quality_check
    )

    if(ZTERMY_STATIC_BUILD)
        list(APPEND ZTERMY_V1_PREFLIGHT_PREREQUISITES
            ztermy_release_bundle
        )
    endif()

    add_custom_target(ztermy_v1_preflight_prerequisites
        DEPENDS ${ZTERMY_V1_PREFLIGHT_PREREQUISITES}
    )

    set(ZTERMY_V1_PREFLIGHT_DATA_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/v1-runtime-preflight"
    )
    add_custom_target(ztermy_v1_runtime_preflight
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
                "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}"
        COMMAND "$<TARGET_FILE:ztermy>"
                --window-runtime-smoke
                --data-dir "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/window-work-area"
        COMMAND "$<TARGET_FILE:ztermy>"
                --window-appearance-smoke
                --data-dir "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/window-appearance"
        COMMAND "$<TARGET_FILE:ztermy>"
                --window-resize-smoke
                --data-dir "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/window-resize"
        COMMAND "${CMAKE_COMMAND}"
                "-DZTERMY_DPI_SMOKE_EXECUTABLE=$<TARGET_FILE:ztermy>"
                "-DZTERMY_DPI_SMOKE_DATA_ROOT=${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/window-dpi"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunWindowDpiSmoke.cmake"
        COMMAND "$<TARGET_FILE:ztermy>"
                --ui-layout-smoke
                --data-dir "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/ui-layout"
        COMMAND "$<TARGET_FILE:ztermy>"
                --ui-keyboard-smoke
                --data-dir "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/ui-keyboard"
        COMMAND "$<TARGET_FILE:ztermy>"
                --terminal-render-smoke
                --data-dir "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/terminal-render"
        COMMAND "$<TARGET_FILE:ztermy>"
                --lifecycle-runtime-smoke
                --data-dir "${ZTERMY_V1_PREFLIGHT_DATA_ROOT}/lifecycle"
        DEPENDS
            ztermy_v1_preflight_prerequisites
            ztermy_clang_tidy_check
        COMMENT "Running all eight real-window gates serially"
        USES_TERMINAL
        VERBATIM
    )

    add_custom_target(ztermy_v1_automated_preflight
        COMMAND "${CMAKE_CTEST_COMMAND}"
                --test-dir "${CMAKE_BINARY_DIR}"
                -E "^ssh-real-host$"
                --output-on-failure
        DEPENDS
            ztermy_format_check
            ztermy_clang_tidy_check
            ztermy_v1_runtime_preflight
        COMMENT "Running the complete automated V1 preflight"
        USES_TERMINAL
        VERBATIM
    )

    # Keep the V1 target as a compatible spelling for existing developer
    # workflows while exposing the product milestone used by current release
    # documentation. The preflight is safe by construction: real-host tests
    # remain explicit, separately authorized checks.
    add_custom_target(ztermy_v2_automated_preflight
        DEPENDS ztermy_v1_automated_preflight
    )

    set(ZTERMY_V2_14_STABILITY_DATA_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/v2-14-stability"
    )
    add_custom_target(ztermy_v2_14_stability_core
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
                "${ZTERMY_V2_14_STABILITY_DATA_ROOT}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "ZTERMY_RUN_LOCAL_SOAK_GATE=1"
                "ZTERMY_LOCAL_SOAK_SECONDS=30"
                "$<TARGET_FILE:ztermy_local_terminal_session_tests>"
                survivesSustainedInteractionWithoutLatencyGrowth
        COMMAND "${CMAKE_CTEST_COMMAND}"
                --test-dir "${CMAKE_BINARY_DIR}"
                -R "^(last-known-good-file|application-settings|ssh-profile-store|port-forwarding-rule-store|script-store|workspace-state-store|credential-vault|session-log-writer|sftp-session|transfer-manager|transfer-recovery-store|transfer-batch-recovery-store|port-forwarding-job|ssh-runtime|app-controller|diagnostic-reporter)$"
                --output-on-failure
        DEPENDS ztermy_test_binaries
        COMMENT "Running the bounded V2.14 persistence and lifecycle stability core"
        USES_TERMINAL
        VERBATIM
    )

    add_custom_target(ztermy_v2_14_stability_preflight
        DEPENDS
            ztermy_v2_14_stability_core
            ztermy_v1_runtime_preflight
        COMMENT "Running the complete V2.14 stability and Windows runtime preflight"
    )

    unset(ZTERMY_V1_PREFLIGHT_DATA_ROOT)
    unset(ZTERMY_V1_PREFLIGHT_PREREQUISITES)
    unset(ZTERMY_V2_14_STABILITY_DATA_ROOT)
endif()

unset(ZTERMY_FORMAT_SOURCES)
unset(ZTERMY_TIDY_SOURCES)
unset(ZTERMY_TIDY_TEST_SOURCES)
unset(ZTERMY_TIDY_TOOL_SOURCES)
unset(ZTERMY_QML_FORMAT_WORK_ROOT)
unset(ZTERMY_RELEASE_BUNDLE_ROOT)
unset(ZTERMY_CLANG_FORMAT_VERSION_OUTPUT)
unset(ZTERMY_CLANG_FORMAT_VERSION_RESULT)
unset(ZTERMY_CLANG_TIDY_VERSION_OUTPUT)
unset(ZTERMY_CLANG_TIDY_VERSION_RESULT)
