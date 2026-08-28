foreach(required_variable
        ZTERMY_PORTABLE_ARCHIVE
        ZTERMY_PORTABLE_INSPECTION_ROOT
        ZTERMY_PORTABLE_VERSION)
    if(NOT DEFINED ${required_variable}
       OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT EXISTS "${ZTERMY_PORTABLE_ARCHIVE}")
    message(FATAL_ERROR
        "Portable archive does not exist: ${ZTERMY_PORTABLE_ARCHIVE}"
    )
endif()

file(REMOVE_RECURSE "${ZTERMY_PORTABLE_INSPECTION_ROOT}")
file(MAKE_DIRECTORY "${ZTERMY_PORTABLE_INSPECTION_ROOT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${ZTERMY_PORTABLE_ARCHIVE}"
    WORKING_DIRECTORY "${ZTERMY_PORTABLE_INSPECTION_ROOT}"
    RESULT_VARIABLE extract_result
)
if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR "Failed to extract the dynamic portable archive")
endif()

set(package_root
    "${ZTERMY_PORTABLE_INSPECTION_ROOT}/ztermy-${ZTERMY_PORTABLE_VERSION}-windows-x64-dynamic"
)
foreach(required_path
        "ztermy.exe"
        "portable.flag"
        "qt.conf"
        "Qt6Core.dll"
        "Qt6Gui.dll"
        "Qt6Qml.dll"
        "Qt6Quick.dll"
        "libcrypto-3-x64.dll"
        "plugins/platforms/qwindows.dll")
    if(NOT EXISTS "${package_root}/${required_path}")
        message(FATAL_ERROR
            "Dynamic portable archive is missing ${required_path}"
        )
    endif()
endforeach()

file(GLOB_RECURSE packaged_files
    LIST_DIRECTORIES FALSE
    "${package_root}/*"
)
foreach(packaged_file IN LISTS packaged_files)
    cmake_path(GET packaged_file FILENAME packaged_name)
    string(TOLOWER "${packaged_name}" packaged_name_lower)
    if(packaged_name_lower MATCHES "\\.pdb$"
       OR packaged_name_lower MATCHES
          "^qt6(core|gui|network|qml|quick|svg)d\\.dll$")
        message(FATAL_ERROR
            "Dynamic portable archive contains a development artifact: "
            "${packaged_file}"
        )
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DZTERMY_DEPLOYED_EXECUTABLE=${package_root}/ztermy.exe"
            "-DZTERMY_DEPLOYED_DATA_DIR=${ZTERMY_PORTABLE_INSPECTION_ROOT}/smoke-data"
            -P "${CMAKE_CURRENT_LIST_DIR}/RunDynamicDeploymentSmoke.cmake"
    RESULT_VARIABLE smoke_result
)
if(NOT smoke_result EQUAL 0)
    message(FATAL_ERROR "Dynamic portable runtime smoke test failed")
endif()

message(STATUS "Dynamic portable archive contract passed")
