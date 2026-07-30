if(NOT DEFINED ZTERMY_DPI_SMOKE_EXECUTABLE
   OR ZTERMY_DPI_SMOKE_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "ZTERMY_DPI_SMOKE_EXECUTABLE is required")
endif()

if(NOT EXISTS "${ZTERMY_DPI_SMOKE_EXECUTABLE}")
    message(FATAL_ERROR
        "DPI smoke executable does not exist: ${ZTERMY_DPI_SMOKE_EXECUTABLE}"
    )
endif()

if(NOT DEFINED ZTERMY_DPI_SMOKE_DATA_ROOT
   OR ZTERMY_DPI_SMOKE_DATA_ROOT STREQUAL "")
    message(FATAL_ERROR "ZTERMY_DPI_SMOKE_DATA_ROOT is required")
endif()

set(dpi_scales 1.0 1.25 1.5 2.0)
set(dpi_labels 100 125 150 200)
list(LENGTH dpi_scales dpi_scale_count)
math(EXPR dpi_scale_last "${dpi_scale_count} - 1")

foreach(index RANGE 0 ${dpi_scale_last})
    list(GET dpi_scales ${index} dpi_scale)
    list(GET dpi_labels ${index} dpi_label)
    set(dpi_data_dir "${ZTERMY_DPI_SMOKE_DATA_ROOT}/${dpi_label}")

    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "QT_SCREEN_SCALE_FACTORS=${dpi_scale}"
            "QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough"
            "ZTERMY_TEST_EXPECTED_DPR=${dpi_scale}"
            "${ZTERMY_DPI_SMOKE_EXECUTABLE}"
            --window-dpi-smoke
            --data-dir "${dpi_data_dir}"
        RESULT_VARIABLE dpi_result
        TIMEOUT 30
    )
    if(NOT dpi_result EQUAL 0)
        message(FATAL_ERROR
            "Window DPI runtime smoke failed at ${dpi_label}% "
            "with exit code ${dpi_result}"
        )
    endif()
endforeach()

message(STATUS "Window DPI runtime smoke passed at 100%, 125%, 150%, and 200%")
