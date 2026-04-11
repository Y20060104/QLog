#!/bin/bash
# QLog 测试脚本

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "========================================="
echo "QLog Test Script"
echo "========================================="

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found. Run scripts/build.sh first."
    exit 1
fi

cd "$BUILD_DIR"

echo "Running unit tests..."
ctest --output-on-failure -C Release

echo "========================================="
echo "All tests passed!"
echo "========================================="
