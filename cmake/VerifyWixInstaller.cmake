foreach(required_variable
        ZTERMY_WIX_EXECUTABLE
        ZTERMY_INSTALLER
        ZTERMY_INSTALLER_FLAVOR
        ZTERMY_INSTALLER_INSPECTION_ROOT)
    if(NOT DEFINED ${required_variable}
       OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT ZTERMY_INSTALLER_FLAVOR MATCHES "^(static|dynamic)$")
    message(FATAL_ERROR
        "ZTERMY_INSTALLER_FLAVOR must be static or dynamic, got: "
        "${ZTERMY_INSTALLER_FLAVOR}"
    )
endif()

if(NOT EXISTS "${ZTERMY_WIX_EXECUTABLE}")
    message(FATAL_ERROR
        "WiX executable does not exist: ${ZTERMY_WIX_EXECUTABLE}"
    )
endif()
if(NOT EXISTS "${ZTERMY_INSTALLER}")
    message(FATAL_ERROR
        "Installer does not exist: ${ZTERMY_INSTALLER}"
    )
endif()

cmake_path(GET ZTERMY_INSTALLER PARENT_PATH installer_directory)
cmake_path(
    ABSOLUTE_PATH installer_directory
    NORMALIZE
    OUTPUT_VARIABLE installer_directory
)
cmake_path(
    ABSOLUTE_PATH ZTERMY_INSTALLER_INSPECTION_ROOT
    NORMALIZE
    OUTPUT_VARIABLE inspection_root
)
cmake_path(
    IS_PREFIX installer_directory
    "${inspection_root}"
    NORMALIZE
    inspection_is_below_build
)
if(NOT inspection_is_below_build
   OR inspection_root STREQUAL installer_directory)
    message(FATAL_ERROR
        "Installer inspection root must be a child of ${installer_directory}: "
        "${inspection_root}"
    )
endif()
set(ZTERMY_INSTALLER_INSPECTION_ROOT "${inspection_root}")

if(ZTERMY_SKIP_ICE_VALIDATION)
    message(STATUS
        "Skipping WiX ICE validation by explicit request; structural MSI "
        "inspection remains enabled"
    )
else()
    find_program(ZTERMY_POWERSHELL_EXECUTABLE NAMES pwsh pwsh.exe REQUIRED)
    execute_process(
        COMMAND
            "${ZTERMY_POWERSHELL_EXECUTABLE}" -NoProfile -Command
            "& '${ZTERMY_WIX_EXECUTABLE}' msi validate '${ZTERMY_INSTALLER}'; exit $LASTEXITCODE"
        RESULT_VARIABLE validate_result
    )
    if(NOT validate_result EQUAL 0)
        message(FATAL_ERROR
            "WiX validation failed with exit code ${validate_result}"
        )
    endif()
endif()

file(REMOVE_RECURSE "${ZTERMY_INSTALLER_INSPECTION_ROOT}")
file(MAKE_DIRECTORY "${ZTERMY_INSTALLER_INSPECTION_ROOT}")
set(decompiled_source
    "${ZTERMY_INSTALLER_INSPECTION_ROOT}/installer.wxs"
)
set(extracted_payload
    "${ZTERMY_INSTALLER_INSPECTION_ROOT}/payload"
)
execute_process(
    COMMAND
        "${ZTERMY_WIX_EXECUTABLE}" msi decompile
        -o "${decompiled_source}"
        -x "${extracted_payload}"
        "${ZTERMY_INSTALLER}"
    RESULT_VARIABLE decompile_result
    OUTPUT_VARIABLE decompile_output
    ERROR_VARIABLE decompile_error
)
if(NOT decompile_result EQUAL 0)
    message(FATAL_ERROR
        "WiX decompile failed with exit code ${decompile_result}:\n"
        "${decompile_output}\n${decompile_error}"
    )
endif()
if(NOT EXISTS "${decompiled_source}")
    message(FATAL_ERROR "WiX did not produce ${decompiled_source}")
endif()

file(READ "${decompiled_source}" installer_source)
set(required_patterns
    "<Package[^>]*Scope=\"perUser\""
    "<StandardDirectory Id=\"LocalAppDataFolder\""
    "<Directory Id=\"INSTALL_ROOT\" Name=\"ztermy\""
    "<Shortcut[^>]*Directory=\"ProgramMenuFolder\"[^>]*Target=\"\\[#CM_FP_ztermy\\.exe\\]\""
    "<File[^>]*Id=\"CM_FP_ztermy\\.exe\"[^>]*Name=\"ztermy\\.exe\""
    "<RemoveFolder[^>]*On=\"uninstall\"[^>]*Directory=\"INSTALL_ROOT\""
    "<MajorUpgrade[^>]*AllowSameVersionUpgrades=\"yes\""
    "<Property[^>]*Id=\"ARPPRODUCTICON\"[^>]*Value=\"ProductIcon\\.ico\""
    "<Icon[^>]*Id=\"ProductIcon\\.ico\""
)
foreach(required_pattern IN LISTS required_patterns)
    if(NOT installer_source MATCHES "${required_pattern}")
        message(FATAL_ERROR
            "Decompiled MSI is missing required contract: ${required_pattern}"
        )
    endif()
endforeach()

string(REGEX MATCHALL "<File[ \t\r\n]" installer_files "${installer_source}")
list(LENGTH installer_files installer_file_count)

string(TOLOWER "${installer_source}" installer_source_lower)
foreach(forbidden_name
        "portable.flag"
        "\\.pdb"
        "ghostty")
    if(installer_source_lower MATCHES "${forbidden_name}")
        message(FATAL_ERROR
            "Decompiled MSI contains forbidden payload reference: ${forbidden_name}"
        )
    endif()
endforeach()

if(ZTERMY_INSTALLER_FLAVOR STREQUAL "static")
    if(NOT installer_file_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one static MSI File entry, observed "
            "${installer_file_count}"
        )
    endif()
    if(installer_source_lower MATCHES "\\.dll")
        message(FATAL_ERROR
            "Static MSI contains an unexpected DLL payload reference"
        )
    endif()
elseif(installer_file_count LESS 8)
    message(FATAL_ERROR
        "Dynamic MSI contains too few payload files: ${installer_file_count}"
    )
endif()

file(GLOB_RECURSE extracted_files
    LIST_DIRECTORIES FALSE
    "${extracted_payload}/*"
)
list(LENGTH extracted_files extracted_file_count)
if(ZTERMY_INSTALLER_FLAVOR STREQUAL "static"
   AND NOT extracted_file_count EQUAL 2)
    message(FATAL_ERROR
        "Expected one executable and one product icon, observed "
        "${extracted_file_count}: ${extracted_files}"
    )
endif()

if(ZTERMY_INSTALLER_FLAVOR STREQUAL "dynamic")
    foreach(required_payload_name
            "Qt6Core.dll"
            "Qt6Gui.dll"
            "Qt6Qml.dll"
            "Qt6Quick.dll"
            "libcrypto-3-x64.dll"
            "qwindows.dll")
        string(TOLOWER "${required_payload_name}" required_payload_name_lower)
        string(REPLACE "." "\\." required_payload_pattern
            "${required_payload_name_lower}"
        )
        if(NOT installer_source_lower MATCHES
           "name=\"${required_payload_pattern}\"")
            message(FATAL_ERROR
                "Dynamic MSI is missing ${required_payload_name}"
            )
        endif()
    endforeach()

    foreach(extracted_file IN LISTS extracted_files)
        cmake_path(GET extracted_file FILENAME extracted_name)
        string(TOLOWER "${extracted_name}" extracted_name_lower)
        if(extracted_name_lower MATCHES
           "^qt6(core|gui|network|qml|quick|svg)d\\.dll$")
            message(FATAL_ERROR
                "Dynamic MSI contains a Debug Qt library: ${extracted_file}"
            )
        endif()
    endforeach()
endif()

set(extracted_executables ${extracted_files})
list(FILTER extracted_executables
    INCLUDE REGEX "/File/CM_FP_ztermy\\.exe$"
)
list(LENGTH extracted_executables extracted_executable_count)
if(NOT extracted_executable_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one extracted ztermy executable, observed: "
        "${extracted_executables}"
    )
endif()
list(GET extracted_executables 0 extracted_executable)
file(SIZE "${extracted_executable}" extracted_executable_size)
if(extracted_executable_size LESS 1)
    message(FATAL_ERROR "Extracted ztermy executable is empty")
endif()

set(extracted_icons ${extracted_files})
list(FILTER extracted_icons
    INCLUDE REGEX "/Icon/ProductIcon\\.ico$"
)
list(LENGTH extracted_icons extracted_icon_count)
if(NOT extracted_icon_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one extracted ztermy product icon, observed: "
        "${extracted_icons}"
    )
endif()
list(GET extracted_icons 0 extracted_icon)
file(SIZE "${extracted_icon}" extracted_icon_size)
if(extracted_icon_size LESS 1)
    message(FATAL_ERROR "Extracted ztermy product icon is empty")
endif()

message(STATUS
    "${ZTERMY_INSTALLER_FLAVOR} installer contract passed: per-user "
    "LocalAppData package, one ztermy.exe, "
    "product icon, Start-menu shortcut, same-version upgrade, and uninstall "
    "folder removal"
)
