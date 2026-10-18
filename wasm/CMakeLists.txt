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
# by WasmBridgeInit.cpp which is always compiled. The linker flag for SDL2 audio
# backend is added conditionally below based on BESPOKE_WASM_SDL2_AUDIO option.
add_compile_options(
    "-sUSE_SDL=2"
    "-Wno-deprecated-literal-operator"  # Suppress JUCE warnings
    "-Wno-#pragma-messages"              # Suppress Projucer warnings
)

# 2. Linker options
# USE_WEBGL2 is unconditional (not gated on BESPOKE_WASM_WEBGPU) because
# WebGL2Context/WebGL2Renderer are always compiled in as the runtime-selectable
# fallback backend (?renderer=webgl), not an alternate build configuration.
# NOTE: -sENVIRONMENT=web is intentionally NOT set here (directory-wide) — it
# would also apply to BespokeSynthWASM_test, which `npm run test:unit` runs
# under Node. It's added to BespokeSynthWASM's own EMSCRIPTEN_LINK_FLAGS below.
# ALLOW_MEMORY_GROWTH is set here only (both targets inherit it); the browser
# target caps it with MAXIMUM_MEMORY in EMSCRIPTEN_LINK_FLAGS below.
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
include_directories("${BESPOKE_LIBS_DIR}/freeverb/freeverb")          # revmodel.hpp (reverb adapter)

# Note: WebGPU headers are provided by Emscripten's emdawnwebgpu port
# They are automatically included when using --use-port=emdawnwebgpu
# Include path: <webgpu/webgpu.h>

# WASM-specific options
option(BESPOKE_WASM_WEBGPU "Enable WebGPU rendering backend" ON)
option(BESPOKE_WASM_SDL2_AUDIO "Enable SDL2 audio backend" ON)
option(BESPOKE_WASM_THREADS "Enable threading support" OFF)
option(BESPOKE_WASM_ASYNCIFY "Enable Asyncify support (off unless an async C++ stack is required)" OFF)
option(BESPOKE_WASM_LTO "Link-time optimization for the Release flavor (slow link; disable for quick CI iteration)" ON)
option(BESPOKE_WASM_SIMD "Compile with -msimd128 and enable wasm_simd128.h DSP kernels (scalar fallback when OFF)" ON)
option(BESPOKE_WASM_LEGACY_PANELS "Compile Mixer/Effects/Sequencer demo panels (runtime: ?legacyPanels=1)" ON)
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
message(STATUS "  LTO (Release only): ${BESPOKE_WASM_LTO}")
message(STATUS "  SIMD128: ${BESPOKE_WASM_SIMD}")
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
    ${BESPOKE_WASM_DIR}/src/adapters/SamplerModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/LooperModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/PulseToNoteModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/MixerModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/CompressorModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/ChorusModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/WaveshaperModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/ClockDividerModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/DrumSeqModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/adapters/ScriptModuleAdapter.cpp
    ${BESPOKE_WASM_DIR}/src/ScriptNoteQueue.cpp
    # Shared by adapters above: generic slider UI + public-domain Freeverb (reverb adapter).
    ${BESPOKE_WASM_DIR}/src/SliderPanelModule.cpp
    ${BESPOKE_LIBS_DIR}/freeverb/src/allpass.cpp
    ${BESPOKE_LIBS_DIR}/freeverb/src/comb.cpp
    ${BESPOKE_LIBS_DIR}/freeverb/src/revmodel.cpp
)

# Collect WASM-specific sources
set(WASM_SOURCES
    ${BESPOKE_WASM_DIR}/src/WasmMain.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPUContext.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPURenderer.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPURendererWidgets.cpp
    ${BESPOKE_WASM_DIR}/src/WebGPURendererPipelines.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2Context.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2Renderer.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2RendererPath.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2RendererWidgets.cpp
    ${BESPOKE_WASM_DIR}/src/WebGL2Shaders.cpp
    ${BESPOKE_WASM_DIR}/src/PixelFont.cpp
    ${BESPOKE_WASM_DIR}/src/SdfFont.cpp
    ${BESPOKE_WASM_DIR}/src/SDL2AudioBackend.cpp
    ${BESPOKE_WASM_DIR}/src/AudioAnalysis.cpp
    ${BESPOKE_WASM_DIR}/src/AudioProcessPlan.cpp
    ${BESPOKE_WASM_DIR}/src/AudioGraphEngine.cpp
    ${BESPOKE_WASM_DIR}/src/WasmAudioEngine.cpp
    ${BESPOKE_WASM_DIR}/src/AudioHealth.cpp
    ${BESPOKE_WASM_DIR}/src/AudioRtGuard.cpp
    ${BESPOKE_WASM_DIR}/src/SampleBuffer.cpp
    ${BESPOKE_WASM_DIR}/src/SampleStore.cpp
    ${BESPOKE_WASM_DIR}/src/InputAudioBus.cpp
    ${BESPOKE_WASM_DIR}/src/LooperArena.cpp
    ${BESPOKE_WASM_DIR}/src/OfflineRender.cpp
    ${BESPOKE_WASM_DIR}/src/WasmInputRouter.cpp
    ${BESPOKE_WASM_DIR}/src/WasmRuntimeState.cpp
    ${BESPOKE_WASM_DIR}/src/WasmPatchState.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridgeInit.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridgeRender.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridgeAudio.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridgePatch.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridgeInput.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridgeLive.cpp
    ${BESPOKE_WASM_DIR}/src/WasmBridgeLegacyPanels.cpp
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
    ${BESPOKE_WASM_DIR}/src/modules/SamplerModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/LooperModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/PulseToNoteModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/ScriptModule.cpp
    ${BESPOKE_WASM_DIR}/src/TuningTable.cpp

    # --- JUCE module wrappers ---
    # WASM audio/core only. juce_graphics / juce_events are not compiled: nothing
    # under wasm/src includes them. CORE (OpenFrameworksPort, Scale, SynthGlobals)
    # still parse GUI/OpenGL headers; LTO should GC unused symbols. Required:
    # juce_core, juce_audio_basics. juce_data_structures is kept while CORE still
    # pulls those headers transitively. juce_colour_stub.cpp satisfies juce::Colour
    # without compiling juce_graphics.cpp.
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_core/juce_core.cpp
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_audio_basics/juce_audio_basics.cpp
    ${BESPOKE_LIBS_DIR}/JUCE/modules/juce_data_structures/juce_data_structures.cpp
    ${BESPOKE_WASM_DIR}/src/juce_colour_stub.cpp
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

# WGSL shaders live in wasm/shaders/*.wgsl and are embedded as string constants
# at build time (WebGPURendererPipelines.cpp includes the generated header).
set(BESPOKE_WASM_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(BESPOKE_WASM_WGSL_HEADER "${BESPOKE_WASM_GENERATED_DIR}/BespokeWasm/generated/WgslSources.inc")
set(BESPOKE_WASM_WGSL_SHADERS
    "kRender2DWgsl=${BESPOKE_WASM_DIR}/shaders/render2d.wgsl"
    "kAnalyzerFftWgsl=${BESPOKE_WASM_DIR}/shaders/analyzer_fft.wgsl"
)
set(BESPOKE_WASM_WGSL_FILES
    ${BESPOKE_WASM_DIR}/shaders/render2d.wgsl
    ${BESPOKE_WASM_DIR}/shaders/analyzer_fft.wgsl
)
add_custom_command(
    OUTPUT ${BESPOKE_WASM_WGSL_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BESPOKE_WASM_GENERATED_DIR}/BespokeWasm/generated
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT=${BESPOKE_WASM_WGSL_HEADER}
        "-DSHADERS=${BESPOKE_WASM_WGSL_SHADERS}"
        -P ${BESPOKE_WASM_DIR}/../scripts/embed_shaders.cmake
    DEPENDS ${BESPOKE_WASM_WGSL_FILES} ${BESPOKE_WASM_DIR}/../scripts/embed_shaders.cmake
    COMMENT "Embedding WGSL shaders"
    VERBATIM
)

# Create the main WASM executable
add_executable(BespokeSynthWASM
    ${WASM_SOURCES}
    ${CORE_SOURCES}
    ${BESPOKE_WASM_WGSL_HEADER}
)

# Only paths not already covered by the global include_directories() calls
# above (wasm/include, ../Source, ../libs, ../libs/nanovg[/src], ../libs/leathers)
# belong here — duplicating them just repeats identical -I flags on every TU.
target_include_directories(BespokeSynthWASM PRIVATE
    ${BESPOKE_WASM_DIR}/src
    ${BESPOKE_WASM_GENERATED_DIR}
    ${BESPOKE_WASM_DIR}/third_party/dr_libs
    ${BESPOKE_LIBS_DIR}/json/jsoncpp/include
)

# WASM compile definitions
target_compile_definitions(BespokeSynthWASM PRIVATE
    BESPOKE_WASM=1
    $<$<BOOL:${BESPOKE_WASM_WEBGPU}>:BESPOKE_WEBGPU=1>
    $<$<BOOL:${BESPOKE_WASM_SDL2_AUDIO}>:BESPOKE_SDL2_AUDIO=1>
    $<$<BOOL:${BESPOKE_WASM_LEGACY_PANELS}>:BESPOKE_WASM_LEGACY_PANELS=1>
)

if(BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Debug")
    target_compile_options(BespokeSynthWASM PRIVATE -O0 -g)
elseif(BESPOKE_WASM_BUILD_FLAVOR STREQUAL "Profile")
    target_compile_options(BespokeSynthWASM PRIVATE -O2 -g)
    target_compile_definitions(BespokeSynthWASM PRIVATE BESPOKE_WASM_FRAME_INSTRUMENTATION=1)
else()
    # -flto enables cross-TU inlining/dead-code elimination at link time; only
    # worth its extra build time for the optimized production build.
    target_compile_options(BespokeSynthWASM PRIVATE -O3)
    if(BESPOKE_WASM_LTO)
        target_compile_options(BespokeSynthWASM PRIVATE -flto)
    endif()
endif()

# WASM SIMD (fixed-width 128-bit). Supported by every browser that ships
# AudioWorklet + SharedArrayBuffer; Safari < 16.4 cannot instantiate the module.
# BESPOKE_WASM_SIMD=1 selects the wasm_simd128.h kernels in DspSimd.h.
if(BESPOKE_WASM_SIMD)
    target_compile_options(BespokeSynthWASM PRIVATE -msimd128)
    target_compile_definitions(BespokeSynthWASM PRIVATE BESPOKE_WASM_SIMD=1)
endif()

# Scala parsing (libs/tuning-library) reports bad user files by throwing
# Tunings::TuningError. Only TuningTable.cpp catches, so only it pays for native
# wasm exception handling; both targets must link the wasm-EH runtime.
set_source_files_properties(${BESPOKE_WASM_DIR}/src/TuningTable.cpp PROPERTIES COMPILE_OPTIONS "-fwasm-exceptions")
target_link_options(BespokeSynthWASM PRIVATE -fwasm-exceptions)

# Build EXPORTED_FUNCTIONS from wasm/exports.txt (single manifest; see scripts/check-wasm-exports.sh).
set(BESPOKE_WASM_EXPORT_SYMBOLS "")
if(EXISTS "${BESPOKE_WASM_DIR}/exports.txt")
    file(STRINGS "${BESPOKE_WASM_DIR}/exports.txt" BESPOKE_WASM_EXPORT_LINES)
    foreach(line IN LISTS BESPOKE_WASM_EXPORT_LINES)
        string(STRIP "${line}" line)
        if(line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif()
        list(APPEND BESPOKE_WASM_EXPORT_SYMBOLS "_${line}")
    endforeach()
else()
    message(FATAL_ERROR "Missing ${BESPOKE_WASM_DIR}/exports.txt")
endif()
list(JOIN BESPOKE_WASM_EXPORT_SYMBOLS "','" BESPOKE_WASM_EXPORTED_FUNCTIONS_JOINED)
set(BESPOKE_WASM_EXPORTED_FUNCTIONS "'${BESPOKE_WASM_EXPORTED_FUNCTIONS_JOINED}'")

# Emscripten linker flags
set(EMSCRIPTEN_LINK_FLAGS
    "-sWASM=1"
    "-sINVOKE_RUN=0"
    "-sENVIRONMENT=web" # Browser-only deliverable; trims Node/worker glue from the JS runtime.
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToNewUTF8','stringToUTF8','lengthBytesUTF8','HEAPF32','HEAPU8','HEAP8','wasmMemory']"
    "-sEXPORTED_FUNCTIONS=[${BESPOKE_WASM_EXPORTED_FUNCTIONS}]"
    # 64MB covers static init, the preload bundle, and the canonical patch
    # graph (peak heap is reported as heapBytes in the audio health HUD).
    # add_link_options() above lets sample-heavy patches grow past it; a growth
    # step detaches HEAP views, which the TS host re-reads per block.
    "-sINITIAL_MEMORY=67108864"  # 64MB initial
    # Growth is capped so a runaway patch fails its allocation instead of
    # taking the tab's whole address space. 2GiB is also the largest heap where
    # heap pointers stay below 2^31, so the TS host's `ptr >> 2` view indexing
    # cannot go negative.
    "-sMAXIMUM_MEMORY=2147483648"
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
    if(BESPOKE_WASM_LTO)
        list(APPEND EMSCRIPTEN_LINK_FLAGS "-flto")
    endif()
endif()
if(BESPOKE_WASM_SIMD)
    list(APPEND EMSCRIPTEN_LINK_FLAGS "-msimd128")
endif()

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

# --preload-file packages resource-pack/ at link time; relink when it changes
# (e.g. a re-baked fonts/ui_font.bsdf) so BespokeSynthWASM.data never goes stale.
file(GLOB_RECURSE BESPOKE_WASM_RESOURCE_PACK_FILES CONFIGURE_DEPENDS "${BESPOKE_WASM_DIR}/resource-pack/*")

set_target_properties(BespokeSynthWASM PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS_STR}"
    LINK_DEPENDS "${BESPOKE_WASM_RESOURCE_PACK_FILES}"
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
    ${BESPOKE_WASM_DIR}/tests/test_basic.cpp
    ${BESPOKE_WASM_DIR}/tests/test_graph_engine.cpp
    ${BESPOKE_WASM_DIR}/tests/test_adapters.cpp
    ${BESPOKE_WASM_DIR}/tests/test_patch_io.cpp
    ${BESPOKE_WASM_DIR}/tests/test_graph_contract.cpp
    ${BESPOKE_WASM_DIR}/tests/test_catalog_wave2.cpp
    ${BESPOKE_WASM_DIR}/tests/test_dsp_tuning.cpp
    ${BESPOKE_WASM_DIR}/tests/test_canvas.cpp
    ${BESPOKE_WASM_DIR}/tests/test_stereo.cpp
    ${BESPOKE_WASM_DIR}/tests/synth_globals_stub.cpp
    ${BESPOKE_WASM_DIR}/src/AudioProcessPlan.cpp
    ${BESPOKE_WASM_DIR}/src/AudioGraphEngine.cpp
    ${BESPOKE_WASM_DIR}/src/AudioHealth.cpp
    ${BESPOKE_WASM_DIR}/src/AudioAnalysis.cpp
    ${BESPOKE_WASM_DIR}/src/SampleBuffer.cpp
    ${BESPOKE_WASM_DIR}/src/SampleStore.cpp
    ${BESPOKE_WASM_DIR}/src/InputAudioBus.cpp
    ${BESPOKE_WASM_DIR}/src/LooperArena.cpp
    ${BESPOKE_WASM_DIR}/src/OfflineRender.cpp
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
    ${BESPOKE_WASM_DIR}/src/modules/SamplerModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/LooperModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/PulseToNoteModule.cpp
    ${BESPOKE_WASM_DIR}/src/modules/ScriptModule.cpp
    ${BESPOKE_WASM_DIR}/src/TuningTable.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvas.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvasInput.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvasRender.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleFactory.cpp
    ${BESPOKE_WASM_DIR}/src/ModuleCanvasHelpers.cpp
    ${BESPOKE_WASM_DIR}/src/WasmPatchState.cpp
    ${BESPOKE_WASM_DIR}/src/PixelFont.cpp
    ${BESPOKE_WASM_DIR}/src/SdfFont.cpp
    ${BESPOKE_SOURCE_DIR}/BiquadFilter.cpp
    ${BESPOKE_SOURCE_DIR}/Oscillator.cpp
)

target_include_directories(BespokeSynthWASM_test PRIVATE
    ${BESPOKE_WASM_DIR}/include
    ${BESPOKE_WASM_DIR}/src
    ${BESPOKE_WASM_DIR}/third_party/dr_libs
    ${BESPOKE_SOURCE_DIR}
)

target_compile_definitions(BespokeSynthWASM_test PRIVATE BESPOKE_WASM_RT_ASSERT=1)
target_link_options(BespokeSynthWASM_test PRIVATE -fwasm-exceptions)

# Unit tests exercise the same DSP kernels the app ships (SIMD vs scalar parity).
if(BESPOKE_WASM_SIMD)
    target_compile_options(BespokeSynthWASM_test PRIVATE -msimd128)
    target_compile_definitions(BespokeSynthWASM_test PRIVATE BESPOKE_WASM_SIMD=1)
    target_link_options(BespokeSynthWASM_test PRIVATE -msimd128)
endif()

target_sources(BespokeSynthWASM_test PRIVATE
    ${BESPOKE_WASM_DIR}/src/RtAllocGuard.cpp
    ${BESPOKE_WASM_DIR}/src/AudioRtGuard.cpp
)

set_target_properties(BespokeSynthWASM_test PROPERTIES
    SUFFIX ".js"
    LINK_FLAGS "-sWASM=1 -sINITIAL_MEMORY=67108864 -sSTACK_SIZE=1048576 -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
)
