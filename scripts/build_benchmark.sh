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

readonly build_dir="${project_root}/build/benchmark/release"
readonly with_bqlog="${QLOG_BENCH_WITH_BQLOG:-ON}"
readonly bqlog_root="${QLOG_BENCH_BQLOG_ROOT:-${project_root}/../BqLog}"

case "${with_bqlog}" in
    ON | OFF)
        ;;
    *)
        echo "QLOG_BENCH_WITH_BQLOG must be ON or OFF"
        exit 2
        ;;
esac

for command_name in cmake ninja; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Missing required command: ${command_name}"
        exit 1
    fi
done

if [[ "${with_bqlog}" == "ON" ]] &&
    [[ ! -f "${bqlog_root}/src/bq_log/types/buffer/siso_ring_buffer.cpp" ]]; then
    echo "BQLog SISO source was not found under: ${bqlog_root}"
    echo "Set QLOG_BENCH_BQLOG_ROOT or use QLOG_BENCH_WITH_BQLOG=OFF"
    exit 1
fi

echo "Configuring QLog SPSC benchmark (Release)..."

cmake \
    -S "${project_root}" \
    -B "${build_dir}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TESTING=OFF \
    -DQLOG_BUILD_BENCHMARKS=ON \
    -DQLOG_BENCH_WITH_BQLOG="${with_bqlog}" \
    -DQLOG_BENCH_BQLOG_ROOT="${bqlog_root}"

echo "Building QLog SPSC benchmark..."

cmake --build "${build_dir}" --target qlog_spsc_benchmark --parallel

echo "QLog benchmark build passed: ${build_dir}"
echo "Quick run:"
echo "  ${build_dir}/benchmarks/qlog_spsc_benchmark --preset quick"
