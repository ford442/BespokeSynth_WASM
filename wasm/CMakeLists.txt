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
    ${BESPOKE_LIBS_DIR}/nanovg/src
    ${BESPOKE_LIBS_DIR}/json/jsoncpp/include
)

# WASM compile definitions
target_compile_definitions(BespokeSynthWASM PRIVATE
    BESPOKE_WASM=1
    $<$<BOOL:${BESPOKE_WASM_WEBGPU}>:BESPOKE_WEBGPU=1>
    $<$<BOOL:${BESPOKE_WASM_SDL2_AUDIO}>:BESPOKE_SDL2_AUDIO=1>
)

# Emscripten linker flags
set(EMSCRIPTEN_LINK_FLAGS
    "-sWASM=1"
    "-sUSE_SDL=2"
    "-sUSE_WEBGPU=1"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "-sEXPORTED_FUNCTIONS=['_main','_bespoke_init','_bespoke_process_audio','_bespoke_render']"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=134217728"  # 128MB initial
    "-sSTACK_SIZE=1048576"  # 1MB stack
    "--shell-file=${BESPOKE_WASM_DIR}/shell.html"
)

# Add WebGPU support
if(BESPOKE_WASM_WEBGPU)
    list(APPEND EMSCRIPTEN_LINK_FLAGS
        "-sUSE_WEBGPU=1"
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
