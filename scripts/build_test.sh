#!/usr/bin/env bash

set -Eeuo pipefail

readonly script_dir="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
    pwd
)"

readonly project_root="$(
    cd -- "${script_dir}/.."
    pwd
)"

readonly build_type="${1:-Debug}"

case "${build_type}" in
    Debug | Release | RelWithDebInfo | MinSizeRel)
        ;;
    *)
        echo "Usage: $0 [Debug|Release|RelWithDebInfo|MinSizeRel]"
        exit 2
        ;;
esac

for command_name in cmake ninja ctest; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Missing required command: ${command_name}"
        exit 1
    fi
done

readonly build_dir="${project_root}/build/test/${build_type,,}"

echo "Configuring QLog (${build_type}, tests enabled)..."

cmake \
    -S "${project_root}" \
    -B "${build_dir}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TESTING=ON

echo "Building QLog tests..."

cmake --build "${build_dir}" --parallel

echo "Running QLog tests..."

ctest \
    --test-dir "${build_dir}" \
    --output-on-failure \
    --no-tests=error

echo "QLog tests passed: ${build_dir}"