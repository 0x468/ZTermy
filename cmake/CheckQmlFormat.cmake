cmake_minimum_required(VERSION 3.28)

foreach(required_variable IN ITEMS
        ZTERMY_QMLFORMAT_EXECUTABLE
        ZTERMY_QML_SOURCE_ROOT
        ZTERMY_QML_FORMAT_WORK_ROOT)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT EXISTS "${ZTERMY_QMLFORMAT_EXECUTABLE}")
    message(FATAL_ERROR
        "qmlformat executable does not exist: ${ZTERMY_QMLFORMAT_EXECUTABLE}"
    )
endif()
if(NOT IS_DIRECTORY "${ZTERMY_QML_SOURCE_ROOT}")
    message(FATAL_ERROR
        "QML source root does not exist: ${ZTERMY_QML_SOURCE_ROOT}"
    )
endif()

set(qml_source_root "${ZTERMY_QML_SOURCE_ROOT}")
set(qml_work_root "${ZTERMY_QML_FORMAT_WORK_ROOT}")
cmake_path(NORMAL_PATH qml_source_root)
cmake_path(NORMAL_PATH qml_work_root)
cmake_path(IS_PREFIX qml_source_root "${qml_work_root}" NORMALIZE work_root_is_in_source)
if(work_root_is_in_source)
    message(FATAL_ERROR
        "QML format work root must stay outside the source tree: ${qml_work_root}"
    )
endif()
file(MAKE_DIRECTORY "${ZTERMY_QML_FORMAT_WORK_ROOT}")

file(GLOB_RECURSE qml_files
    LIST_DIRECTORIES FALSE
    "${ZTERMY_QML_SOURCE_ROOT}/*.qml"
)
list(SORT qml_files)
if(NOT qml_files)
    message(FATAL_ERROR "No QML files found below ${ZTERMY_QML_SOURCE_ROOT}")
endif()

set(unformatted_files)
foreach(qml_file IN LISTS qml_files)
    file(RELATIVE_PATH relative_qml_file "${ZTERMY_QML_SOURCE_ROOT}" "${qml_file}")
    set(formatted_file "${ZTERMY_QML_FORMAT_WORK_ROOT}/${relative_qml_file}")
    get_filename_component(formatted_directory "${formatted_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${formatted_directory}")
    file(COPY_FILE "${qml_file}" "${formatted_file}")

    execute_process(
        COMMAND "${ZTERMY_QMLFORMAT_EXECUTABLE}"
                --newline unix
                --inplace
                "${formatted_file}"
        RESULT_VARIABLE format_result
        ERROR_VARIABLE format_error
    )
    if(NOT format_result EQUAL 0)
        message(FATAL_ERROR
            "qmlformat failed for ${qml_file}: ${format_error}"
        )
    endif()

    file(SHA256 "${qml_file}" original_hash)
    file(SHA256 "${formatted_file}" formatted_hash)
    if(NOT original_hash STREQUAL formatted_hash)
        list(APPEND unformatted_files "${qml_file}")
    endif()
endforeach()

if(unformatted_files)
    list(JOIN unformatted_files "\n  " unformatted_list)
    message(FATAL_ERROR
        "QML formatting differs from qmlformat 6.8 output:\n  "
        "${unformatted_list}"
    )
endif()

list(LENGTH qml_files qml_file_count)
message(STATUS "${qml_file_count} QML files match qmlformat")
