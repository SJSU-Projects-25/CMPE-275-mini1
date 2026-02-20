#!/bin/bash

###############################################################################
# Build Validation Script
# 
# Quick script to validate that the project builds correctly.
# Usage: bash scripts/validate_build.sh
###############################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "==================================================================="
echo "CMPE 275 Mini1 - Build Validation"
echo "==================================================================="
echo ""

cd "$PROJECT_ROOT"

# Check if CMake is available
if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found. Please install CMake 3.20 or newer."
    echo ""
    echo "Installation options:"
    echo "  macOS (Homebrew):  brew install cmake"
    echo "  Linux (apt):       sudo apt-get install cmake"
    echo "  Linux (yum):       sudo yum install cmake"
    echo "  Or download from:  https://cmake.org/download/"
    echo ""
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
echo "✓ CMake version: $CMAKE_VERSION"

# Check if C++ compiler is available
if command -v g++ &> /dev/null; then
    GCC_VERSION=$(g++ --version | head -n1)
    echo "✓ Compiler: $GCC_VERSION"
elif command -v clang++ &> /dev/null; then
    CLANG_VERSION=$(clang++ --version | head -n1)
    echo "✓ Compiler: $CLANG_VERSION"
else
    echo "ERROR: No C++ compiler found (g++ or clang++)"
    exit 1
fi

echo ""
echo "Creating build directory..."
mkdir -p build
cd build

echo "Configuring with CMake..."
cmake .. || {
    echo ""
    echo "ERROR: CMake configuration failed!"
    exit 1
}

echo ""
echo "Building project..."
cmake --build . || {
    echo ""
    echo "ERROR: Build failed!"
    exit 1
}

echo ""
echo "==================================================================="
echo "Build Validation Complete ✓"
echo "==================================================================="
echo ""
echo "Built targets:"
if [ -f "bin/taxi_bench" ]; then
    echo "  ✓ taxi_bench (benchmark harness)"
fi
if [ -f "bin/test_csv_reader" ]; then
    echo "  ✓ test_csv_reader (test utility)"
fi
if [ -f "lib/libtaxi_core.a" ] || [ -f "lib/libtaxi_core.so" ]; then
    echo "  ✓ libtaxi_core (library)"
fi
echo ""
echo "To run benchmarks:"
echo "  ./bin/taxi_bench <path_to_csv> [num_runs]"
echo ""
