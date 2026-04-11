#!/bin/bash
# QLog 运行消毒器脚本（ASan/TSan/UBSan）

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SANITIZER="${1:-thread}"
BUILD_DIR="${PROJECT_ROOT}/build_sanitizer_${SANITIZER}"

echo "========================================="
echo "QLog Sanitizer Script"
echo "========================================="
echo "Sanitizer: $SANITIZER"
echo

case "$SANITIZER" in
    address|asan)
        SANITIZER_FLAG="QLOG_ENABLE_ASAN=ON"
        ;;
    thread|tsan)
        SANITIZER_FLAG="QLOG_ENABLE_TSAN=ON"
        ;;
    undefined|ubsan)
        SANITIZER_FLAG="QLOG_ENABLE_UBSAN=ON"
        ;;
    *)
        echo "Unknown sanitizer: $SANITIZER"
        echo "Usage: $0 [address|thread|undefined]"
        exit 1
        ;;
esac

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置 CMake with 消毒器
echo "[1/3] Configuring CMake with $SANITIZER..."
cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -D"$SANITIZER_FLAG"=ON \
    -DQLOG_BUILD_TESTS=ON \
    "$PROJECT_ROOT"

# 构建
echo "[2/3] Building..."
cmake --build . --parallel "$(nproc)"

# 运行测试
echo "[3/3] Running tests with $SANITIZER..."
ctest --output-on-failure -C Debug

echo "========================================="
echo "Sanitizer test complete!"
echo "========================================="
