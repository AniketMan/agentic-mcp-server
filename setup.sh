#!/usr/bin/env bash
# =============================================================================
# UE 5.6 Level Editor — Bootstrap Setup
# Installs .NET SDK, clones UAssetAPI, builds it, installs Python dependencies.
# Run once after cloning the repo.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="${SCRIPT_DIR}/lib/publish"
DOTNET_DIR="${HOME}/.dotnet"
UASSET_TMP="/tmp/UAssetGUI_build"

echo "============================================"
echo "  UE 5.6 Level Editor — Setup"
echo "============================================"
echo ""

# -------------------------------------------------------
# Step 1: Install .NET SDK (if not already installed)
# -------------------------------------------------------
echo "[1/5] Checking .NET SDK..."
if command -v dotnet &>/dev/null; then
    echo "  .NET SDK already installed: $(dotnet --version)"
elif [ -f "${DOTNET_DIR}/dotnet" ]; then
    echo "  .NET SDK found at ${DOTNET_DIR}"
    export PATH="${DOTNET_DIR}:${PATH}"
    export DOTNET_ROOT="${DOTNET_DIR}"
    echo "  Version: $(dotnet --version)"
else
    echo "  Installing .NET SDK 8.0..."
    wget -q https://dot.net/v1/dotnet-install.sh -O /tmp/dotnet-install.sh
    chmod +x /tmp/dotnet-install.sh
    /tmp/dotnet-install.sh --channel 8.0 --install-dir "${DOTNET_DIR}" 2>&1 | tail -3
    export PATH="${DOTNET_DIR}:${PATH}"
    export DOTNET_ROOT="${DOTNET_DIR}"
    echo "  Installed: $(dotnet --version)"
    rm -f /tmp/dotnet-install.sh
fi

# Ensure dotnet is on PATH for the rest of the script
export PATH="${DOTNET_DIR}:${PATH}"
export DOTNET_ROOT="${DOTNET_DIR}"

# -------------------------------------------------------
# Step 2: Clone UAssetGUI + UAssetAPI submodule
# -------------------------------------------------------
echo ""
echo "[2/5] Cloning UAssetGUI + UAssetAPI..."
if [ -d "${LIB_DIR}" ] && [ -f "${LIB_DIR}/UAssetAPI.dll" ]; then
    echo "  UAssetAPI.dll already exists at ${LIB_DIR}. Skipping build."
    echo "  (Delete ${LIB_DIR} and re-run to force rebuild)"
else
    rm -rf "${UASSET_TMP}"
    echo "  Cloning from GitHub..."
    git clone --recursive --depth=1 https://github.com/atenfyr/UAssetGUI.git "${UASSET_TMP}" 2>&1 | tail -3

    # -------------------------------------------------------
    # Step 3: Build and publish UAssetAPI
    # -------------------------------------------------------
    echo ""
    echo "[3/5] Building UAssetAPI..."
    cd "${UASSET_TMP}/UAssetAPI"
    dotnet publish UAssetAPI/UAssetAPI.csproj \
        -c Release \
        -o "${LIB_DIR}" \
        --no-self-contained \
        -f net8.0 2>&1 | tail -5

    # Verify the build
    if [ ! -f "${LIB_DIR}/UAssetAPI.dll" ]; then
        echo ""
        echo "  ERROR: UAssetAPI.dll was not produced. Build failed."
        echo "  Check the output above for errors."
        echo "  Press Enter to exit..."
        read -r
        exit 1
    fi

    # Create runtimeconfig if missing
    if [ ! -f "${LIB_DIR}/UAssetAPI.runtimeconfig.json" ]; then
        cat > "${LIB_DIR}/UAssetAPI.runtimeconfig.json" <<'EOF'
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "8.0.0"
    }
  }
}
EOF
    fi

    echo "  Build successful. Output: ${LIB_DIR}"
    ls -lh "${LIB_DIR}"/*.dll

    # Cleanup
    cd "${SCRIPT_DIR}"
    rm -rf "${UASSET_TMP}"
fi

# -------------------------------------------------------
# Step 4: Install Python dependencies
# -------------------------------------------------------
echo ""
echo "[4/5] Installing Python dependencies..."
if python3 -c "import pythonnet" &>/dev/null; then
    echo "  pythonnet already installed."
else
    echo "  Installing pythonnet..."
    pip3 install pythonnet 2>&1 | tail -3
fi

if python3 -c "import flask" &>/dev/null; then
    echo "  Flask already installed."
else
    echo "  Installing Flask..."
    pip3 install flask 2>&1 | tail -3
fi

# -------------------------------------------------------
# Step 5: Verify everything works
# -------------------------------------------------------
echo ""
echo "[5/5] Verifying installation..."

# Test .NET bridge
VERIFY_RESULT=$(cd "${SCRIPT_DIR}" && python3 -c "
import os, sys
os.environ['DOTNET_ROOT'] = '${DOTNET_DIR}'
from clr_loader import get_coreclr
from pythonnet import set_runtime
rt = get_coreclr(runtime_config='${LIB_DIR}/UAssetAPI.runtimeconfig.json')
set_runtime(rt)
import clr
clr.AddReference('${LIB_DIR}/UAssetAPI.dll')
from UAssetAPI import UAsset
from UAssetAPI import EngineVersion
print('OK')
" 2>&1)

if [ "${VERIFY_RESULT}" = "OK" ]; then
    echo "  UAssetAPI bridge:  OK"
else
    echo "  UAssetAPI bridge:  FAILED"
    echo "  ${VERIFY_RESULT}"
    echo ""
    echo "  The bridge failed to initialize. Check the error above."
    echo "  Common fixes:"
    echo "    - Ensure .NET 8.0 runtime is installed"
    echo "    - Ensure pythonnet is compatible with your Python version"
    echo "  Press Enter to exit..."
    read -r
    exit 1
fi

echo "  Flask server:      OK"
echo ""
echo "============================================"
echo "  Setup complete."
echo ""
echo "  To start the dashboard:"
echo "    cd ${SCRIPT_DIR}"
echo "    python3 run_dashboard.py"
echo ""
echo "  Dashboard will be available at:"
echo "    http://localhost:8080"
echo "============================================"
