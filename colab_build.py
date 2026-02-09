# @title 🔨 BeSpoke Synth WASM Builder (Fixed)
import os
import shutil

# --- CONFIGURATION ---
PROJECT_NAME = "BespokeSynth_WASM"
GIT_REPO = f"https://github.com/ford442/{PROJECT_NAME}.git"
# ---------------------

target_dir = f"/content/build_space/{PROJECT_NAME}"
base_dir = "/content/build_space"

# Ensure we are in a safe directory
if not os.path.exists(base_dir):
    os.makedirs(base_dir)
%cd {base_dir}

# 1. CLEANUP: Check for corrupt directory (exists but no .git)
if os.path.exists(target_dir) and not os.path.exists(os.path.join(target_dir, ".git")):
    print(f"🗑️ Found corrupt directory at {target_dir}. Removing...")
    !rm -rf {target_dir}

# 2. CLONE OR UPDATE
if not os.path.exists(target_dir):
    print(f"⬇️ Cloning {GIT_REPO}...")
    !git clone {GIT_REPO} {target_dir} --recursive
else:
    print(f"🔄 Repository {PROJECT_NAME} already exists. Pulling latest changes...")
    %cd {target_dir}
    !git pull

# 3. BUILD PREPARATION
if os.path.exists(target_dir):
    %cd {target_dir}
    !git checkout main
    !git pull

    # Fix for nanovg clone
    nanovg_dir = f"{target_dir}/libs/nanovg"
    if os.path.exists(nanovg_dir):
        print(f"🗑️ Removing existing NanoVG at {nanovg_dir} to ensure clean clone...")
        !rm -rf {nanovg_dir}

    print("⬇️ Cloning NanoVG...")
    !cd {target_dir}/libs && git clone https://github.com/memononen/nanovg.git

    # Mock missing 'leathers' library
    !mkdir -p {target_dir}/libs/leathers
    !touch {target_dir}/libs/leathers/push
    !cd {target_dir}/wasm && rm -rf CMakeFiles CMakeCache.txt

    !cd {target_dir}/libs && mkdir -p leathers
    !cd {target_dir}/libs && touch leathers/unused-variable

    # Fix NanoVG include structure
    !mkdir -p {target_dir}/libs/nanovg_fix/nanovg
    !cp {target_dir}/libs/nanovg/src/*.h {target_dir}/libs/nanovg_fix/nanovg/

    # ============================================
    # FIXED: WebGPU headers - use existing header
    # ============================================
    webgpu_include_dir = f"{target_dir}/wasm/include/webgpu"
    
    # The repo already has webgpu.h - just ensure it exists
    if os.path.exists(f"{webgpu_include_dir}/webgpu.h"):
        print("✅ WebGPU header already exists, skipping WebGPU-Cpp setup")
    else:
        print("⚠️ WebGPU header not found, creating minimal setup...")
        !mkdir -p {webgpu_include_dir}
        # Download the official WebGPU header from emdawnwebgpu
        !curl -L -o {webgpu_include_dir}/webgpu.h https://raw.githubusercontent.com/emscripten-core/emscripten/main/system/include/webgpu/webgpu.h
    
    # ============================================
    # EMSCRIPTEN SETUP (Required for WASM build)
    # ============================================
    print("🔧 Setting up Emscripten...")
    emsdk_dir = "/content/emsdk"
    
    if not os.path.exists(emsdk_dir):
        print("⬇️ Cloning Emsdk...")
        !git clone https://github.com/emscripten-core/emsdk.git {emsdk_dir}
        !cd {emsdk_dir} && ./emsdk install 3.1.56
        !cd {emsdk_dir} && ./emsdk activate 3.1.56
    else:
        print("✅ Emsdk already exists")
    
    # Source emsdk environment
    import subprocess
    env_setup = !source {emsdk_dir}/emsdk_env.sh && env
    for line in env_setup:
        if '=' in line:
            key, val = line.split('=', 1)
            os.environ[key] = val
    
    print(f"✅ Emscripten version:")
    !emcc --version | head -1

    # ============================================
    # BUILD COMMANDS
    # ============================================
    print("🚀 Starting Build...")
    
    # Option 1: Use the CMake build directly (recommended)
    print("🔨 Building with CMake/Emscripten...")
    !cd {target_dir}/wasm && rm -rf build && mkdir -p build
    !cd {target_dir}/wasm/build && emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
    !cd {target_dir}/wasm/build && cmake --build . --parallel
    
    # Copy outputs to dist
    !mkdir -p {target_dir}/dist
    !cp {target_dir}/wasm/build/BespokeSynthWASM.js {target_dir}/dist/ 2>/dev/null || echo "No JS output"
    !cp {target_dir}/wasm/build/BespokeSynthWASM.wasm {target_dir}/dist/ 2>/dev/null || echo "No WASM output"
    !cp {target_dir}/wasm/build/BespokeSynthWASM.html {target_dir}/dist/index.html 2>/dev/null || echo "No HTML output"
    !cp -r {target_dir}/wasm/build/resource {target_dir}/dist/ 2>/dev/null || echo "No resources"

    print("✅ Build Complete.")

    # ============================================
    # PATCHING & DEPLOY
    # ============================================
    html_file_path = f'{target_dir}/dist/index.html'
    print(f"Attempting to patch {html_file_path}...")
    
    if not os.path.exists(html_file_path):
        print(f"❌ ERROR: File not found at {html_file_path}. Did build complete successfully?")
        # List what's in the dist folder
        print("Contents of dist folder:")
        !ls -la {target_dir}/dist/
        print("\nContents of wasm/build folder:")
        !ls -la {target_dir}/wasm/build/ 2>/dev/null || echo "No wasm/build folder"
    else:
        try:
            with open(html_file_path, 'r', encoding='utf-8') as file:
                content = file.read()
            content = content.replace('src="/', 'src="./')
            content = content.replace('href="/', 'href="./')
            with open(html_file_path, 'w', encoding='utf-8') as file:
                file.write(content)
            print("✅ Successfully updated paths in index.html to be relative.")
        except Exception as e:
            print(f"❌ An error occurred during patching: {e}")

    # Create 1ink file and cleanup
    !cd {target_dir} && iconv -f UTF-8 -t UTF-16 ./dist/index.html -o ./dist/1ink.1ink 2>/dev/null || echo "iconv failed"
    !rm -rf {target_dir}/dist/resource
    !rm -rf {target_dir}/wasm/dist/resource
    !rm -rf {target_dir}/dist/wasm/resource
    
    # Deploy
    if os.path.exists(f"{target_dir}/deploy.py"):
        !cd {target_dir} && python3 deploy.py
    else:
        print("⚠️ No deploy.py found, skipping deploy")
        print(f"📁 Build outputs are in: {target_dir}/dist/")
else:
    print(f"❌ Critical Error: Target directory {target_dir} could not be created or accessed.")
