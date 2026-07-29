cmake_minimum_required(VERSION 3.28)

project(
    ztermy
    VERSION 0.1.0
    DESCRIPTION "A native Windows SSH terminal"
    LANGUAGES CXX
)

include(CTest)

option(ZTERMY_STATIC_BUILD "Build ztermy with the locally provided static Qt package" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

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

qt_add_executable(ztermy WIN32
    src/main.cpp
    src/core/logging/Logging.cpp
    src/core/logging/Logging.h
    src/platform/windows/NativeWindow.cpp
    src/platform/windows/NativeWindow.h
    src/platform/windows/WindowHitTest.cpp
    src/platform/windows/WindowHitTest.h
    resources/windows/ztermy.manifest
)

set_source_files_properties(
    src/ui/qml/CaptionButton.qml
    PROPERTIES QT_RESOURCE_ALIAS CaptionButton.qml
)
set_source_files_properties(
    src/ui/qml/Main.qml
    PROPERTIES QT_RESOURCE_ALIAS Main.qml
)

qt_add_qml_module(ztermy
    URI Ztermy
    VERSION 1.0
    QML_FILES
        src/ui/qml/CaptionButton.qml
        src/ui/qml/Main.qml
)

target_compile_features(ztermy PRIVATE cxx_std_23)
target_include_directories(ztermy PRIVATE src)

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
    ztermy_terminal_transport
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    dwmapi
)

if(MSVC)
    target_compile_options(ztermy_terminal_transport PRIVATE
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
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

    qt_add_executable(ztermy_window_hit_test_tests
        tests/window_hit_test_tests.cpp
        src/platform/windows/WindowHitTest.cpp
        src/platform/windows/WindowHitTest.h
    )

    target_compile_features(ztermy_window_hit_test_tests PRIVATE cxx_std_23)
    target_include_directories(ztermy_window_hit_test_tests PRIVATE src)
    target_link_libraries(ztermy_window_hit_test_tests PRIVATE Qt6::Test)

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
endif()
