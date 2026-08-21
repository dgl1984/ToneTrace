add_library(tonetrace-clap MODULE
  tonetrace_clap.cpp)

target_link_libraries(tonetrace-clap PRIVATE ToneTrace::Engine)
target_include_directories(tonetrace-clap PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/clap/include")
if(WIN32)
  target_sources(tonetrace-clap PRIVATE
    tonetrace_win32_editor.cpp
    resource.rc)
  target_link_libraries(tonetrace-clap PRIVATE user32 gdi32 comctl32)
endif()
target_compile_features(tonetrace-clap PRIVATE cxx_std_20)
target_compile_definitions(tonetrace-clap PRIVATE
  TONETRACE_VERSION="${PROJECT_VERSION}")
tonetrace_enable_warnings(tonetrace-clap)
set_target_properties(tonetrace-clap PROPERTIES
  PREFIX ""
  SUFFIX ".clap"
  OUTPUT_NAME "Tone Trace EQ")

install(TARGETS tonetrace-clap
  RUNTIME DESTINATION plugins/clap COMPONENT Release
  LIBRARY DESTINATION plugins/clap COMPONENT Release)

if(TONETRACE_BUILD_TESTS)
  add_executable(tonetrace-clap-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_clap.cpp")
  target_link_libraries(tonetrace-clap-tests PRIVATE
    ToneTrace::Engine
    ${CMAKE_DL_LIBS})
  target_include_directories(tonetrace-clap-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/clap/include")
  target_compile_features(tonetrace-clap-tests PRIVATE cxx_std_20)
  tonetrace_enable_warnings(tonetrace-clap-tests)
  if(BUILD_TESTING)
    add_test(NAME tonetrace-clap-host
      COMMAND tonetrace-clap-tests $<TARGET_FILE:tonetrace-clap>)
  endif()
  install(TARGETS tonetrace-clap-tests
    RUNTIME DESTINATION diagnostics COMPONENT Development)
endif()
