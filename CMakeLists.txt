cmake_minimum_required(VERSION 3.28)

project(
    ztermy
    VERSION 0.1.0
    DESCRIPTION "A native Windows SSH terminal"
    LANGUAGES C CXX
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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2)

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
)

add_library(ztermy_ssh_persistence STATIC
    src/infrastructure/ssh/KnownHostsStore.cpp
    src/infrastructure/ssh/KnownHostsStore.h
    src/infrastructure/ssh/SshProfileStore.cpp
    src/infrastructure/ssh/SshProfileStore.h
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
        src/ui/qml/AppSlider.qml
        src/ui/qml/AppSpinBox.qml
        src/ui/qml/AppSwitch.qml
        src/ui/qml/AppTextField.qml
        src/ui/qml/CaptionButton.qml
        src/ui/qml/ConfirmationDialog.qml
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
        src/ui/qml/Theme.qml
)

qt_generate_foreign_qml_types(ztermy_terminal_ui ztermy)

target_compile_features(ztermy PRIVATE cxx_std_23)
target_include_directories(ztermy PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
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
    ztermy_core_config
    ztermy_ssh_persistence
    ztermy_ssh_session
    ztermy_terminal_session
    ztermy_terminal_transport
    ztermy_terminal_ui
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
    VERBATIM
)

add_custom_target(ztermy_ui_layout_runtime_smoke
    COMMAND "$<TARGET_FILE:ztermy>"
            --ui-layout-smoke
            --data-dir "${CMAKE_CURRENT_BINARY_DIR}/test-data/ui-layout-smoke"
    DEPENDS ztermy
    COMMENT "Checking compact and regular QML layout breakpoints in the real window"
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
    target_link_libraries(ztermy_window_hit_test_tests PRIVATE Qt6::Test user32)

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

    add_test(NAME ssh-terminal-session COMMAND ztermy_ssh_terminal_session_tests)

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
        ztermy_core_config
        ztermy_ssh_persistence
        ztermy_ssh_session
        ztermy_terminal_session
        ztermy_terminal_ui
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

    include(CPack)

    add_custom_target(ztermy_installer
        COMMAND "${CMAKE_CPACK_COMMAND}"
                --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
                -G WIX
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        DEPENDS ztermy
        COMMENT "Creating the per-user ztermy MSI installer"
        VERBATIM
    )
endif()
