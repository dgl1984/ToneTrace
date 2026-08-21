# Tone Trace 1.0 ships CLAP as its supported plug-in format.
# Additional wrappers can be added later without changing the root builder.

set(TONETRACE_WRAPPER_DIRECTORIES clap)
if(APPLE)
  list(APPEND TONETRACE_WRAPPER_DIRECTORIES au)
endif()

set(TONETRACE_WRAPPER_COUNT 0)
foreach(WRAPPER_DIRECTORY IN LISTS TONETRACE_WRAPPER_DIRECTORIES)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${WRAPPER_DIRECTORY}/CMakeLists.txt")
    add_subdirectory(${WRAPPER_DIRECTORY})
    math(EXPR TONETRACE_WRAPPER_COUNT "${TONETRACE_WRAPPER_COUNT} + 1")
  endif()
endforeach()

if(TONETRACE_WRAPPER_COUNT EQUAL 0)
  message(STATUS "Tone Trace plug-in wrappers have not been added yet")
else()
  message(STATUS "Configured ${TONETRACE_WRAPPER_COUNT} Tone Trace wrapper project(s)")
endif()
