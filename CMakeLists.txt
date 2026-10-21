cmake_minimum_required(VERSION 3.22)
project(SkellyEQ VERSION 1.4.0 LANGUAGES C CXX)
include(FetchContent)
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
  add_subdirectory("${JUCE_PATH}" JUCE)
else()
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
juce_add_binary_data(SkellyAssets SOURCES Assets/frame.png Assets/skull.png Assets/graph_bg.png Assets/knob_cap.png)
juce_add_plugin(SkellyEQ COMPANY_NAME "Skelly Audio" BUNDLE_ID "audio.skelly.eq" PLUGIN_MANUFACTURER_CODE SkAu PLUGIN_CODE SeQ1
  FORMATS VST3 Standalone PRODUCT_NAME "Skelly EQ" IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(SkellyEQ)
file(GLOB_RECURSE SKELLY_SOURCES CONFIGURE_DEPENDS "Source/*.cpp" "Source/*.h")
target_sources(SkellyEQ PRIVATE ${SKELLY_SOURCES})
target_compile_features(SkellyEQ PRIVATE cxx_std_17)
target_compile_definitions(SkellyEQ PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)
target_compile_definitions(SkellyEQ PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SkellyEQ PRIVATE SkellyAssets juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra)

# V1.4 defaults to a conservative functional editor. The old artwork editor
# is still available but should only be re-enabled after FL crash isolation.
option(SKELLY_SAFE_EDITOR "Use safe controls editor (recommended for FL Studio)" ON)
option(SKELLY_DIAGNOSTIC_EDITOR "Use a completely minimal diagnostic window" OFF)
option(SKELLY_TRACE_LIFECYCLE "Log plugin load, state and editor lifecycle checkpoints" ON)
target_compile_definitions(SkellyEQ PRIVATE
  SKELLY_SAFE_EDITOR=$<BOOL:${SKELLY_SAFE_EDITOR}>
  SKELLY_DIAGNOSTIC_EDITOR=$<BOOL:${SKELLY_DIAGNOSTIC_EDITOR}>
  SKELLY_TRACE_LIFECYCLE=$<BOOL:${SKELLY_TRACE_LIFECYCLE}>)
message(STATUS "Skelly EQ ${PROJECT_VERSION}: safe editor=${SKELLY_SAFE_EDITOR}, minimal editor=${SKELLY_DIAGNOSTIC_EDITOR}, trace=${SKELLY_TRACE_LIFECYCLE}")
