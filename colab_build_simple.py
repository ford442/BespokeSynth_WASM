# @title 🔨 BeSpoke Synth WASM Builder (Simple - Recommended)
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

# 1. CLEANUP: Check for corrupt directory
if os.path.exists(target_dir) and not os.path.exists(os.path.join(target_dir, ".git")):
    print(f"🗑️ Found corrupt directory at {target_dir}. Removing...")
    !rm -rf {target_dir}

# 2. CLONE OR UPDATE
if not os.path.exists(target_dir):
    print(f"⬇️ Cloning {GIT_REPO}...")
    !git clone --recursive {GIT_REPO} {target_dir}
else:
    print(f"🔄 Pulling latest changes...")
    %cd {target_dir}
    !git pull

# 3. EMSCRIPTEN SETUP
print("🔧 Setting up Emscripten...")
emsdk_dir = "/content/emsdk"

if not os.path.exists(emsdk_dir):
    print("⬇️ Installing Emsdk...")
    !git clone https://github.com/emscripten-core/emsdk.git {emsdk_dir}
    !cd {emsdk_dir} && ./emsdk install 3.1.56
    !cd {emsdk_dir} && ./emsdk activate 3.1.56

# Source emsdk
import subprocess
env_setup = !source {emsdk_dir}/emsdk_env.sh && env
for line in env_setup:
    if '=' in line:
        key, val = line.split('=', 1)
        os.environ[key] = val

!emcc --version | head -1

# 4. BUILD USING EXISTING BUILD SCRIPT
%cd {target_dir}

# Setup dependencies
print("📦 Setting up dependencies...")
!rm -rf libs/nanovg
!git clone https://github.com/memononen/nanovg.git libs/nanovg
!mkdir -p libs/leathers && touch libs/leathers/unused-variable
!mkdir -p libs/nanovg_fix/nanovg
!cp libs/nanovg/src/*.h libs/nanovg_fix/nanovg/

# Clean previous build
!rm -rf wasm/build wasm/dist

# Build using the repo's build script
print("🔨 Building WASM...")
!cd wasm && bash build.sh

# 5. COPY OUTPUTS
print("📋 Copying outputs...")
!mkdir -p dist
!cp wasm/dist/BespokeSynthWASM.js dist/ 2>/dev/null || cp wasm/build/BespokeSynthWASM.js dist/ 2>/dev/null
!cp wasm/dist/BespokeSynthWASM.wasm dist/ 2>/dev/null || cp wasm/build/BespokeSynthWASM.wasm dist/ 2>/dev/null
!cp wasm/dist/index.html dist/ 2>/dev/null || cp wasm/build/BespokeSynthWASM.html dist/index.html 2>/dev/null

# 6. PATCH HTML
html_path = f"{target_dir}/dist/index.html"
if os.path.exists(html_path):
    with open(html_path, 'r') as f:
        content = f.read()
    content = content.replace('src="/', 'src="./').replace('href="/', 'href="./')
    with open(html_path, 'w') as f:
        f.write(content)
    print("✅ Patched HTML paths")

# 7. DEPLOY
print("🚀 Deploying...")
if os.path.exists(f"{target_dir}/deploy.py"):
    !cd {target_dir} && python3 deploy.py
else:
    print(f"📁 Build complete! Files in: {target_dir}/dist/")
    print("⚠️ No deploy.py found - manually upload the dist folder")
