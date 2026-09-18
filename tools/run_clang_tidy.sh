#!/usr/bin/env bash
# Run clang-tidy over the library, apps and tests against a configured build
# directory. Usage: tools/run_clang_tidy.sh [build-dir]
#
# On macOS this passes the active SDK explicitly. A clang-tidy installed
# separately from Apple's toolchain (via pip or Homebrew) does not know Apple's
# default sysroot, and without it every <vector>/<chrono> include fails to
# resolve and the output fills with errors that have nothing to do with the code.
set -euo pipefail

BUILD_DIR="${1:-build}"
if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
    echo "error: ${BUILD_DIR}/compile_commands.json not found." >&2
    echo "Configure first: cmake -S . -B ${BUILD_DIR} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    exit 1
fi

EXTRA=()
if [[ "$(uname -s)" == "Darwin" ]]; then
    EXTRA+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)"
            "--extra-arg=-Wno-unused-command-line-argument")
fi

mapfile -t SOURCES < <(git ls-files 'src/*.cpp' 'apps/*.cpp' 'tests/*.cpp')
clang-tidy -p "${BUILD_DIR}" "${EXTRA[@]}" "${SOURCES[@]}"
