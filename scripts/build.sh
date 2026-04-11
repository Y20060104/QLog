#!/bin/bash
# QLog 构建脚本

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BUILD_TYPE="${1:-Release}"

echo "========================================="
echo "QLog Build Script"
echo "========================================="
echo "Project Root: $PROJECT_ROOT"
echo "Build Type: $BUILD_TYPE"
echo

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置 CMake
echo "[1/3] Configuring CMake..."
cmake \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DQLOG_BUILD_TESTS=ON \
    -DQLOG_BUILD_BENCH=ON \
    -DQLOG_ENABLE_LTO=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    "$PROJECT_ROOT"

# 构建
echo "[2/3] Building..."
cmake --build . --parallel "$(nproc)"

echo "[3/3] Build complete!"
echo "========================================="
echo "Build artifacts:"
echo "  - Library: $BUILD_DIR/lib/"
echo "  - Tests: $BUILD_DIR/bin/"
echo "  - Compile commands: $BUILD_DIR/compile_commands.json"
echo "========================================="
