foreach(required_variable
        ZTERMY_DYNAMIC_DEPLOY_ROOT
        ZTERMY_PORTABLE_PACKAGE_ROOT
        ZTERMY_PORTABLE_VERSION)
    if(NOT DEFINED ${required_variable}
       OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(package_name
    "ztermy-${ZTERMY_PORTABLE_VERSION}-windows-x64-dynamic"
)
set(staging_root "${ZTERMY_PORTABLE_PACKAGE_ROOT}/${package_name}")
set(archive_path
    "${ZTERMY_PORTABLE_PACKAGE_ROOT}/${package_name}-portable.zip"
)

if(NOT EXISTS "${ZTERMY_DYNAMIC_DEPLOY_ROOT}/ztermy.exe")
    message(FATAL_ERROR
        "Dynamic deployment is missing ztermy.exe: "
        "${ZTERMY_DYNAMIC_DEPLOY_ROOT}"
    )
endif()

file(REMOVE_RECURSE "${staging_root}")
file(REMOVE "${archive_path}")
file(MAKE_DIRECTORY "${staging_root}")
file(COPY "${ZTERMY_DYNAMIC_DEPLOY_ROOT}/"
    DESTINATION "${staging_root}"
    PATTERN "smoke-data" EXCLUDE
)
file(TOUCH "${staging_root}/portable.flag")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive_path}"
            --format=zip "${package_name}"
    WORKING_DIRECTORY "${ZTERMY_PORTABLE_PACKAGE_ROOT}"
    RESULT_VARIABLE archive_result
)
if(NOT archive_result EQUAL 0 OR NOT EXISTS "${archive_path}")
    message(FATAL_ERROR
        "Failed to create dynamic portable archive: ${archive_path}"
    )
endif()

message(STATUS "Created ${archive_path}")
