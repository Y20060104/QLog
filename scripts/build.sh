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

readonly build_name="${build_type,,}"
readonly build_dir="${project_root}/build/${build_name}"

for required_command in cmake ninja; do
    if ! command -v "${required_command}" >/dev/null 2>&1; then
        echo "Missing required command: ${required_command}"
        exit 1
    fi
done

echo "Configuring QLog (${build_type})..."

cmake \
    -S "${project_root}" \
    -B "${build_dir}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "Building QLog..."

cmake --build "${build_dir}" --parallel

echo "Running tests..."

ctest \
    --test-dir "${build_dir}" \
    --output-on-failure

echo "Build and tests passed: ${build_dir}"