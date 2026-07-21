cmake_minimum_required(VERSION 3.22)
project(LoopBreaker VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if no build type is set (single-config generators only).
# Without this, the VST3 SDK's fdebug.h errors because none of DEVELOPMENT,
# RELEASE, _DEBUG, or NDEBUG is defined.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified — defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ─── JUCE ─────────────────────────────────────────────────────────────────────
# CI sets JUCE_DIR; local dev falls back to a sibling JUCE checkout.
if(NOT DEFINED JUCE_DIR)
    if(DEFINED ENV{JUCE_DIR})
        set(JUCE_DIR "$ENV{JUCE_DIR}")
    else()
        # Default: JUCE installed alongside this project's parent
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE")
    endif()
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# ─── CLAP extensions ──────────────────────────────────────────────────────────
add_subdirectory(Submodules/clap-juce-extensions EXCLUDE_FROM_ALL)

# ─── SoundTouch (header-only for us — sources are unity-included) ────────────
set(SOUNDTOUCH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/soundtouch/include")

# ─── Binary Resources ────────────────────────────────────────────────────────
juce_add_binary_data(LoopBreakerBinaryData
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        Resources/Fonts/Rajdhani-Bold.ttf
        Resources/Fonts/Rajdhani-Regular.ttf
        Resources/Fonts/JetBrainsMono-Regular.ttf
        Resources/Fonts/PressStart2P-Regular.ttf
)

# ─── Plugin Target ───────────────────────────────────────────────────────────
juce_add_plugin(LoopBreaker
    COMPANY_NAME             "Glow Machine Audio"
    PLUGIN_NAME              "Loop Breaker"
    PLUGIN_DESCRIPTION       "Loop Breaker"
    PLUGIN_MANUFACTURER_CODE GloM
    PLUGIN_CODE              TsPl
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    PRODUCES_MIDI_OUTPUT     FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS                  VST3 AU AUv3
    VST3_CATEGORIES          Instrument
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    HARDENED_RUNTIME_ENABLED TRUE
    BUNDLE_ID                com.glowmachineaudio.LoopBreaker
    PRODUCT_NAME             "Loop Breaker"
    COPY_PLUGIN_AFTER_BUILD  FALSE
)

juce_generate_juce_header(LoopBreaker)

target_sources(LoopBreaker PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AudioBuffer.cpp
    Source/AudioBufferManager.cpp
    Source/Modifier.cpp
    Source/ModifierScheduler.cpp
    Source/ProjectManager.cpp
    Source/ModifierSchedulerTests.cpp
    Source/ThemeEngine.cpp
)

target_include_directories(LoopBreaker PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${SOUNDTOUCH_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Submodules"
)

# Match Projucer's addUsingNamespaceToJuceHeader="0" —
# prevents juce::AudioBuffer from colliding with project's AudioBuffer class.
target_compile_definitions(LoopBreaker PUBLIC
    DONT_SET_USING_JUCE_NAMESPACE=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

# ─── CLAP target ──────────────────────────────────────────────────────────────
clap_juce_extensions_plugin(TARGET LoopBreaker
    CLAP_ID "com.glowmachineaudio.LoopBreaker"
    CLAP_FEATURES instrument synthesizer)

# ─── pluginval (plugin validation) ────────────────────────────────────────────
add_subdirectory("Submodules/pluginval")

# Custom validation target using VST3 (AU requires system-level installation on macOS)
set(PLUGINVAL_STRICTNESS 5 CACHE STRING "pluginval strictness level (1-10)")
get_target_property(vst3_artefact LoopBreaker_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
add_custom_target(validate
    COMMAND $<TARGET_FILE:pluginval>
        --validate "${vst3_artefact}"
        --strictness-level ${PLUGINVAL_STRICTNESS}
    DEPENDS pluginval LoopBreaker_VST3
    COMMENT "Running pluginval on VST3 (strictness ${PLUGINVAL_STRICTNESS})")

target_link_libraries(LoopBreaker
    PRIVATE
        LoopBreakerBinaryData
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
