#!/bin/bash
# QLog 代码格式化脚本

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "========================================="
echo "QLog Code Format Script"
echo "========================================="

PATHS=(
    "src/"
    "include/"
    "test/cpp/"
    "benchmark/cpp/"
    "demo/"
)

for path in "${PATHS[@]}"; do
    full_path="$PROJECT_ROOT/$path"
    if [ -d "$full_path" ]; then
        echo "Formatting $path..."
        find "$full_path" \( -name "*.h" -o -name "*.cpp" -o -name "*.inl" \) \
            -print0 | xargs -0 clang-format -i --style=file:"$PROJECT_ROOT/.clang-format"
    fi
done

echo "========================================="
echo "Code formatting complete!"
echo "========================================="
