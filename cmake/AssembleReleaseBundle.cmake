foreach(required_variable
        ZTERMY_BUILD_ROOT
        ZTERMY_RELEASE_BUNDLE_ROOT
        ZTERMY_RELEASE_VERSION
        ZTERMY_RELEASE_FLAVOR
        ZTERMY_PORTABLE_ARCHIVE
        ZTERMY_INSTALLER)
    if(NOT DEFINED ${required_variable}
       OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT ZTERMY_RELEASE_FLAVOR MATCHES "^(static|dynamic)$")
    message(FATAL_ERROR
        "ZTERMY_RELEASE_FLAVOR must be static or dynamic, got: "
        "${ZTERMY_RELEASE_FLAVOR}"
    )
endif()

foreach(required_artifact
        ZTERMY_PORTABLE_ARCHIVE
        ZTERMY_INSTALLER)
    if(NOT EXISTS "${${required_artifact}}")
        message(FATAL_ERROR
            "Release artifact does not exist: ${${required_artifact}}"
        )
    endif()
    file(SIZE "${${required_artifact}}" artifact_size)
    if(artifact_size LESS 1)
        message(FATAL_ERROR
            "Release artifact is empty: ${${required_artifact}}"
        )
    endif()
endforeach()

cmake_path(
    ABSOLUTE_PATH ZTERMY_BUILD_ROOT
    NORMALIZE
    OUTPUT_VARIABLE build_root
)
cmake_path(
    ABSOLUTE_PATH ZTERMY_RELEASE_BUNDLE_ROOT
    NORMALIZE
    OUTPUT_VARIABLE bundle_root
)
cmake_path(
    IS_PREFIX build_root
    "${bundle_root}"
    NORMALIZE
    bundle_is_below_build
)
if(NOT bundle_is_below_build
   OR "${bundle_root}" STREQUAL "${build_root}")
    message(FATAL_ERROR
        "Release bundle root must be a child of ${build_root}: ${bundle_root}"
    )
endif()

set(expected_stem
    "ztermy-${ZTERMY_RELEASE_VERSION}-windows-x64-${ZTERMY_RELEASE_FLAVOR}"
)
set(expected_portable_name "${expected_stem}-portable.zip")
set(expected_installer_name "${expected_stem}.msi")
cmake_path(GET ZTERMY_PORTABLE_ARCHIVE FILENAME portable_name)
cmake_path(GET ZTERMY_INSTALLER FILENAME installer_name)
if(NOT "${portable_name}" STREQUAL "${expected_portable_name}")
    message(FATAL_ERROR
        "Unexpected portable archive name: '${portable_name}', expected "
        "'${expected_portable_name}'"
    )
endif()
if(NOT "${installer_name}" STREQUAL "${expected_installer_name}")
    message(FATAL_ERROR
        "Unexpected installer name: '${installer_name}', expected "
        "'${expected_installer_name}'"
    )
endif()

file(REMOVE_RECURSE "${bundle_root}")
file(MAKE_DIRECTORY "${bundle_root}")
file(COPY_FILE
    "${ZTERMY_PORTABLE_ARCHIVE}"
    "${bundle_root}/${portable_name}"
    ONLY_IF_DIFFERENT
)
file(COPY_FILE
    "${ZTERMY_INSTALLER}"
    "${bundle_root}/${installer_name}"
    ONLY_IF_DIFFERENT
)

file(SHA256 "${bundle_root}/${portable_name}" portable_sha256)
file(SHA256 "${bundle_root}/${installer_name}" installer_sha256)
string(TOLOWER "${portable_sha256}" portable_sha256)
string(TOLOWER "${installer_sha256}" installer_sha256)

string(CONCAT checksum_manifest
    "${portable_sha256}  ${portable_name}\n"
    "${installer_sha256}  ${installer_name}\n"
)
file(WRITE
    "${bundle_root}/SHA256SUMS.txt"
    "${checksum_manifest}"
)

string(CONCAT release_manifest
    "{\n"
    "  \"schemaVersion\": 1,\n"
    "  \"product\": \"ztermy\",\n"
    "  \"version\": \"${ZTERMY_RELEASE_VERSION}\",\n"
    "  \"platform\": \"windows\",\n"
    "  \"architecture\": \"x64\",\n"
    "  \"flavor\": \"${ZTERMY_RELEASE_FLAVOR}\",\n"
    "  \"checksumAlgorithm\": \"SHA-256\",\n"
    "  \"artifacts\": [\n"
    "    {\n"
    "      \"kind\": \"portable\",\n"
    "      \"file\": \"${portable_name}\",\n"
    "      \"sha256\": \"${portable_sha256}\"\n"
    "    },\n"
    "    {\n"
    "      \"kind\": \"installer\",\n"
    "      \"file\": \"${installer_name}\",\n"
    "      \"sha256\": \"${installer_sha256}\"\n"
    "    }\n"
    "  ]\n"
    "}\n"
)
file(WRITE
    "${bundle_root}/release-manifest.json"
    "${release_manifest}"
)

file(READ "${bundle_root}/SHA256SUMS.txt" written_checksum_manifest)
if(NOT "${written_checksum_manifest}" STREQUAL "${checksum_manifest}")
    message(FATAL_ERROR "Written SHA256SUMS.txt does not match its source data")
endif()
file(READ "${bundle_root}/release-manifest.json" written_release_manifest)
if(NOT "${written_release_manifest}" STREQUAL "${release_manifest}")
    message(FATAL_ERROR
        "Written release-manifest.json does not match its source data"
    )
endif()
string(JSON manifest_schema_version
    GET "${written_release_manifest}" schemaVersion
)
string(JSON manifest_version
    GET "${written_release_manifest}" version
)
string(JSON manifest_artifact_count
    LENGTH "${written_release_manifest}" artifacts
)
if(NOT manifest_schema_version EQUAL 1
   OR NOT "${manifest_version}" STREQUAL "${ZTERMY_RELEASE_VERSION}"
   OR NOT manifest_artifact_count EQUAL 2)
    message(FATAL_ERROR
        "Release JSON manifest has an invalid schema, version, or artifact count"
    )
endif()

file(GLOB bundle_files
    LIST_DIRECTORIES FALSE
    "${bundle_root}/*"
)
list(LENGTH bundle_files bundle_file_count)
if(NOT bundle_file_count EQUAL 4)
    message(FATAL_ERROR
        "Release bundle must contain exactly four files, observed "
        "${bundle_file_count}: ${bundle_files}"
    )
endif()

file(SHA256 "${bundle_root}/${portable_name}" copied_portable_sha256)
file(SHA256 "${bundle_root}/${installer_name}" copied_installer_sha256)
string(TOLOWER "${copied_portable_sha256}" copied_portable_sha256)
string(TOLOWER "${copied_installer_sha256}" copied_installer_sha256)
if(NOT "${copied_portable_sha256}" STREQUAL "${portable_sha256}"
   OR NOT "${copied_installer_sha256}" STREQUAL "${installer_sha256}")
    message(FATAL_ERROR "Release bundle copy changed an artifact digest")
endif()

message(STATUS
    "Release bundle assembled at ${bundle_root} with portable and MSI SHA-256 "
    "manifests"
)
