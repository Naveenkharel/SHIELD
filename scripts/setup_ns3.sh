#!/usr/bin/env bash
# setup_ns3.sh – Minimal automated ns-3 build for Ubuntu 24.04 (core networking)
# Target: ns-3.45 (latest stable as of writing). Change NS3_VERSION if a newer release exists.
# This script installs build dependencies, downloads the source tarball, builds
# an optimised ns-3, and runs the "hello-simulator" example to verify the build.
#
# NS3_ROOT controls where the FINAL built ns-3 source tree ends up (this is the
# directory link_sim.sh expects, and the directory you `cd` into to run ./ns3).
# It defaults to ~/ns-3-45 but can be overridden:
#   NS3_ROOT=/some/other/path ./scripts/setup_ns3.sh
#
# Download + extraction happen in a separate staging directory, NEVER in
# whatever directory you happen to run this script from — this matters
# because this script lives inside a git repo (scripts/), and a multi-hundred-MB
# ns-3 source tree must never land inside a tracked repo directory by accident.

set -euo pipefail

# ---------- Configuration ----------
NS3_VERSION="3.45"                     # Update when a new stable ns-3 is released
NS3_TARBALL="ns-allinone-${NS3_VERSION}.tar.bz2"
NS3_URL="https://www.nsnam.org/release/${NS3_TARBALL}"

NS3_ROOT="${NS3_ROOT:-$HOME/ns-3-45}"                 # final location of the built ns-3.45 source dir
STAGE_DIR="${STAGE_DIR:-$HOME/.cache/ns3-build-stage}" # scratch area for download/extract, never the repo

# Avoid building as root – ns-3 refuses to do so.
if [[ $EUID -eq 0 ]]; then
    echo "ERROR: Do not run this script as root. Run as a normal user; sudo will be used for apt." >&2
    exit 1
fi

echo "=== ns-3 setup script ==="
echo "Target version: ns-${NS3_VERSION}"
echo "Final install location (NS3_ROOT): ${NS3_ROOT}"
echo "Staging directory (download/extract only): ${STAGE_DIR}"

if [[ -d "${NS3_ROOT}" ]]; then
    echo "ERROR: ${NS3_ROOT} already exists. Refusing to overwrite." >&2
    echo "       Remove it first, or set NS3_ROOT to a different path." >&2
    exit 1
fi

# ---------- 1. Install build dependencies ----------
echo "Step 1: Updating package lists..."
sudo apt-get update -qq

REQUIRED_PKGS=( build-essential python3 python3-dev cmake ninja-build
                git pkg-config libsqlite3-dev libxml2-dev wget )

echo "Installing required packages: ${REQUIRED_PKGS[*]}"
if ! sudo apt-get install -y "${REQUIRED_PKGS[@]}"; then
    echo "ERROR: Failed to install one or more packages: ${REQUIRED_PKGS[*]}" >&2
    exit 1
fi

# ---------- 2. Download + extract ns-3 source (into STAGE_DIR, not cwd) ----------
# Trade-off comment (unchanged from before):
# We use the official tarball instead of a git clone. A tarball is smaller and
# downloads faster because it doesn't include the entire git history. It is also
# less likely to fail on slow/unstable connections. The downside is that the
# version is hard-coded; when a new release appears you must edit NS3_VERSION.
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
rm -rf "ns-allinone-${NS3_VERSION}"   # clean any partial extraction from a previous failed run
tar xjf "${NS3_TARBALL}"

NS3_SRC_DIR="${STAGE_DIR}/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}"
if [[ ! -d "${NS3_SRC_DIR}" ]]; then
    echo "ERROR: Expected directory ${NS3_SRC_DIR} not found after extraction." >&2
    exit 1
fi

# ---------- 3. Move the extracted source to its FINAL location before building ----------
# Important: this happens BEFORE ./ns3 configure, not after — CMake bakes
# absolute paths into CMakeCache.txt at configure time, so the source must
# already be at NS3_ROOT before configure runs, not moved there afterward.
echo "Step 3: Moving extracted source to ${NS3_ROOT}..."
mkdir -p "$(dirname "${NS3_ROOT}")"
mv "${NS3_SRC_DIR}" "${NS3_ROOT}"

cd "${NS3_ROOT}"

# ---------- 4. Configure and build ----------
# --build-profile=optimized explanation:
#   optimized  → -O3, no debug symbols, no extra runtime assertions. Simulation
#                runs significantly faster – crucial for thousands of training
#                episodes where every second counts.
#   debug      → -O0 -g, includes sanity checks and is much slower (often 5-10x).
#                Only useful when you need to step through the ns-3 code itself.
echo "Step 4: Configuring ns-3 (optimized build)..."
./ns3 configure --build-profile=optimized --enable-examples --enable-tests

echo "Building ns-3 (this may take several minutes)..."
./ns3 build

# ---------- 5. Verify with hello-simulator ----------
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
echo "ns-3 successfully built and tested."
echo "ns-3 source directory (NS3_ROOT): ${NS3_ROOT}"
echo ""
echo "Next step — link your project code into scratch/:"
echo "  NS3_ROOT=${NS3_ROOT} ./scripts/link_sim.sh"
echo "=============================================="
