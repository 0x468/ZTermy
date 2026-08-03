foreach(required_variable
        ZTERMY_WINDOWS_ICON
        ZTERMY_BRANDING_PNG_DIRECTORY)
    if(NOT DEFINED ${required_variable}
       OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT EXISTS "${ZTERMY_WINDOWS_ICON}")
    message(FATAL_ERROR "Windows icon does not exist: ${ZTERMY_WINDOWS_ICON}")
endif()

file(READ "${ZTERMY_WINDOWS_ICON}" ico_header LIMIT 6 HEX)
string(TOLOWER "${ico_header}" ico_header)
if(NOT ico_header STREQUAL "000001000900")
    message(FATAL_ERROR
        "Windows icon must contain the nine-layer ICO header, observed: ${ico_header}"
    )
endif()

set(icon_sizes 16 20 24 32 40 48 64 128 256)
set(icon_size_hex
    00000010
    00000014
    00000018
    00000020
    00000028
    00000030
    00000040
    00000080
    00000100
)

list(LENGTH icon_sizes icon_size_count)
math(EXPR icon_size_last_index "${icon_size_count} - 1")
foreach(index RANGE ${icon_size_last_index})
    list(GET icon_sizes ${index} icon_size)
    list(GET icon_size_hex ${index} expected_dimension)
    set(png_path "${ZTERMY_BRANDING_PNG_DIRECTORY}/ztermy-${icon_size}.png")
    if(NOT EXISTS "${png_path}")
        message(FATAL_ERROR "Generated PNG does not exist: ${png_path}")
    endif()
    file(SIZE "${png_path}" png_size)
    if(png_size LESS 100)
        message(FATAL_ERROR "Generated PNG is unexpectedly small: ${png_path}")
    endif()
    file(READ "${png_path}" png_header LIMIT 24 HEX)
    string(TOLOWER "${png_header}" png_header)
    string(SUBSTRING "${png_header}" 0 32 png_signature_and_ihdr)
    string(SUBSTRING "${png_header}" 32 8 png_width)
    string(SUBSTRING "${png_header}" 40 8 png_height)
    if(NOT png_signature_and_ihdr STREQUAL "89504e470d0a1a0a0000000d49484452"
       OR NOT png_width STREQUAL expected_dimension
       OR NOT png_height STREQUAL expected_dimension)
        message(FATAL_ERROR
            "Generated PNG header or dimensions are invalid for ${png_path}: ${png_header}"
        )
    endif()
endforeach()

message(STATUS "Verified ztermy ICO and nine PNG branding layers")
