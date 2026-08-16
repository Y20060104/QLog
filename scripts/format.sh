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

readonly mode="${1:---write}"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "Missing required command: clang-format"
    echo "Install it with: sudo apt install clang-format"
    exit 1
fi

case "${mode}" in
    --write | --check)
        ;;
    *)
        echo "Usage: $0 [--write|--check]"
        exit 2
        ;;
esac

source_files=()

for source_dir in include src tests benchmarks; do
    absolute_dir="${project_root}/${source_dir}"

    if [[ ! -d "${absolute_dir}" ]]; then
        continue
    fi

    while IFS= read -r -d '' source_file; do
        source_files+=("${source_file}")
    done < <(
        find "${absolute_dir}" -type f \
            \( \
                -name '*.c'   -o \
                -name '*.cc'  -o \
                -name '*.cpp' -o \
                -name '*.cxx' -o \
                -name '*.h'   -o \
                -name '*.hh'  -o \
                -name '*.hpp' -o \
                -name '*.hxx' -o \
                -name '*.inl' \
            \) \
            -print0
    )
done

if [[ ${#source_files[@]} -eq 0 ]]; then
    echo "No C/C++ source files found."
    exit 0
fi

if [[ "${mode}" == "--check" ]]; then
    clang-format \
        --style=file \
        --dry-run \
        --Werror \
        "${source_files[@]}"

    echo "Format check passed."
else
    clang-format \
        --style=file \
        -i \
        "${source_files[@]}"

    echo "Formatted ${#source_files[@]} source files."
fi