set(ZTERMY_BUILD_COMMIT "unknown")
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE build_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE git_result
    )
    if(git_result EQUAL 0 AND build_commit MATCHES "^[0-9a-f]+$")
        set(ZTERMY_BUILD_COMMIT "${build_commit}")
    endif()
    # Follow HEAD and its ref, including linked worktrees and packed refs.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE build_ref OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    foreach(git_path IN ITEMS HEAD packed-refs "${build_ref}")
        if(NOT git_path STREQUAL "")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse --path-format=absolute --git-path "${git_path}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE identity_path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            )
            if(EXISTS "${identity_path}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${identity_path}")
            endif()
        endif()
    endforeach()
endif()
