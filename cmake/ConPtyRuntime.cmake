# Official MIT-licensed Microsoft redistributable, embedded for single-EXE use.
FetchContent_Declare(ztermy_conpty
    URL https://api.nuget.org/v3-flatcontainer/microsoft.windows.console.conpty/1.24.260710001/microsoft.windows.console.conpty.1.24.260710001.nupkg
    URL_HASH SHA256=175640566a3b59c4b132070ee96c2c77e5ab7edd2e92732a5eb3610bbf63d90e
    DOWNLOAD_NAME conpty.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ztermy_conpty)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "The pinned ConPTY runtime currently supports ztermy's x64 builds only")
endif()
set(conpty_dll "${ztermy_conpty_SOURCE_DIR}/runtimes/win-x64/native/conpty.dll")
set(conpty_host "${ztermy_conpty_SOURCE_DIR}/build/native/runtimes/x64/OpenConsole.exe")
file(SHA256 "${conpty_dll}" conpty_dll_hash)
file(SHA256 "${conpty_host}" conpty_host_hash)
if(NOT conpty_dll_hash STREQUAL "39fba2713e2495117b1591ae8c32a3b904bea7aa66069cf7815e2844c76d75d8"
   OR NOT conpty_host_hash STREQUAL "b7fd936c2668b87b9ecf7b3366dc6568afc1c6f981874cba3e955a1c35cf8160")
    message(FATAL_ERROR "Unexpected ConPTY binaries, including in a local source override")
endif()
set_source_files_properties("${conpty_dll}" PROPERTIES QT_RESOURCE_ALIAS "conpty.dll")
set_source_files_properties("${conpty_host}" PROPERTIES QT_RESOURCE_ALIAS "OpenConsole.exe")
set(conpty_notice "${CMAKE_CURRENT_SOURCE_DIR}/resources/third-party/ConPTY-NOTICE.txt")
set_source_files_properties("${conpty_notice}" PROPERTIES QT_RESOURCE_ALIAS "NOTICE.txt")
qt_add_resources(ztermy_terminal_transport ztermy_conpty_runtime
    PREFIX "/ztermy/conpty" FILES "${conpty_dll}" "${conpty_host}" "${conpty_notice}")
