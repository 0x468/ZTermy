if(NOT DEFINED ZTERMY_DEPLOYED_EXECUTABLE)
    message(FATAL_ERROR "ZTERMY_DEPLOYED_EXECUTABLE is required")
endif()

if(NOT DEFINED ZTERMY_DEPLOYED_DATA_DIR)
    message(FATAL_ERROR "ZTERMY_DEPLOYED_DATA_DIR is required")
endif()

if(NOT EXISTS "${ZTERMY_DEPLOYED_EXECUTABLE}")
    message(FATAL_ERROR
        "The deployed executable does not exist: ${ZTERMY_DEPLOYED_EXECUTABLE}"
    )
endif()

get_filename_component(deployed_bin_dir "${ZTERMY_DEPLOYED_EXECUTABLE}" DIRECTORY)
file(GLOB deployed_crypto_dlls "${deployed_bin_dir}/libcrypto*.dll")
if(NOT deployed_crypto_dlls)
    message(FATAL_ERROR
        "The dynamic deployment does not contain its OpenSSL runtime dependency"
    )
endif()

file(REMOVE_RECURSE "${ZTERMY_DEPLOYED_DATA_DIR}")

# The Windows loader searches the executable directory before PATH. Restricting
# PATH to Windows directories proves the smoke run is not borrowing Qt DLLs or
# plugins from the developer Qt installation or the build tree.
set(ENV{PATH} "$ENV{SystemRoot}\\System32;$ENV{SystemRoot}")

execute_process(
    COMMAND
        "${ZTERMY_DEPLOYED_EXECUTABLE}"
        --smoke-test
        --data-dir "${ZTERMY_DEPLOYED_DATA_DIR}"
    RESULT_VARIABLE smoke_result
    OUTPUT_VARIABLE smoke_stdout
    ERROR_VARIABLE smoke_stderr
    TIMEOUT 30
)

if(NOT smoke_result EQUAL 0)
    message(FATAL_ERROR
        "The isolated dynamic deployment smoke test failed (${smoke_result}).\n"
        "stdout:\n${smoke_stdout}\n"
        "stderr:\n${smoke_stderr}"
    )
endif()

message(STATUS "The isolated dynamic deployment smoke test passed")
