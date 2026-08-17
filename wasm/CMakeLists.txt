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

# Define source directories FIRST (before include_directories that use them)
set(BESPOKE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Source")
set(BESPOKE_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libs")
set(BESPOKE_WASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# --- CRITICAL FIX START ---

# 1. Force Emscripten to make SDL2 available
# SDL2 compile option is unconditional because SDL2AudioBackend.h is included
# by WasmBridge.cpp which is always compiled. The linker flag for SDL2 audio
# backend is added conditionally below based on BESPOKE_WASM_SDL2_AUDIO option.
add_compile_options(
    "-sUSE_SDL=2"
    "-Wno-deprecated-literal-operator"  # Suppress JUCE warnings
    "-Wno-#pragma-messages"              # Suppress Projucer warnings
)

# 2. Linker options
add_link_options(
    "-sALLOW_MEMORY_GROWTH=1"
    "-sUSE_WEBGL2=1"
    "-sMIN_WEBGL_VERSION=2"
    "-sMAX_WEBGL_VERSION=2"
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
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/JUCE/modules")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/JUCE/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/nanovg/src") # NanoVG
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs")             # For 'leathers'
include_directories("${BESPOKE_LIBS_DIR}/leathers")                      # ensure nested leathers headers are visible
include_directories("${BESPOKE_LIBS_DIR}/nanovg")                        # allow includes like "nanovg/nanovg.h"
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libs/nanovg_fix")

# Note: WebGPU headers are provided by Emscripten's emdawnwebgpu port
# They are automatically included when using --use-port=emdawnwebgpu
# Include path: <webgpu/webgpu.h>

# WASM-specific options
option(BESPOKE_WASM_WEBGPU "Enable WebGPU rendering backend" ON)
option(BESPOKE_WASM_SDL2_AUDIO "Enable SDL2 audio backend" ON)
option(BESPOKE_WASM_THREADS "Enable threading support" OFF)
option(BESPOKE_WASM_ASYNCIFY "Enable Asyncify support (off unless an async C++ stack is required)" OFF)
set(BESPOKE_WASM_BUILD_FLAVOR "Release" CACHE STRING "Release, Debug, or Profile WASM build")
set_property(CACHE BESPOKE_WASM_BUILD_FLAVOR PROPERTY STRINGS Release Debug Profile)

if(NOT BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Release" AND
   NOT BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Debug" AND
   NOT BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Profile")
    message(FATAL_ERROR "BESPOKE_WASM_BUILD_FLAVOR must be Release, Debug, or Profile")
endif()

message(STATUS "Building BespokeSynth for WebAssembly")
message(STATUS "  WebGPU: ${BESPOKE_WASM_WEBGPU}")
message(STATUS "  SDL2 Audio: ${BESPOKE_WASM_SDL2_AUDIO}")
message(STATUS "  Threads: ${BESPOKE_WASM_THREADS}")
message(STATUS "  Asyncify: ${BESPOKE_WASM_ASYNCIFY}")
message(STATUS "  Build flavor: ${BESPOKE_WASM_BUILD_FLAVOR}")

# Each adapter is one header + one source + one line here.
set(BESPOKE_WASM_ADAPTER_SOURCES
    ${BESPOKE_WASM_DIR}/src/adapters/OscillatorModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/GainModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/OutputModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/FilterModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/LfoModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/AdsrModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/DelayModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/NoiseModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/StepSequencerModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/TransportModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/ScaleModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/ReverbModuleAdapter.cpp
)

# Collect WASM-specific sources
set(WASM_SOURCES
    ${BESPOKE_WASM_DIR}/src/WasmMain.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPUContext.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPURenderer.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2Context.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2Renderer.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2Shaders.cpp
    ${BESPOKE_WASM_DIR}/src/PixelFont.cpp
    ${BESPOKE_WASM_DIR}/src/SDL2AudioBackend.cpp
    ${BESPOKE_WASM_DIR}/src/AudioAnalysis.cpp
    ${BESPOKE_WASM_DIR}/src/AudioProcessPlan.cpp
    ${BESPOKE_WASM_DIR}/src/AudioGraphEngine.cpp
    ${BESPOKE_WASM_DIR}/src/WasmAudioEngine.cpp
    ${BESPOKE_WASM_DIR}/src/AudioHealth.cpp
    ${BESPOKE_WASM_DIR}/src/AudioRtGuard.cpp
    ${BESPOKE_WASM_DIR}/src/WasmInputRouter.cpp
    ${BESPOKE_WASM_DIR}/src/WasmRuntimeState.cpp
    ${BESPOKE_WASM_DIR}/src/WasmPatchState.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridge.cpp
    ${BESPOKE_WASM_DIR}/src/Knob.cpp
    ${BESPOKE_WASM_DIR}/src/Module.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvas.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvasRender.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvasInput.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvasHelpers.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleFactory.cpp
    ${BESPOKE_WASM_DIR}/src/WasmModuleAdapterRegistry.cpp
    ${BESPOKE_WASM_ADAPTER_SOURCES}
    ${BESPOKE_WASM_DIR}/src/modules/OscillatorModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/GainModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/OutputModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/FilterModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/LFOModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/AdsrModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/DelayModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/NoiseModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/StepSequencerModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/TransportModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/ScaleModule.cpp

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

if(BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Debug")
    target_compile_options(BespokeSynthWASM PRIVATE -O0 -g)
elseif(BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Profile")
    target_compile_options(BespokeSynthWASM PRIVATE -O2 -g)
    target_compile_definitions(BespokeSynthWASM PRIVATE BESPOKE_WASM_FRAME_INSTRUMENTATION=1)
else()
    target_compile_options(BespokeSynthWASM PRIVATE -O3)
endif()

# Emscripten linker flags
set(EMSCRIPTEN_LINK_FLAGS
    "-sWASM=1"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','allocateUTF8','stringToUTF8','lengthBytesUTF8','HEAPF32','HEAPU8','HEAP8','wasmMemory']"
    "-sEXPORTED_FUNCTIONS=['_main','_bespoke_init','_bespoke_shutdown','_bespoke_process_audio','_bespoke_render','_bespoke_resize','_bespoke_process_events','_bespoke_play','_bespoke_stop','_bespoke_set_tempo','_bespoke_get_tempo','_bespoke_set_sample_rate','_bespoke_set_buffer_size','_bespoke_get_sample_rate','_bespoke_get_buffer_size','_bespoke_get_cpu_load','_bespoke_mouse_move','_bespoke_mouse_down','_bespoke_mouse_up','_bespoke_mouse_wheel','_bespoke_key_down','_bespoke_key_up','_bespoke_create_module','_bespoke_delete_module','_bespoke_connect_modules','_bespoke_get_view_mode','_bespoke_set_view_mode','_bespoke_set_control_value','_bespoke_get_control_value','_bespoke_save_state','_bespoke_load_state','_bespoke_get_state_json','_bespoke_load_state_json','_bespoke_get_module_count','_bespoke_set_panel','_bespoke_get_panel','_bespoke_get_panel_count','_bespoke_get_panel_name','_bespoke_is_panel_loaded','_bespoke_is_panel_running','_bespoke_get_panel_frame_count','_bespoke_log_all_panels_status','_bespoke_get_init_state','_bespoke_get_init_error','_bespoke_is_fully_initialized','_bespoke_get_version','_bespoke_set_renderer_backend','_bespoke_get_renderer_backend','_bespoke_is_webgl2_supported','_bespoke_get_webgl2_error','_bespoke_set_webgl_debug_mode','_bespoke_get_webgl_debug_mode','_bespoke_capture_screenshot','_bespoke_get_screenshot_pixels','_bespoke_set_render_test_mode','_bespoke_get_render_test_mode','_bespoke_set_font_test_visible','_bespoke_get_font_test_visible','_bespoke_get_control_count','_bespoke_get_control_info','_bespoke_set_theme_color','_bespoke_reset_theme']"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sINITIAL_MEMORY=134217728"  # 128MB initial
    "-sSTACK_SIZE=1048576"  # 1MB stack
    "--shell-file=${BESPOKE_WASM_DIR}/shell.html"
    "--preload-file=${BESPOKE_WASM_DIR}/resource-pack@/resource"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME='createBespokeSynth'"
)

# WebGPU initialization is callback-driven and no source uses emscripten_sleep(),
# so Asyncify is opt-in rather than a production binary-size cost.
if(BESPOKE_WASM_ASYNCIFY)
    list(APPEND EMSCRIPTEN_LINK_FLAGS
        "-sASYNCIFY=1"
        "-sASYNCIFY_STACK_SIZE=2097152"
    )
endif()

if(BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Debug")
    list(APPEND EMSCRIPTEN_LINK_FLAGS "-O0" "-g" "-sASSERTIONS=2")
elseif(BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Profile")
    list(APPEND EMSCRIPTEN_LINK_FLAGS "-O2" "-g" "-sASSERTIONS=1")
else()
    list(APPEND EMSCRIPTEN_LINK_FLAGS "-O3" "-sASSERTIONS=0")
endif()

# Keep additional C APIs in the explicit JavaScript export surface.
# NOTE: string(REPLACE) runs on the joined list; chain carefully.
string(REPLACE "'_bespoke_process_audio'," "'_bespoke_process_audio','_bespoke_process_audio_block',"
    EMSCRIPTEN_LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS}")
string(REPLACE "'_bespoke_reset_theme'" "'_bespoke_reset_theme','_bespoke_midi_note_on','_bespoke_midi_note_off','_bespoke_midi_cc','_bespoke_load_layout','_bespoke_set_external_audio','_bespoke_get_external_audio','_bespoke_set_external_sample_rate','_bespoke_set_audio_backend_id','_bespoke_get_audio_health_json','_bespoke_reset_audio_health','_bespoke_set_audio_queue_stats','_malloc','_free'"
    EMSCRIPTEN_LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS}")

# Add WebGPU support
if(BESPOKE_WASM_WEBGPU)
    # Add compile flag to enable WebGPU headers from emdawnwebgpu port
    target_compile_options(BespokeSynthWASM PRIVATE
        "SHELL:--use-port=emdawnwebgpu"
    )
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

# Copy only the minimal WASM resource pack next to the binary (not full desktop resource/).
add_custom_command(TARGET BespokeSynthWASM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resource
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${BESPOKE_WASM_DIR}/resource-pack
        ${CMAKE_BINARY_DIR}/resource
    COMMENT "Copying minimal WASM resource pack to build directory"
)

# Create a simple test target
add_executable(BespokeSynthWASM_test
    ${BESPOKE_WASM_DIR}/tests/test_main.cpp
    ${BESPOKE_WASM_DIR}/tests/synth_globals_stub.cpp
    ${BESPOKE_WASM_DIR}/src/AudioProcessPlan.cpp
    ${BESPOKE_WASM_DIR}/src/AudioGraphEngine.cpp
    ${BESPOKE_WASM_DIR}/src/AudioHealth.cpp
    ${BESPOKE_WASM_DIR}/src/WasmModuleAdapterRegistry.cpp
    ${BESPOKE_WASM_ADAPTER_SOURCES}
    ${BESPOKE_WASM_DIR}/src/Module.cpp
    ${BESPOKE_WASM_DIR}/src/modules/FilterModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/OscillatorModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/GainModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/OutputModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/LFOModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/AdsrModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/DelayModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/NoiseModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/StepSequencerModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/TransportModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/ScaleModule.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvasHelpers.cpp
    ${BESPOKE_WASM_DIR}/src/WasmPatchState.cpp
    ${BESPOKE_WASM_DIR}/src/PixelFont.cpp
    ${BESPOKE_SOURCE_DIR}/BiquadFilter.cpp
    ${BESPOKE_SOURCE_DIR}/Oscillator.cpp
)

target_include_directories(BespokeSynthWASM_test PRIVATE
    ${BESPOKE_WASM_DIR}/include
    ${BESPOKE_WASM_DIR}/src
    ${BESPOKE_SOURCE_DIR}
)

target_compile_definitions(BespokeSynthWASM_test PRIVATE BESPOKE_WASM_RT_ASSERT=1)

target_sources(BespokeSynthWASM_test PRIVATE
    ${BESPOKE_WASM_DIR}/src/RtAllocGuard.cpp
    ${BESPOKE_WASM_DIR}/src/AudioRtGuard.cpp
)

set_target_properties(BespokeSynthWASM_test PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "-sWASM=1 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)
