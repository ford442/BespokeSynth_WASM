# CMakeLists.txt for BespokeSynth WASM build with WebGPU and SDL2 audio
# This file configures the Emscripten build for WebAssembly target

cmake_minimum_required(VERSION 3.16)

project(BespokeSynthWASM VERSION 1.0.0 LANGUAGES C CXX)

# Ensure we're building with Emscripten
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This CMakeLists.txt is intended for Emscripten builds only. Use emcmake cmake to configure.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
cmake_minimum_required(VERSION 3.10)
project(BespokeSynthWASM)

# --- CRITICAL FIX START ---

# 1. Force Emscripten to make SDL2 and WebGPU headers available
add_compile_options(
    "-sUSE_SDL=2" 
    "--use-port=emdawnwebgpu"     # NEW: Replacement for -sUSE_WEBGPU=1
)

# 2. Linker options
add_link_options(
    "-sUSE_SDL=2" 
    "--use-port=emdawnwebgpu"     # NEW: Replacement for -sUSE_WEBGPU=1
    "-sALLOW_MEMORY_GROWTH=1"
    "-sASSERTIONS=1"
)

# JUCE Configuration (Fixes "No global header" error)
add_compile_definitions(
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_PTHREAD=0
    DONT_SET_USING_JUCE_NAMESPACE=1
)

# Include Paths
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../Source")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/jsoncpp/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/JUCE/modules")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/JUCE/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/nanovg/src") # NanoVG
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs")             # For 'leathers'
include_directories("${BESPOKE_LIBS_DIR}/leathers")                      # ensure nested leathers headers are visible
include_directories("${BESPOKE_LIBS_DIR}/nanovg")                        # allow includes like "nanovg/nanovg.h"
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/nanovg_fix")
# WASM-specific options
option(BESPOKE_WASM_WEBGPU "Enable WebGPU rendering backend" ON)
option(BESPOKE_WASM_SDL2_AUDIO "Enable SDL2 audio backend" ON)
option(BESPOKE_WASM_THREADS "Enable threading support" OFF)

message(STATUS "Building BespokeSynth for WebAssembly")
message(STATUS "  WebGPU: ${BESPOKE_WASM_WEBGPU}")
message(STATUS "  SDL2 Audio: ${BESPOKE_WASM_SDL2_AUDIO}")
message(STATUS "  Threads: ${BESPOKE_WASM_THREADS}")

# Define source directories
set(BESPOKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Source")
set(BESPOKE_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libs")
set(BESPOKE_WASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Collect WASM-specific sources
set(WASM_SOURCES
    ${BESPOKE_WASM_DIR}/src/WasmMain.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPUContext.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPURenderer.cpp
    ${BESPOKE_WASM_DIR}/src/SDL2AudioBackend.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridge.cpp
    ${BESPOKE_WASM_DIR}/src/Knob.cpp

    # --- JUCE module wrappers (implementations) ---
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_core/juce_core.cpp
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_audio_basics/juce_audio_basics.cpp
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_events/juce_events.cpp
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_graphics/juce_graphics.cpp
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_data_structures/juce_data_structures.cpp
)

# Collect core synth sources (subset for WASM - audio processing core)
# Note: Full source integration would require significant porting work
set(CORE_SOURCES
    # Core audio processing
    ${BESPOKE_SOURCE_DIR}/SynthGlobals.cpp
    ${BESPOKE_SOURCE_DIR}/OpenFrameworksPort.cpp
    ${BESPOKE_SOURCE_DIR}/ChannelBuffer.cpp
    ${BESPOKE_SOURCE_DIR}/RollingBuffer.cpp
    ${BESPOKE_SOURCE_DIR}/ADSR.cpp
    ${BESPOKE_SOURCE_DIR}/BiquadFilter.cpp
    ${BESPOKE_SOURCE_DIR}/Oscillator.cpp
    ${BESPOKE_SOURCE_DIR}/LFO.cpp
    ${BESPOKE_SOURCE_DIR}/Scale.cpp
    ${BESPOKE_SOURCE_DIR}/Transport.cpp
    ${BESPOKE_SOURCE_DIR}/MathUtils.cpp
    ${BESPOKE_SOURCE_DIR}/Ramp.cpp
    ${BESPOKE_SOURCE_DIR}/PerlinNoise.cpp
    ${BESPOKE_SOURCE_DIR}/PeakTracker.cpp
    ${BESPOKE_SOURCE_DIR}/JumpBlender.cpp
    ${BESPOKE_SOURCE_DIR}/Curve.cpp
    ${BESPOKE_SOURCE_DIR}/FileStream.cpp
    ${BESPOKE_SOURCE_DIR}/ModulationChain.cpp
)

# Create the main WASM executable
add_executable(BespokeSynthWASM
    ${WASM_SOURCES}
    ${CORE_SOURCES}
)

target_include_directories(BespokeSynthWASM PRIVATE
    ${BESPOKE_SOURCE_DIR}
    ${BESPOKE_WASM_DIR}/src
    ${BESPOKE_WASM_DIR}/include
    ${BESPOKE_LIBS_DIR}
    ${BESPOKE_LIBS_DIR}/nanovg/src
    ${BESPOKE_LIBS_DIR}/nanovg
    ${BESPOKE_LIBS_DIR}/leathers
    ${BESPOKE_LIBS_DIR}/json/jsoncpp/include
)

# WASM compile definitions
target_compile_definitions(BespokeSynthWASM PRIVATE
    BESPOKE_WASM=1
    $<$<BOOL:${BESPOKE_WASM_WEBGPU}>:BESPOKE_WEBGPU=1>
    $<$<BOOL:${BESPOKE_WASM_SDL2_AUDIO}>:BESPOKE_SDL2_AUDIO=1>
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/jsoncpp/include")

# Emscripten linker flags
set(EMSCRIPTEN_LINK_FLAGS
    "-sWASM=1"
    "-sUSE_SDL=2"
    "--use-port=emdawnwebgpu"     # NEW: Replacement for -sUSE_WEBGPU=1
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "-sEXPORTED_FUNCTIONS=['_main','_bespoke_init','_bespoke_process_audio','_bespoke_render']"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=134217728"  # 128MB initial
    "-sSTACK_SIZE=1048576"  # 1MB stack
    "--shell-file=${BESPOKE_WASM_DIR}/shell.html"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME='createBespokeSynth'"
)

# Add WebGPU support
if(BESPOKE_WASM_WEBGPU)
    list(APPEND EMSCRIPTEN_LINK_FLAGS
        "--use-port=emdawnwebgpu"
    )
endif()

# Add SDL2 audio support
if(BESPOKE_WASM_SDL2_AUDIO)
    list(APPEND EMSCRIPTEN_LINK_FLAGS
        "-sUSE_SDL=2"
    )
endif()

# Add threading support if enabled
if(BESPOKE_WASM_THREADS)
    list(APPEND EMSCRIPTEN_LINK_FLAGS
        "-pthread"
        "-sPTHREAD_POOL_SIZE=4"
    )
    target_compile_options(BespokeSynthWASM PRIVATE -pthread)
endif()

# Convert list to string for linking
string(REPLACE ";" " " EMSCRIPTEN_LINK_FLAGS_STR "${EMSCRIPTEN_LINK_FLAGS}")

set_target_properties(BespokeSynthWASM PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS_STR}"
)

# Copy resources
add_custom_command(TARGET BespokeSynthWASM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../resource
        ${CMAKE_BINARY_DIR}/resource
    COMMENT "Copying resources to build directory"
)

# Create a simple test target
add_executable(BespokeSynthWASM_test
    ${BESPOKE_WASM_DIR}/tests/test_main.cpp
)

target_include_directories(BespokeSynthWASM_test PRIVATE
    ${BESPOKE_WASM_DIR}/include
    ${BESPOKE_WASM_DIR}/src
)

set_target_properties(BespokeSynthWASM_test PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "-sWASM=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)
