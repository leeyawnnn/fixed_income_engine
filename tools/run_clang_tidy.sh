#!/usr/bin/env bash
# Run clang-tidy over the library, apps and tests against a configured build
# directory. Usage: tools/run_clang_tidy.sh [build-dir]
#
# Two portability notes, both learned the hard way:
#
#   * `mapfile` is bash 4+. macOS ships bash 3.2, where it silently does not
#     exist, so an earlier version of this script collected an empty file list
#     and reported a clean run having checked nothing. The loop below works on
#     3.2, and the script now refuses to run with no sources.
#   * A clang-tidy that did not ship with Xcode does not know Apple's default
#     sysroot, so without -isysroot every standard header fails to resolve and
#     the output fills with errors that have nothing to do with the code.
set -euo pipefail

BUILD_DIR="${1:-build}"
if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "error: ${BUILD_DIR}/compile_commands.json not found." >&2
    echo "Configure first: cmake -S . -B ${BUILD_DIR} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    exit 1
fi

SOURCES=()
while IFS= read -r file; do
    SOURCES+=("$file")
done < <(git ls-files 'src/*.cpp' 'apps/*.cpp' 'tests/*.cpp')

if [[ ${#SOURCES[@]} -eq 0 ]]; then
    echo "error: no sources matched; refusing to report a clean run." >&2
    exit 1
fi
echo "clang-tidy: checking ${#SOURCES[@]} files" >&2

EXTRA=()
if [[ "$(uname -s)" == "Darwin" ]]; then
    EXTRA+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)"
            "--extra-arg=-Wno-unused-command-line-argument")
fi

clang-tidy -p "${BUILD_DIR}" "${EXTRA[@]}" "${SOURCES[@]}"
