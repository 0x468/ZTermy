cmake_minimum_required(VERSION 3.28)

project(
    ztermy
    VERSION 0.1.0
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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui LinguistTools Quick QuickControls2)

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

add_executable(ztermy_windows_icon_generator
    tools/WindowsIconGenerator.cpp
)
target_compile_features(ztermy_windows_icon_generator PRIVATE cxx_std_23)
set_target_properties(ztermy_windows_icon_generator PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
)

set(ZTERMY_WINDOWS_ICON_PATH "${ZTERMY_GENERATED_DIRECTORY}/ztermy.ico")
add_custom_command(
    OUTPUT "${ZTERMY_WINDOWS_ICON_PATH}"
    COMMAND "$<TARGET_FILE:ztermy_windows_icon_generator>"
            "${ZTERMY_WINDOWS_ICON_PATH}"
    DEPENDS ztermy_windows_icon_generator
    COMMENT "Generating the deterministic ztermy Windows icon"
    VERBATIM
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

add_library(ztermy_core_diagnostics STATIC
    src/core/diagnostics/LatencyHistogram.cpp
    src/core/diagnostics/LatencyHistogram.h
)

add_library(ztermy_credential_vault STATIC
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
target_link_libraries(ztermy_core_config PUBLIC Qt6::Core)

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
    src/infrastructure/ssh/Libssh2Runtime.cpp
    src/infrastructure/ssh/Libssh2Runtime.h
    src/infrastructure/ssh/Libssh2Session.cpp
    src/infrastructure/ssh/Libssh2Session.h
    src/infrastructure/ssh/WindowsTcpSocket.cpp
    src/infrastructure/ssh/WindowsTcpSocket.h
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

add_library(ztermy_ssh_persistence STATIC
    src/infrastructure/ssh/KnownHostsStore.cpp
    src/infrastructure/ssh/KnownHostsStore.h
    src/infrastructure/ssh/SshProfileStore.cpp
    src/infrastructure/ssh/SshProfileStore.h
)

add_library(ztermy_workbench_domain STATIC
    src/domain/workbench/QuickCommand.cpp
    src/domain/workbench/QuickCommand.h
    src/domain/workbench/ShellHistory.cpp
    src/domain/workbench/ShellHistory.h
)

add_library(ztermy_workbench_persistence STATIC
    src/infrastructure/workbench/PowerShellHistoryReader.cpp
    src/infrastructure/workbench/PowerShellHistoryReader.h
    src/infrastructure/workbench/QuickCommandStore.cpp
    src/infrastructure/workbench/QuickCommandStore.h
)

target_compile_features(ztermy_workbench_domain PUBLIC cxx_std_23)
target_include_directories(ztermy_workbench_domain PUBLIC src)

target_compile_features(ztermy_workbench_persistence PUBLIC cxx_std_23)
target_include_directories(ztermy_workbench_persistence PUBLIC src)
target_link_libraries(ztermy_workbench_persistence
    PUBLIC ztermy_workbench_domain
    PRIVATE Qt6::Core
)

target_compile_features(ztermy_ssh_domain PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_domain PUBLIC src)

target_compile_features(ztermy_ssh_persistence PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_persistence PUBLIC src)
target_link_libraries(ztermy_ssh_persistence
    PUBLIC ztermy_ssh_domain
    PRIVATE Qt6::Core
)

target_compile_features(ztermy_ssh_transport PUBLIC cxx_std_23)
target_include_directories(ztermy_ssh_transport PUBLIC src)
target_link_libraries(ztermy_ssh_transport
    PUBLIC
        ztermy_ssh_domain
    PRIVATE
        libssh2::libssh2_static
        ws2_32
)

add_library(ztermy_terminal_engine STATIC
    src/domain/terminal/GhosttyTerminalEngine.cpp
    src/domain/terminal/GhosttyTerminalEngine.h
    src/domain/terminal/TerminalEngine.h
)

target_compile_features(ztermy_terminal_engine PUBLIC cxx_std_23)
target_include_directories(ztermy_terminal_engine PUBLIC src)
target_link_libraries(ztermy_terminal_engine PRIVATE ghostty-vt-static)

add_library(ztermy_terminal_session STATIC
    src/application/terminal/LocalTerminalSession.cpp
    src/application/terminal/LocalTerminalSession.h
)

add_library(ztermy_ssh_session STATIC
    src/application/ssh/SshTerminalSession.cpp
    src/application/ssh/SshTerminalSession.h
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
target_link_libraries(ztermy_ssh_session
    PUBLIC
        ztermy_core_diagnostics
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
    src/ui/terminal/TerminalTextLayout.cpp
    src/ui/terminal/TerminalTextLayout.h
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

set_source_files_properties(
    src/ui/qml/ActionButton.qml
    PROPERTIES QT_RESOURCE_ALIAS ActionButton.qml
)
set_source_files_properties(
    src/ui/qml/AppTextField.qml
    PROPERTIES QT_RESOURCE_ALIAS AppTextField.qml
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
    src/ui/qml/TerminalWorkbench.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalWorkbench.qml
)
set_source_files_properties(
    src/ui/qml/TerminalComposer.qml
    PROPERTIES QT_RESOURCE_ALIAS TerminalComposer.qml
)

qt_add_qml_module(ztermy
    URI Ztermy
    VERSION 1.0
    DEPENDENCIES
        QtQuick
    QML_FILES
        src/ui/qml/ActionButton.qml
        src/ui/qml/AppCheckBox.qml
        src/ui/qml/AppComboBox.qml
        src/ui/qml/AppIcon.qml
        src/ui/qml/AppMenu.qml
        src/ui/qml/AppMenuItem.qml
        src/ui/qml/AppMenuSeparator.qml
        src/ui/qml/AppSlider.qml
        src/ui/qml/AppSpinBox.qml
        src/ui/qml/AppSwitch.qml
        src/ui/qml/AppTextField.qml
        src/ui/qml/AppToolTip.qml
        src/ui/qml/CaptionButton.qml
        src/ui/qml/ConfirmationDialog.qml
        src/ui/qml/FontPicker.qml
        src/ui/qml/HostConnectionPane.qml
        src/ui/qml/HostKeyPrompt.qml
        src/ui/qml/KeyboardAction.qml
        src/ui/qml/Main.qml
        src/ui/qml/SectionCard.qml
        src/ui/qml/SettingsPane.qml
        src/ui/qml/SideNavigationItem.qml
        src/ui/qml/StatePanel.qml
        src/ui/qml/StatusMessage.qml
        src/ui/qml/TerminalTabAction.qml
        src/ui/qml/TerminalComposer.qml
        src/ui/qml/TerminalWorkbench.qml
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
    ztermy_credential_vault
    ztermy_core_config
    ztermy_ssh_persistence
    ztermy_ssh_session
    ztermy_terminal_session
    ztermy_terminal_transport
    ztermy_terminal_ui
    ztermy_workbench_persistence
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    dbghelp
    dwmapi
)

add_custom_target(ztermy_window_runtime_smoke
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

    qt_add_executable(ztermy_ssh_real_host_tests
        tests/ssh_real_host_tests.cpp
    )

    target_compile_features(ztermy_ssh_real_host_tests PRIVATE cxx_std_23)
    target_link_libraries(ztermy_ssh_real_host_tests PRIVATE
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
        ztermy_credential_vault
        ztermy_core_config
        ztermy_ssh_persistence
        ztermy_ssh_session
        ztermy_terminal_session
        ztermy_terminal_ui
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
    add_custom_target(ztermy_test_binaries
        DEPENDS
            ztermy_application_paths_tests
            ztermy_application_settings_tests
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
            ztermy_ssh_handshake_tests
            ztermy_ssh_real_host_tests
            ztermy_ssh_terminal_session_tests
            ztermy_terminal_engine_tests
            ztermy_local_terminal_session_tests
            ztermy_app_controller_tests
            ztermy_terminal_item_tests
            ztermy_font_catalog_tests
            ztermy_windows_executable_metadata_tests
    )
endif()

find_program(ZTERMY_CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)
find_program(ZTERMY_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)

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

add_custom_target(ztermy_clang_tidy_check
    COMMAND "${ZTERMY_CLANG_TIDY_EXECUTABLE}"
            -p "${CMAKE_BINARY_DIR}"
            --warnings-as-errors=*
            --quiet
            ${ZTERMY_TIDY_SOURCES}
    DEPENDS ztermy
    COMMENT "Running clang-tidy with all diagnostics treated as errors"
    USES_TERMINAL
    VERBATIM
)

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
    add_dependencies(ztermy_clang_tidy_check ztermy_test_binaries)

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
        DEPENDS ztermy_v1_preflight_prerequisites
        COMMENT "Running all seven real-window V1 gates serially"
        USES_TERMINAL
        VERBATIM
    )

    add_custom_target(ztermy_v1_automated_preflight
        COMMAND "${CMAKE_CTEST_COMMAND}"
                --test-dir "${CMAKE_BINARY_DIR}"
                --output-on-failure
        DEPENDS
            ztermy_format_check
            ztermy_clang_tidy_check
            ztermy_v1_runtime_preflight
        COMMENT "Running the complete automated V1 preflight"
        USES_TERMINAL
        VERBATIM
    )

    unset(ZTERMY_V1_PREFLIGHT_DATA_ROOT)
    unset(ZTERMY_V1_PREFLIGHT_PREREQUISITES)
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
