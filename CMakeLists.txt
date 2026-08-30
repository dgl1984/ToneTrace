cmake_minimum_required(VERSION 3.25)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TONETRACE_VERSION
  LIMIT_COUNT 1)
if(NOT TONETRACE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain one semantic version such as 1.0.0")
endif()

project(ToneTrace
  VERSION ${TONETRACE_VERSION}
  DESCRIPTION "Independent Tone Trace matching engine and plug-in project"
  LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

option(TONETRACE_BUILD_TOOLS "Build Tone Trace command-line and evaluation tools" ON)
option(TONETRACE_BUILD_TESTS "Build Tone Trace automated tests" ON)
option(TONETRACE_BUILD_PLUGINS "Build every plug-in wrapper present under plugins" ON)
option(TONETRACE_STATIC_MSVC_RUNTIME
       "Use the static MSVC runtime so portable Windows builds need no redist" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC AND TONETRACE_STATIC_MSVC_RUNTIME)
  set(CMAKE_MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

function(tonetrace_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(tonetrace_engine STATIC
  src/tonetrace_engine.cpp
  src/tonetrace_realtime.cpp
  src/tonetrace_describe.cpp)
add_library(ToneTrace::Engine ALIAS tonetrace_engine)
target_include_directories(tonetrace_engine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(tonetrace_engine PUBLIC cxx_std_20)
set_target_properties(tonetrace_engine PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
tonetrace_enable_warnings(tonetrace_engine)

if(TONETRACE_BUILD_TOOLS)
  add_executable(tonetrace-match src/main.cpp)
  target_link_libraries(tonetrace-match PRIVATE ToneTrace::Engine)
  tonetrace_enable_warnings(tonetrace-match)

  add_executable(tonetrace-fixture-eval tools/fixture_eval.cpp)
  target_link_libraries(tonetrace-fixture-eval PRIVATE ToneTrace::Engine)
  tonetrace_enable_warnings(tonetrace-fixture-eval)

  add_executable(tonetrace-pair-eval tools/pair_eval.cpp)
  target_link_libraries(tonetrace-pair-eval PRIVATE ToneTrace::Engine)
  tonetrace_enable_warnings(tonetrace-pair-eval)

  add_executable(tonetrace-stability-eval tools/stability_eval.cpp)
  target_link_libraries(tonetrace-stability-eval PRIVATE ToneTrace::Engine)
  tonetrace_enable_warnings(tonetrace-stability-eval)

  add_executable(tonetrace-realtime-bench tools/realtime_bench.cpp)
  target_link_libraries(tonetrace-realtime-bench PRIVATE ToneTrace::Engine)
  tonetrace_enable_warnings(tonetrace-realtime-bench)

  install(TARGETS
      tonetrace-match
      tonetrace-fixture-eval
      tonetrace-pair-eval
      tonetrace-stability-eval
      tonetrace-realtime-bench
    RUNTIME DESTINATION tools COMPONENT Development)
endif()

if(TONETRACE_BUILD_TESTS)
  add_executable(tonetrace-tests tests/test_engine.cpp)
  target_link_libraries(tonetrace-tests PRIVATE ToneTrace::Engine)
  tonetrace_enable_warnings(tonetrace-tests)
  install(TARGETS tonetrace-tests
    RUNTIME DESTINATION diagnostics COMPONENT Development)
  if(BUILD_TESTING)
    add_test(NAME tonetrace-engine COMMAND tonetrace-tests)
  endif()

  add_executable(tonetrace-realtime-tests tests/test_realtime.cpp)
  target_link_libraries(tonetrace-realtime-tests PRIVATE ToneTrace::Engine)
  tonetrace_enable_warnings(tonetrace-realtime-tests)
  install(TARGETS tonetrace-realtime-tests
    RUNTIME DESTINATION diagnostics COMPONENT Development)
  if(BUILD_TESTING)
    add_test(NAME tonetrace-realtime COMMAND tonetrace-realtime-tests)
  endif()

  add_executable(tonetrace-ui-layout-tests tests/test_ui_layout.cpp)
  target_include_directories(tonetrace-ui-layout-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_features(tonetrace-ui-layout-tests PRIVATE cxx_std_20)
  tonetrace_enable_warnings(tonetrace-ui-layout-tests)
  install(TARGETS tonetrace-ui-layout-tests
    RUNTIME DESTINATION diagnostics COMPONENT Development)
  if(BUILD_TESTING)
    add_test(NAME tonetrace-ui-layout COMMAND tonetrace-ui-layout-tests)
  endif()

  add_executable(tonetrace-band-value-tests tests/test_band_value.cpp)
  target_include_directories(tonetrace-band-value-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/clap")
  target_compile_features(tonetrace-band-value-tests PRIVATE cxx_std_20)
  tonetrace_enable_warnings(tonetrace-band-value-tests)
  install(TARGETS tonetrace-band-value-tests
    RUNTIME DESTINATION diagnostics COMPONENT Development)
  if(BUILD_TESTING)
    add_test(NAME tonetrace-band-value COMMAND tonetrace-band-value-tests)
  endif()
endif()

if(TONETRACE_BUILD_PLUGINS)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/CMakeLists.txt")
    add_subdirectory(plugins)
  else()
    message(STATUS "No plug-in wrapper tree is present yet; building engine and tools")
  endif()
endif()

install(FILES
    README.md
    MANUAL.md
    DESIGN.md
    GUI_DESIGN.md
    CHANGELOG.md
    RELEASE_NOTES.md
    RELEASING.md
    CONTRIBUTING.md
    SECURITY.md
  DESTINATION docs
  COMPONENT Release)

install(FILES
    LICENSE
  DESTINATION .
  COMPONENT Release)

install(FILES
    third_party/clap/LICENSE
  DESTINATION licenses/clap
  COMPONENT Release)
