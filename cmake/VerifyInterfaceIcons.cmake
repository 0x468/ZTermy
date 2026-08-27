if(NOT DEFINED ZTERMY_ICON_DIRECTORY)
    message(FATAL_ERROR "ZTERMY_ICON_DIRECTORY is required")
endif()

file(GLOB icon_files LIST_DIRECTORIES FALSE "${ZTERMY_ICON_DIRECTORY}/*.svg")
list(LENGTH icon_files icon_count)
if(NOT icon_count EQUAL 59)
    message(FATAL_ERROR "Expected 59 production interface icons, found ${icon_count}")
endif()

foreach(required_icon IN ITEMS activity.svg ai.svg application.svg bookmark.svg chevron-left.svg close.svg cpu.svg disk.svg highlight.svg
                               memory.svg minus.svg network.svg new-file.svg pause.svg paste.svg pin.svg pin-tab.svg pin-window.svg security.svg select-visible.svg shortcuts.svg settings.svg
                               split-horizontal.svg split-vertical.svg warning.svg)
    if(NOT EXISTS "${ZTERMY_ICON_DIRECTORY}/${required_icon}")
        message(FATAL_ERROR "Missing required interface icon: ${required_icon}")
    endif()
endforeach()

foreach(icon_file IN LISTS icon_files)
    file(READ "${icon_file}" icon_source)
    if(NOT icon_source MATCHES "viewBox=\"0 0 20 20\"")
        message(FATAL_ERROR "${icon_file} does not use the 20x20 interface-icon grid")
    endif()
    if(NOT icon_source MATCHES "currentColor")
        message(FATAL_ERROR "${icon_file} does not use the currentColor paint token")
    endif()
    if(icon_source MATCHES "#[0-9A-Fa-f]{3,8}")
        message(FATAL_ERROR "${icon_file} contains a hard-coded color")
    endif()
endforeach()

message(STATUS "Verified ${icon_count} production interface SVG icons")
