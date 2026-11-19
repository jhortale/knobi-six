# SPDX-License-Identifier: GPL-3.0-only
# KNOBI-SIX as an AU / VST3 instrument (docs/superpowers/specs/2026-11-19-plugin-design.md, plan Task 3): JUCE 8
# (its AGPLv3 open-source option, pinned below; LICENSING.md) around the firmware's own synth
# (firmware/src/core/knobi_core.c, built here as a static library with the firmware's sources). macOS (Apple Silicon), CMake + Ninja or Xcode; plugin/README.md.
#   cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/plugin
#   ctest --test-dir build/plugin --output-on-failure
# Nothing is installed by a build (no COPY_PLUGIN_AFTER_BUILD): plugin/README.md says how.
cmake_minimum_required(VERSION 3.22)
project(KNOBI_SIX_PLUGIN VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS minimum")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Apple Silicon")
endif()
# Only the plugin's entry points are exported from the bundles: no firmware symbol, no kc_* (KC_API= below)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# The OB-6 factory bank (local only, docs/FACTORY_BANK.md): OFF by default and in CI. ON compiles the local
# firmware/gen/factory_bank.h (never committed) into the core and names everything KNOBI-SIX-local, its own plugin code
# and bundle id, so the two builds never stand in for one another.
option(KNOBI_FACTORY_LOCAL "Compile the local OB-6 factory bank (firmware/gen/factory_bank.h); local builds only" OFF)

get_filename_component(KNOBI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(KNOBI_FW "${KNOBI_ROOT}/firmware")

if(KNOBI_FACTORY_LOCAL)
    if(NOT EXISTS "${KNOBI_FW}/gen/factory_bank.h")
        message(FATAL_ERROR "KNOBI_FACTORY_LOCAL=ON needs firmware/gen/factory_bank.h (tools/ob6_import.py; docs/FACTORY_BANK.md)")
    endif()
    set(KNOBI_PRODUCT "KNOBI-SIX-local")
    set(KNOBI_PLUGIN_CODE "Kn6l")
    set(KNOBI_BUNDLE_ID "com.jhortale.knobisix.local")
    set(KNOBI_FACTORY_DEFS KNOBI_FACTORY_LOCAL=1 "KNOBI_FACTORY_H=\"${KNOBI_FW}/gen/factory_bank.h\"")
    message(STATUS "KNOBI-SIX: LOCAL build with the OB-6 factory bank -> KNOBI-SIX-local (never distribute)")
else()
    set(KNOBI_PRODUCT "KNOBI-SIX")
    set(KNOBI_PLUGIN_CODE "Kn6x")
    set(KNOBI_BUNDLE_ID "com.jhortale.knobisix")
    set(KNOBI_FACTORY_DEFS KNOBI_NO_FACTORY=1)
endif()

# ---- JUCE 8, pinned (tag 8.0.15, commit 91ad83ae34a81e0833b1a2b0866f54846370ae53), AGPLv3 (LICENSING.md)
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/91ad83ae34a81e0833b1a2b0866f54846370ae53.tar.gz
    URL_HASH SHA256=04f8d5055382582c757be9da069ea98338005f98248facd9c2804435ac853e70
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- the core: the firmware's behaviour code (firmware/src/core/knobi_core.h), one source of truth with the device
add_library(knobi_core STATIC "${KNOBI_FW}/src/core/knobi_core.c")
set_target_properties(knobi_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(knobi_core PUBLIC "${KNOBI_FW}/src" "${KNOBI_FW}/hal" "${KNOBI_FW}/gen")
# KC_API= : the core's calls are not exported from the bundle either (they are the plugin's own)
target_compile_definitions(knobi_core PUBLIC KC_API= PRIVATE ${KNOBI_FACTORY_DEFS})
# bit-exact with the firmware's host build (no fused multiply-add: the filter's float path), as tests/Makefile FP_EXACT
target_compile_options(knobi_core PRIVATE -O2 -ffp-contract=off -fvisibility=hidden -Wall -Wno-unused-function)
find_package(Threads REQUIRED)
target_link_libraries(knobi_core PUBLIC Threads::Threads)

# ---- the DSP around it, no JUCE (tested on its own): timeline, sample-accurate events, resampler, the threading seam
add_library(knobi_dsp STATIC src/CoreRunner.cpp src/Resampler.cpp)
target_include_directories(knobi_dsp PUBLIC src)
target_link_libraries(knobi_dsp PUBLIC knobi_core)
target_compile_options(knobi_dsp PRIVATE -O2 -Wall -Wextra)
set_target_properties(knobi_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- the plugin: AU (aumu Kn6x Jhrt) + VST3
juce_add_plugin(KnobiSix
    PRODUCT_NAME "${KNOBI_PRODUCT}"
    PLUGIN_NAME "${KNOBI_PRODUCT}"
    COMPANY_NAME "jhortale"
    DESCRIPTION "OB-6-inspired polysynth, the KNOBI-SIX firmware's own engine"
    BUNDLE_ID "${KNOBI_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE Jhrt
    PLUGIN_CODE "${KNOBI_PLUGIN_CODE}"
    FORMATS AU VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(KnobiSix PRIVATE src/Processor.cpp)
target_compile_definitions(KnobiSix PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    KNOBI_PLUGIN_LOCAL=$<BOOL:${KNOBI_FACTORY_LOCAL}>)
target_link_libraries(KnobiSix
    PRIVATE juce::juce_audio_utils
    PUBLIC knobi_dsp juce::juce_recommended_config_flags
    PRIVATE juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ad-hoc signed for local use (Apple Silicon loads only signed code; JUCE signs too, this makes it explicit)
if(APPLE)
    foreach(fmt AU VST3)
        add_custom_command(TARGET KnobiSix_${fmt} POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:KnobiSix_${fmt}>"
            VERBATIM)
    endforeach()
endif()

# ---- tests (CTest): the processor without a host, the resampler, the seam, real-time safety; plugin/tests/
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
