#!/usr/bin/env bash
# setup_ns3.sh – Cross-platform ns-3 build (Ubuntu 24.04 / macOS)
# Detects the OS and installs build dependencies accordingly:
#   Linux  -> apt-get (build-essential, cmake, ninja-build, etc.)
#   macOS  -> Homebrew (cmake, ninja, etc.) + Xcode Command Line Tools
#             for the clang++ compiler (ns-3 works fine on clang++, no
#             need to install g++ separately on macOS).
# Everything else (download, build, verify) is identical across platforms.
set -euo pipefail

NS3_VERSION="3.45"
NS3_TARBALL="ns-allinone-${NS3_VERSION}.tar.bz2"
NS3_URL="https://www.nsnam.org/release/${NS3_TARBALL}"

NS3_ROOT="${NS3_ROOT:-$HOME/ns-3-45}"                  # final location of the built ns-3 source dir
STAGE_DIR="${STAGE_DIR:-$HOME/.cache/ns3-build-stage}" # scratch area for download/extract, never the repo

OS_NAME="$(uname -s)"

# Portable root check (works on Linux and macOS)
if [[ "$(id -u)" -eq 0 ]]; then
    echo "ERROR: Do not run this script as root. Run as a normal user." >&2
    exit 1
fi

echo "=== ns-3 setup script ==="
echo "Detected OS: ${OS_NAME}"
echo "Target version: ns-${NS3_VERSION}"
echo "Final install location (NS3_ROOT): ${NS3_ROOT}"
echo "Staging directory (download/extract only): ${STAGE_DIR}"

if [[ -d "${NS3_ROOT}" ]]; then
    echo "ERROR: ${NS3_ROOT} already exists. Refusing to overwrite." >&2
    echo "       Remove it first, or set NS3_ROOT to a different path." >&2
    exit 1
fi

# ---------- 1. Install build dependencies (platform-specific) ----------
if [[ "${OS_NAME}" == "Darwin" ]]; then
    echo "Step 1: Checking macOS build tools..."

    # ns-3 works fine on recent clang++ - Xcode Command Line Tools provide
    # it, and there's no need to also install g++ on macOS.
    if ! xcode-select -p >/dev/null 2>&1; then
        echo "ERROR: Xcode Command Line Tools not installed." >&2
        echo "       Run:  xcode-select --install" >&2
        echo "       Then re-run this script once that finishes (it opens" >&2
        echo "       a GUI installer - this script can't do it unattended)." >&2
        exit 1
    fi
    echo "Xcode Command Line Tools found ($(xcode-select -p))."

    if ! command -v brew >/dev/null 2>&1; then
        echo "ERROR: Homebrew not installed." >&2
        echo "       Install it from https://brew.sh, then re-run this script." >&2
        exit 1
    fi
    echo "Homebrew found ($(brew --version | head -1))."

    # Base packages needed by ns-3
    BREW_PKGS=(cmake ninja git pkg-config sqlite libxml2 wget)

    # Python 3 is required by ns-3's ./ns3 build wrapper.
    # Xcode Command Line Tools usually provides python3, so only install via
    # brew if no python3 is already available.
    if ! command -v python3 >/dev/null 2>&1; then
        BREW_PKGS+=(python3)
    fi

    echo "Installing required packages via brew (no sudo needed for brew)..."
    brew install "${BREW_PKGS[@]}" || {
        echo "ERROR: brew install failed for one or more packages." >&2
        exit 1
    }

elif [[ "${OS_NAME}" == "Linux" ]]; then
    echo "Step 1: Updating package lists..."
    sudo apt-get update -qq

    REQUIRED_PKGS=( build-essential python3 python3-dev cmake ninja-build
                    git pkg-config libsqlite3-dev libxml2-dev wget )

    echo "Installing required packages: ${REQUIRED_PKGS[*]}"
    if ! sudo apt-get install -y "${REQUIRED_PKGS[@]}"; then
        echo "ERROR: Failed to install one or more packages: ${REQUIRED_PKGS[*]}" >&2
        exit 1
    fi

else
    echo "ERROR: Unsupported OS '${OS_NAME}'. This script supports Linux and macOS only." >&2
    exit 1
fi

# ---------- 2. Download + extract ns-3 source (into STAGE_DIR, not cwd) ----------
mkdir -p "${STAGE_DIR}"
cd "${STAGE_DIR}"

echo "Step 2: Downloading ${NS3_TARBALL}..."
if [[ ! -f "${NS3_TARBALL}" ]]; then
    wget -q --show-progress "${NS3_URL}" || {
        echo "ERROR: Could not download ${NS3_URL}" >&2
        exit 1
    }
else
    echo "Tarball already cached in ${STAGE_DIR}, skipping download."
fi

echo "Extracting..."
rm -rf "ns-allinone-${NS3_VERSION}"
tar xjf "${NS3_TARBALL}"

NS3_SRC_DIR="${STAGE_DIR}/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}"
if [[ ! -d "${NS3_SRC_DIR}" ]]; then
    echo "ERROR: Expected directory ${NS3_SRC_DIR} not found after extraction." >&2
    exit 1
fi

# ---------- 3. Move the extracted source to its FINAL location before building ----------
# Important: this happens BEFORE ./ns3 configure, not after - CMake bakes
# absolute paths into its build files at configure time.
echo "Step 3: Moving extracted source to ${NS3_ROOT}..."
mkdir -p "$(dirname "${NS3_ROOT}")"
mv "${NS3_SRC_DIR}" "${NS3_ROOT}"

cd "${NS3_ROOT}"

# ---------- 4. Configure and build ----------
echo "Step 4: Configuring ns-3 (optimized build)..."
./ns3 configure --build-profile=optimized --enable-examples --enable-tests

echo "Building ns-3 (this may take several minutes)..."
./ns3 build

# ---------- 5. Verify with a simple std::cout scratch program ----------
echo "Step 5: Verifying build..."
cat > scratch/verify.cc << 'EOF'
#include <iostream>
int main() { std::cout << "Hello Simulator" << std::endl; return 0; }
EOF
if ./ns3 run verify 2>&1 | grep -q "Hello Simulator"; then
    echo "PASS: Verification output contains 'Hello Simulator'"
    rm -f scratch/verify.cc
else
    echo "FAIL: Verification did not produce the expected string" >&2
    rm -f scratch/verify.cc
    exit 1
fi

# ---------- 6. Final information ----------
echo ""
echo "=============================================="
echo "ns-3 successfully built and tested on ${OS_NAME}."
echo "ns-3 source directory (NS3_ROOT): ${NS3_ROOT}"
echo ""
echo "Next step - link your project code into scratch/:"
echo "  NS3_ROOT=${NS3_ROOT} ./scripts/link_sim.sh"
echo "=============================================="