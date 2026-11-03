if(NOT DEFINED ROOT OR NOT IS_DIRECTORY "${ROOT}")
  message(FATAL_ERROR "ROOT must name the staged package directory")
endif()
if(NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}")
  message(FATAL_ERROR "SOURCE_ROOT must name the authoritative source directory")
endif()
if(NOT DEFINED OUT)
  message(FATAL_ERROR "OUT must name the manifest file")
endif()
if(NOT DEFINED PLATFORM_NAME)
  set(PLATFORM_NAME "unknown")
endif()
if(NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR "PROJECT_VERSION must name the Tone Trace version")
endif()

string(TIMESTAMP CREATED_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
file(WRITE "${OUT}"
  "Tone Trace build manifest\n"
  "Created UTC: ${CREATED_UTC}\n"
  "Platform: ${PLATFORM_NAME}\n"
  "Project version: ${PROJECT_VERSION}\n"
  "Build type: Release\n\n"
  "Packaged files\n")

file(GLOB_RECURSE ARTIFACT_FILES LIST_DIRECTORIES false "${ROOT}/*")
file(RELATIVE_PATH MANIFEST_RELATIVE "${ROOT}" "${OUT}")
string(REPLACE "\\" "/" MANIFEST_RELATIVE "${MANIFEST_RELATIVE}")
list(SORT ARTIFACT_FILES)
foreach(FILE_PATH IN LISTS ARTIFACT_FILES)
  file(RELATIVE_PATH RELATIVE_PATH "${ROOT}" "${FILE_PATH}")
  string(REPLACE "\\" "/" RELATIVE_PATH "${RELATIVE_PATH}")
  if(RELATIVE_PATH STREQUAL MANIFEST_RELATIVE)
    continue()
  endif()
  file(SHA256 "${FILE_PATH}" FILE_HASH)
  file(SIZE "${FILE_PATH}" FILE_SIZE)
  file(APPEND "${OUT}" "${FILE_HASH}  ${FILE_SIZE}  ${RELATIVE_PATH}\n")
endforeach()

file(APPEND "${OUT}" "\nAuthoritative source files\n")
set(SOURCE_FILES
  "${SOURCE_ROOT}/CMakeLists.txt"
  "${SOURCE_ROOT}/CMakePresets.json"
  "${SOURCE_ROOT}/VERSION"
  "${SOURCE_ROOT}/README.md"
  "${SOURCE_ROOT}/MANUAL.md"
  "${SOURCE_ROOT}/DESIGN.md"
  "${SOURCE_ROOT}/GUI_DESIGN.md"
  "${SOURCE_ROOT}/CHANGELOG.md"
  "${SOURCE_ROOT}/RELEASE_NOTES.md"
  "${SOURCE_ROOT}/RELEASING.md"
  "${SOURCE_ROOT}/CONTRIBUTING.md"
  "${SOURCE_ROOT}/SECURITY.md"
  "${SOURCE_ROOT}/LICENSE"
  "${SOURCE_ROOT}/.gitignore"
  "${SOURCE_ROOT}/.gitattributes"
  "${SOURCE_ROOT}/build_all_windows.bat")
foreach(SOURCE_DIRECTORY IN ITEMS include src tests tools plugins cmake .github)
  if(IS_DIRECTORY "${SOURCE_ROOT}/${SOURCE_DIRECTORY}")
    file(GLOB_RECURSE DIRECTORY_FILES LIST_DIRECTORIES false
      "${SOURCE_ROOT}/${SOURCE_DIRECTORY}/*")
    # The authoritative list describes source, not local build state. Exclude
    # generated build trees, fetched dependencies, caches, and packaged output
    # so a manifest made beside a working build tree still matches a clean
    # source archive.
    list(FILTER DIRECTORY_FILES EXCLUDE REGEX
      "/(build[^/]*|_deps|deps|CMakeFiles|\\.git|\\.vs|\\.cache|dist|out|visual-snapshots|publish-[^/]*|release-[^/]*)(/|$)")
    list(FILTER DIRECTORY_FILES EXCLUDE REGEX
      "/(CMakeCache\\.txt|cmake_install\\.cmake|AUDIT_STATE\\.md|Thumbs\\.db|\\.DS_Store|\\$out)$")
    list(FILTER DIRECTORY_FILES EXCLUDE REGEX
      "\\.(exe|dll|lib|pdb|obj|ilk|exp|log|zip|sha256|tmp)$")
    list(APPEND SOURCE_FILES ${DIRECTORY_FILES})
  endif()
endforeach()
list(REMOVE_DUPLICATES SOURCE_FILES)
list(SORT SOURCE_FILES)
foreach(FILE_PATH IN LISTS SOURCE_FILES)
  if(EXISTS "${FILE_PATH}" AND NOT IS_DIRECTORY "${FILE_PATH}")
    file(RELATIVE_PATH RELATIVE_PATH "${SOURCE_ROOT}" "${FILE_PATH}")
    file(SHA256 "${FILE_PATH}" FILE_HASH)
    string(REPLACE "\\" "/" RELATIVE_PATH "${RELATIVE_PATH}")
    file(APPEND "${OUT}" "${FILE_HASH}  ${RELATIVE_PATH}\n")
  endif()
endforeach()

# Every explicitly listed source file is part of the published record. A
# silent skip would hide a missing licence, policy, or release document.
set(MISSING_SOURCE_FILES)
foreach(FILE_PATH IN LISTS SOURCE_FILES)
  if(NOT EXISTS "${FILE_PATH}")
    file(RELATIVE_PATH RELATIVE_PATH "${SOURCE_ROOT}" "${FILE_PATH}")
    list(APPEND MISSING_SOURCE_FILES "${RELATIVE_PATH}")
  endif()
endforeach()
if(MISSING_SOURCE_FILES)
  list(JOIN MISSING_SOURCE_FILES ", " MISSING_SOURCE_LIST)
  message(WARNING "Manifest skipped missing source files: ${MISSING_SOURCE_LIST}")
endif()
