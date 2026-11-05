cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED TONETRACE_VERSION)
  # Standalone plug-in builds fall back to the repository VERSION file so the
  # version has a single source of truth. Standalone copies require an explicit version.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" TONETRACE_VERSION
      LIMIT_COUNT 1)
  else()
    message(FATAL_ERROR "Supply TONETRACE_VERSION when building outside the repository")
  endif()
endif()
project(ToneTraceEQ VERSION ${TONETRACE_VERSION} LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if (DEFINED JUCE_DIR AND NOT JUCE_DIR STREQUAL "")
  add_subdirectory("${JUCE_DIR}" JUCE)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE/CMakeLists.txt")
  add_subdirectory(deps/JUCE)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/JUCE/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../deps/JUCE" JUCE)
else()
  FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850)
  FetchContent_MakeAvailable(juce)
endif()

# The repository root is the single canonical copy of the frozen 1.0.5
# matching/realtime engine. SourceContractTests.ps1 pins these files by hash.
set(TONETRACE_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TONETRACE_RELEASE_INCLUDE "${TONETRACE_ENGINE_ROOT}/include")
set(TONETRACE_RELEASE_ENGINE
  "${TONETRACE_ENGINE_ROOT}/src/tonetrace_engine.cpp"
  "${TONETRACE_ENGINE_ROOT}/src/tonetrace_describe.cpp"
)
set(TONETRACE_RELEASE_REALTIME
  "${TONETRACE_ENGINE_ROOT}/src/tonetrace_realtime.cpp"
)

set(TONETRACE_PLATFORM_SOURCES
  src/platform/ToneTraceStoppedTransportTone.cpp
  src/platform/ToneTraceAnnouncements.cpp
)

set(TONETRACE_DESCRIPTION_SOURCES
  src/ToneTracePreMatchCapture.cpp
  src/controls/ToneTraceCurveDescriptionField.cpp
)

add_executable(ToneTraceProductCatalogTests tests/ProductCatalogTests.cpp)
target_include_directories(ToneTraceProductCatalogTests PRIVATE src)

add_executable(ToneTraceCaptureModelTests
  tests/CaptureModelTests.cpp
  src/capture/ToneTraceCaptureModel.cpp
)
target_include_directories(ToneTraceCaptureModelTests PRIVATE src)

# Exact released 1.0.5 engine/realtime regression suites. These source files
# are copied byte-for-byte from commit 24462d9d80d9df23048f528bd81e8d8573397724.
add_executable(ToneTraceReleasedEngineTests
  tests/ReleasedEngineTests.cpp
  ${TONETRACE_RELEASE_ENGINE}
)
target_include_directories(ToneTraceReleasedEngineTests PRIVATE "${TONETRACE_RELEASE_INCLUDE}")

add_executable(ToneTraceReleasedRealtimeTests
  tests/ReleasedRealtimeTests.cpp
  ${TONETRACE_RELEASE_ENGINE}
  ${TONETRACE_RELEASE_REALTIME}
)
target_include_directories(ToneTraceReleasedRealtimeTests PRIVATE "${TONETRACE_RELEASE_INCLUDE}")

add_executable(ToneTraceCorrectionControllerTests
  tests/CorrectionControllerTests.cpp
  src/correction/ToneTraceCorrectionController.cpp
  ${TONETRACE_RELEASE_ENGINE}
  ${TONETRACE_RELEASE_REALTIME}
)
target_include_directories(ToneTraceCorrectionControllerTests PRIVATE
  src
  "${TONETRACE_RELEASE_INCLUDE}"
)

add_executable(ToneTraceNotificationToneTests
  tests/NotificationToneTests.cpp
  src/tone/ToneTraceNotificationTone.cpp
)
target_include_directories(ToneTraceNotificationToneTests PRIVATE src)

add_executable(ToneTraceOutputBypassTests
  tests/OutputBypassTests.cpp
  src/tone/ToneTraceOutputBypass.cpp
)
target_include_directories(ToneTraceOutputBypassTests PRIVATE src)

add_executable(ToneTraceBandPageLayoutTests
  tests/BandPageLayoutTests.cpp
  src/ui/ToneTraceBandPageLayout.cpp
)
target_include_directories(ToneTraceBandPageLayoutTests PRIVATE src)


juce_add_console_app(ToneTraceKeyboardControlTests
  PRODUCT_NAME "Tone Trace Keyboard Control Tests"
  NEEDS_CURL FALSE
  NEEDS_WEB_BROWSER FALSE
)
target_sources(ToneTraceKeyboardControlTests PRIVATE
  tests/KeyboardControlTests.cpp
  src/controls/ToneTraceAccessibleComboBox.cpp
  src/controls/ToneTracePageSelector.cpp
  src/controls/ToneTraceParameterSlider.cpp
  src/controls/ToneTraceBandSlider.cpp
)
target_include_directories(ToneTraceKeyboardControlTests PRIVATE src)
target_compile_definitions(ToneTraceKeyboardControlTests PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)
target_link_libraries(ToneTraceKeyboardControlTests PRIVATE
  juce::juce_gui_basics
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
)
juce_generate_juce_header(ToneTraceKeyboardControlTests)


# Validation-only processor state round-trip executable. It compiles the same
# processor/editor/state implementation as the VST3, but does not create a plug-in.
juce_add_console_app(ToneTraceProcessorStateTests
  PRODUCT_NAME "Tone Trace Processor State Tests"
  NEEDS_CURL FALSE
  NEEDS_WEB_BROWSER FALSE
)

target_sources(ToneTraceProcessorStateTests PRIVATE
  tests/ProcessorStateTests.cpp
  tests/AnnouncementTests.cpp
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  ${TONETRACE_DESCRIPTION_SOURCES}
  src/capture/ToneTraceCaptureModel.cpp
  src/correction/ToneTraceCorrectionController.cpp
  src/tone/ToneTraceNotificationTone.cpp
  src/tone/ToneTraceOutputBypass.cpp
  ${TONETRACE_PLATFORM_SOURCES}
  ${TONETRACE_RELEASE_ENGINE}
  ${TONETRACE_RELEASE_REALTIME}
  src/controls/ToneTraceParameterSlider.cpp
  src/controls/ToneTraceAccessibleComboBox.cpp
  src/controls/ToneTracePageSelector.cpp
  src/controls/ToneTraceBandSlider.cpp
  src/controls/ToneTraceChoiceControl.cpp
  src/controls/ToneTraceToggleControl.cpp
  src/controls/ToneTraceCommandButton.cpp
  src/controls/ToneTraceReadOnlyField.cpp
  src/ui/ToneTraceGraphSurface.cpp
  src/ui/ToneTraceBandPageLayout.cpp
)

target_include_directories(ToneTraceProcessorStateTests PRIVATE
  src
  "${TONETRACE_RELEASE_INCLUDE}"
)

target_compile_definitions(ToneTraceProcessorStateTests PRIVATE
  TONETRACE_VISUAL_SNAPSHOT_BUILD=1
  TONETRACE_DISABLE_STOPPED_TRANSPORT_BEEP=1
  # The state suite pumps the real editor message loop so asynchronous curve
  # reports are covered end-to-end. Production builds keep JUCE defaults.
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(ToneTraceProcessorStateTests PRIVATE
  juce::juce_audio_utils
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
)

juce_generate_juce_header(ToneTraceProcessorStateTests)

# On macOS, generate Audio Unit in addition to VST3 and CLAP.
set(TONETRACE_APPLE_FORMATS)
if(APPLE)
  set(TONETRACE_APPLE_FORMATS AU)
endif()

# Both formats use the same processor, editor, and state implementation.
juce_add_plugin(ToneTraceEQ
  COMPANY_NAME "Lanes Audio"
  BUNDLE_ID "com.lanesaudio.tonetrace-eq"
  PLUGIN_MANUFACTURER_CODE Lane
  PLUGIN_CODE TTRE
  FORMATS VST3 ${TONETRACE_APPLE_FORMATS}
  PRODUCT_NAME "Tone Trace EQ"
  DESCRIPTION "Accessible match EQ"
  VST3_CATEGORIES Fx
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

target_sources(ToneTraceEQ PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  ${TONETRACE_DESCRIPTION_SOURCES}
  src/capture/ToneTraceCaptureModel.cpp
  src/correction/ToneTraceCorrectionController.cpp
  src/tone/ToneTraceNotificationTone.cpp
  src/tone/ToneTraceOutputBypass.cpp
  ${TONETRACE_PLATFORM_SOURCES}
  ${TONETRACE_RELEASE_ENGINE}
  ${TONETRACE_RELEASE_REALTIME}
  src/controls/ToneTraceParameterSlider.cpp
  src/controls/ToneTraceAccessibleComboBox.cpp
  src/controls/ToneTracePageSelector.cpp
  src/controls/ToneTraceBandSlider.cpp
  src/controls/ToneTraceChoiceControl.cpp
  src/controls/ToneTraceToggleControl.cpp
  src/controls/ToneTraceCommandButton.cpp
  src/controls/ToneTraceReadOnlyField.cpp
  src/ui/ToneTraceGraphSurface.cpp
  src/ui/ToneTraceBandPageLayout.cpp
)

target_include_directories(ToneTraceEQ PRIVATE
  src
  "${TONETRACE_RELEASE_INCLUDE}"
)

target_compile_definitions(ToneTraceEQ PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(ToneTraceEQ PRIVATE
  juce::juce_audio_utils
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)

juce_generate_juce_header(ToneTraceEQ)

if(NOT DEFINED CLAP_JUCE_EXTENSIONS_DIR OR CLAP_JUCE_EXTENSIONS_DIR STREQUAL "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/clap-juce-extensions/CMakeLists.txt")
    set(CLAP_JUCE_EXTENSIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/clap-juce-extensions")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/clap-juce-extensions/CMakeLists.txt")
    set(CLAP_JUCE_EXTENSIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/clap-juce-extensions")
  else()
    FetchContent_Declare(clap_juce
      GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
      GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e)
    FetchContent_MakeAvailable(clap_juce)
    set(CLAP_JUCE_EXTENSIONS_DIR "${clap_juce_SOURCE_DIR}")
  endif()
endif()
if(NOT TARGET clap_juce_sources)
  set(CLAP_JUCE_EXTENSIONS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CLAP_JUCE_EXTENSIONS_DIR}" clap-juce-extensions EXCLUDE_FROM_ALL)
endif()
include(cmake/ToneTraceClapCompatibility.cmake)
clap_juce_extensions_plugin(TARGET ToneTraceEQ
  CLAP_ID "com.lanesaudio.tonetrace-eq"
  CLAP_FEATURES audio-effect equalizer stereo
  CLAP_USE_JUCE_PARAMETER_RANGES ALL
)

if(TONETRACE_BUILD_TESTS AND (WIN32 OR APPLE))
  add_executable(tonetrace_clap_tests "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_clap_binary.cpp")
  target_include_directories(tonetrace_clap_tests PRIVATE "${CLAP_JUCE_EXTENSIONS_DIR}/clap-libs/clap/include")
  target_compile_definitions(tonetrace_clap_tests PRIVATE
    TONETRACE_EXPECTED_VERSION=\"${TONETRACE_VERSION}\")
  target_link_libraries(tonetrace_clap_tests PRIVATE ${CMAKE_DL_LIBS})
  add_dependencies(tonetrace_clap_tests ToneTraceEQ_CLAP)
  if(APPLE)
    set(TONETRACE_CLAP_TEST_PATH "$<TARGET_BUNDLE_DIR:ToneTraceEQ_CLAP>")
  else()
    set(TONETRACE_CLAP_TEST_PATH "$<TARGET_FILE:ToneTraceEQ_CLAP>")
  endif()
  add_test(NAME tonetrace_clap_tests COMMAND tonetrace_clap_tests "${TONETRACE_CLAP_TEST_PATH}")
endif()

install(TARGETS ToneTraceEQ_CLAP
  LIBRARY DESTINATION plugins/clap COMPONENT Release
  RUNTIME DESTINATION plugins/clap COMPONENT Release)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ToneTraceEQ_artefacts/$<CONFIG>/VST3/Tone Trace EQ.vst3"
  DESTINATION plugins/vst3 COMPONENT Release)
if(APPLE)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ToneTraceEQ_artefacts/$<CONFIG>/AU/Tone Trace EQ.component"
    DESTINATION plugins/au COMPONENT Release)
endif()
install(FILES "${CLAP_JUCE_EXTENSIONS_DIR}/LICENSE.md"
  DESTINATION licenses/clap-juce-extensions COMPONENT Release)
get_target_property(juceCoreDir juce::juce_core INTERFACE_JUCE_MODULE_PATH)
install(FILES "${juceCoreDir}/../LICENSE.md" DESTINATION licenses/JUCE COMPONENT Release)
foreach(notice IN ITEMS
    juce_core/zip/zlib/LICENSE
    juce_graphics/fonts/harfbuzz/COPYING
    juce_graphics/unicode/sheenbidi/LICENSE
    juce_graphics/image_formats/pnglib/LICENSE
    juce_graphics/image_formats/jpglib/README
    "juce_audio_formats/codecs/flac/Flac Licence.txt"
    "juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt"
    juce_audio_formats/codecs/oggvorbis/libvorbis-1.3.7/COPYING
    juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt)
  get_filename_component(noticeDir "${notice}" DIRECTORY)
  install(FILES "${juceCoreDir}/${notice}" DESTINATION "licenses/JUCE/${noticeDir}" COMPONENT Release)
endforeach()
install(FILES "${CLAP_JUCE_EXTENSIONS_DIR}/clap-libs/clap/LICENSE"
  DESTINATION licenses/clap-adapter-sdk COMPONENT Release)
install(FILES "${CLAP_JUCE_EXTENSIONS_DIR}/clap-libs/clap-helpers/LICENSE"
  DESTINATION licenses/clap-helpers COMPONENT Release)

enable_testing()
foreach(testTarget IN ITEMS ToneTraceProductCatalogTests ToneTraceCaptureModelTests
    ToneTraceReleasedEngineTests ToneTraceReleasedRealtimeTests
    ToneTraceCorrectionControllerTests ToneTraceNotificationToneTests
    ToneTraceOutputBypassTests ToneTraceBandPageLayoutTests
    ToneTraceKeyboardControlTests ToneTraceProcessorStateTests)
  add_test(NAME ${testTarget} COMMAND ${testTarget})
endforeach()

# Visual regression executable. It compiles the same processor/editor sources as
# the plug-in and renders the real editor offscreen into PNG files at representative
# sizes. It is kept as reusable validation infrastructure and is not part of the
# end-user plug-in package.
juce_add_console_app(ToneTraceVisualSnapshots
  PRODUCT_NAME "Tone Trace Visual Snapshots"
)

target_sources(ToneTraceVisualSnapshots PRIVATE
  tools/VisualSnapshotMain.cpp
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  ${TONETRACE_DESCRIPTION_SOURCES}
  src/capture/ToneTraceCaptureModel.cpp
  src/correction/ToneTraceCorrectionController.cpp
  src/tone/ToneTraceNotificationTone.cpp
  src/tone/ToneTraceOutputBypass.cpp
  ${TONETRACE_PLATFORM_SOURCES}
  ${TONETRACE_RELEASE_ENGINE}
  ${TONETRACE_RELEASE_REALTIME}
  src/controls/ToneTraceAccessibleComboBox.cpp
  src/controls/ToneTracePageSelector.cpp
  src/controls/ToneTraceParameterSlider.cpp
  src/controls/ToneTraceBandSlider.cpp
  src/controls/ToneTraceChoiceControl.cpp
  src/controls/ToneTraceToggleControl.cpp
  src/controls/ToneTraceCommandButton.cpp
  src/controls/ToneTraceReadOnlyField.cpp
  src/ui/ToneTraceGraphSurface.cpp
  src/ui/ToneTraceBandPageLayout.cpp
)

target_include_directories(ToneTraceVisualSnapshots PRIVATE
  src
  "${TONETRACE_RELEASE_INCLUDE}"
)

target_compile_definitions(ToneTraceVisualSnapshots PRIVATE
  TONETRACE_VISUAL_SNAPSHOT_BUILD=1
  TONETRACE_DISABLE_STOPPED_TRANSPORT_BEEP=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(ToneTraceVisualSnapshots PRIVATE
  juce::juce_audio_utils
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)

juce_generate_juce_header(ToneTraceVisualSnapshots)

if(WIN32)
  foreach(target ToneTraceEQ ToneTraceProcessorStateTests ToneTraceVisualSnapshots)
    target_link_libraries(${target} PRIVATE uiautomationcore oleaut32)
  endforeach()
endif()

