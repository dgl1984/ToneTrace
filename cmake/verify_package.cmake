# Verifies a staged Tone Trace release package before it is zipped or uploaded.
#
# Required variables:
#   STAGE_DIR       staged package root (contains plugins/, docs/, licenses/)
#   PROJECT_VERSION semantic version expected in the VST3 moduleinfo.json
#
# The check fails the build rather than publishing a partial or mislabelled
# package. It deliberately validates the manifest as a second opinion: a stage
# that omits (or adds) a file after the manifest was written must not ship.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED STAGE_DIR OR NOT IS_DIRECTORY "${STAGE_DIR}")
  message(FATAL_ERROR "STAGE_DIR must name the staged package directory")
endif()
if(NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR "PROJECT_VERSION must name the Tone Trace version")
endif()

function(require_file relative_path)
  if(NOT EXISTS "${STAGE_DIR}/${relative_path}")
    message(FATAL_ERROR "Release package is missing ${relative_path}")
  endif()
endfunction()

# --- Exactly one CLAP and one complete VST3 bundle -------------------------

file(GLOB CLAP_FILES LIST_DIRECTORIES false "${STAGE_DIR}/plugins/clap/*.clap")
list(LENGTH CLAP_FILES CLAP_COUNT)
if(NOT CLAP_COUNT EQUAL 1)
  message(FATAL_ERROR
    "Release package must contain exactly one CLAP binary; found ${CLAP_COUNT}")
endif()

file(GLOB VST3_BUNDLES LIST_DIRECTORIES true "${STAGE_DIR}/plugins/vst3/*.vst3")
list(LENGTH VST3_BUNDLES VST3_COUNT)
if(NOT VST3_COUNT EQUAL 1)
  message(FATAL_ERROR
    "Release package must contain exactly one VST3 bundle; found ${VST3_COUNT}")
endif()

list(GET VST3_BUNDLES 0 VST3_BUNDLE)
file(GLOB VST3_BINARIES LIST_DIRECTORIES false
  "${VST3_BUNDLE}/Contents/x86_64-win/*.vst3")
list(LENGTH VST3_BINARIES VST3_BINARY_COUNT)
if(NOT VST3_BINARY_COUNT EQUAL 1)
  message(FATAL_ERROR
    "VST3 bundle must contain exactly one x86_64 binary; found ${VST3_BINARY_COUNT}")
endif()

set(MODULEINFO "${VST3_BUNDLE}/Contents/Resources/moduleinfo.json")
if(NOT EXISTS "${MODULEINFO}")
  message(FATAL_ERROR "VST3 bundle is missing Contents/Resources/moduleinfo.json")
endif()
file(READ "${MODULEINFO}" MODULEINFO_TEXT)
if(NOT MODULEINFO_TEXT MATCHES "\"Version\"[ \t\r\n]*:[ \t\r\n]*\"${PROJECT_VERSION}\"")
  message(FATAL_ERROR
    "VST3 moduleinfo.json does not report version ${PROJECT_VERSION}")
endif()

# --- No developer-only artifacts -------------------------------------------

file(GLOB_RECURSE DEV_FILES LIST_DIRECTORIES false
  "${STAGE_DIR}/*.exe" "${STAGE_DIR}/*.dll" "${STAGE_DIR}/*.lib"
  "${STAGE_DIR}/*.pdb" "${STAGE_DIR}/*.obj" "${STAGE_DIR}/*.ilk"
  "${STAGE_DIR}/*.exp")
if(DEV_FILES)
  list(JOIN DEV_FILES ", " DEV_FILE_LIST)
  message(FATAL_ERROR
    "Release package contains developer artifacts: ${DEV_FILE_LIST}")
endif()

# --- Documentation ---------------------------------------------------------

set(REQUIRED_DOCS
  docs/README.md
  docs/MANUAL.md
  docs/CHANGELOG.md
  docs/RELEASE_NOTES.md
  docs/LICENSE
  docs/DESIGN.md
  docs/GUI_DESIGN.md
  docs/RELEASING.md
  docs/CONTRIBUTING.md
  docs/SECURITY.md
  docs/plugins/README.md
  docs/BUILD_MANIFEST.txt
  LICENSE)
foreach(REQUIRED_DOC IN LISTS REQUIRED_DOCS)
  require_file("${REQUIRED_DOC}")
endforeach()

# --- Manifest matches the staged tree --------------------------------------

set(MANIFEST "${STAGE_DIR}/docs/BUILD_MANIFEST.txt")
file(STRINGS "${MANIFEST}" MANIFEST_LINES)
set(IN_PACKAGED_SECTION FALSE)
set(MANIFEST_FILES)
foreach(LINE IN LISTS MANIFEST_LINES)
  if(LINE STREQUAL "Packaged files")
    set(IN_PACKAGED_SECTION TRUE)
    continue()
  endif()
  if(LINE STREQUAL "Authoritative source files")
    set(IN_PACKAGED_SECTION FALSE)
    continue()
  endif()
  if(NOT IN_PACKAGED_SECTION)
    continue()
  endif()
  if(NOT LINE MATCHES "^([0-9a-f]+)  ([0-9]+)  (.+)$")
    continue()
  endif()
  set(FILE_HASH "${CMAKE_MATCH_1}")
  set(FILE_SIZE "${CMAKE_MATCH_2}")
  set(FILE_RELATIVE "${CMAKE_MATCH_3}")
  set(FILE_PATH "${STAGE_DIR}/${FILE_RELATIVE}")
  if(NOT EXISTS "${FILE_PATH}")
    message(FATAL_ERROR "Manifest lists a missing packaged file: ${FILE_RELATIVE}")
  endif()
  file(SIZE "${FILE_PATH}" ACTUAL_SIZE)
  if(NOT ACTUAL_SIZE EQUAL FILE_SIZE)
    message(FATAL_ERROR
      "Packaged file size disagrees with the manifest: ${FILE_RELATIVE}")
  endif()
  file(SHA256 "${FILE_PATH}" ACTUAL_HASH)
  if(NOT ACTUAL_HASH STREQUAL FILE_HASH)
    message(FATAL_ERROR
      "Packaged file hash disagrees with the manifest: ${FILE_RELATIVE}")
  endif()
  list(APPEND MANIFEST_FILES "${FILE_RELATIVE}")
endforeach()

if(NOT MANIFEST_FILES)
  message(FATAL_ERROR "BUILD_MANIFEST.txt does not list any packaged files")
endif()

file(GLOB_RECURSE STAGED_FILES LIST_DIRECTORIES false "${STAGE_DIR}/*")
file(RELATIVE_PATH MANIFEST_RELATIVE "${STAGE_DIR}" "${MANIFEST}")
string(REPLACE "\\" "/" MANIFEST_RELATIVE "${MANIFEST_RELATIVE}")
foreach(STAGED_FILE IN LISTS STAGED_FILES)
  file(RELATIVE_PATH RELATIVE "${STAGE_DIR}" "${STAGED_FILE}")
  string(REPLACE "\\" "/" RELATIVE "${RELATIVE}")
  if(RELATIVE STREQUAL MANIFEST_RELATIVE)
    continue()
  endif()
  if(NOT RELATIVE IN_LIST MANIFEST_FILES)
    message(FATAL_ERROR
      "Packaged file is missing from BUILD_MANIFEST.txt: ${RELATIVE}")
  endif()
endforeach()

list(LENGTH MANIFEST_FILES MANIFEST_COUNT)
message(STATUS
  "Verified release package: 1 CLAP, 1 VST3, ${MANIFEST_COUNT} manifest entries")
