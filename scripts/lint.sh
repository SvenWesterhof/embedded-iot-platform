#!/usr/bin/env bash
# scripts/lint.sh — unified static analysis for ESP32, STM32, and common code.
#
# Usage:
#   ./scripts/lint.sh [esp32|stm32|common|all]
#
# Prerequisites (install once):
#   Ubuntu/Debian:  sudo apt-get install cppcheck clang-tidy-17
#   macOS:          brew install cppcheck llvm
#   Windows (MSYS): pacman -S mingw-w64-x86_64-cppcheck  (clang-tidy via LLVM installer)
#
# Before running clang-tidy you must generate compile_commands.json:
#   ESP32:  cd ESP32 && idf.py build          (generates ESP32/build/compile_commands.json)
#   STM32:  cmake --preset CI                 (generates STM32/build/CI/compile_commands.json)
#           or: cmake --preset Debug/Release with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

set -euo pipefail

TARGET="${1:-all}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="${REPO_ROOT}/tools"
ERRORS=0

# ---------------------------------------------------------------------------
# Resolve clang-tidy binary (prefer versioned to avoid system surprises)
# Override with: CLANG_TIDY_BIN=clang-tidy-18 ./scripts/lint.sh
# ---------------------------------------------------------------------------
if [ -n "${CLANG_TIDY_BIN:-}" ]; then
    CLANG_TIDY="$CLANG_TIDY_BIN"
elif command -v clang-tidy-17 &>/dev/null; then
    CLANG_TIDY="clang-tidy-17"
elif command -v clang-tidy &>/dev/null; then
    CLANG_TIDY="clang-tidy"
else
    echo "WARNING: clang-tidy not found. Skipping clang-tidy checks."
    CLANG_TIDY=""
fi

# ---------------------------------------------------------------------------
# cppcheck runner
# Args: <src_dir> <extra_defines...>
# ---------------------------------------------------------------------------
run_cppcheck() {
    local src_dir="$1"
    shift
    local extra_args=("$@")

    if ! command -v cppcheck &>/dev/null; then
        echo "WARNING: cppcheck not found. Skipping cppcheck for ${src_dir}."
        return 0
    fi

    echo ""
    echo "  [cppcheck] ${src_dir}"
    cppcheck \
        --enable=warning,performance,portability \
        --error-exitcode=1 \
        --inline-suppr \
        --suppress=normalCheckLevelMaxBranches \
        --suppressions-list="${TOOLS_DIR}/cppcheck-suppressions.txt" \
        --std=c11 \
        -I "${REPO_ROOT}/common/include" \
        "${extra_args[@]}" \
        --quiet \
        "${src_dir}" || ERRORS=$((ERRORS + 1))
}

# ---------------------------------------------------------------------------
# clang-tidy runner
# Args: <compile_commands_dir> <source_glob_root> [extra_clang_tidy_args...]
# Skips gracefully if compile_commands.json does not exist yet.
# Extra args (e.g. --extra-arg=-I/path) supplement the compile_commands.json
# include paths. Use this for cross-compiled targets where the compile DB
# paths are correct for the target toolchain but clang needs project headers
# to be resolvable on the host.
#
# Special args consumed by this function (not forwarded to clang-tidy):
#   --remap OLD:NEW   Passed to fix_compile_commands.py to translate paths
#                     that were recorded inside a Docker container to their
#                     equivalent host paths (may be repeated).
# ---------------------------------------------------------------------------
run_clang_tidy() {
    local compile_commands_dir="$1"
    local src_root="$2"
    shift 2

    # Split args: --remap flags go to fix_compile_commands.py; the rest to clang-tidy.
    local extra_clang_args=()
    local remap_args=()
    while [[ $# -gt 0 ]]; do
        if [[ "$1" == "--remap" && $# -gt 1 ]]; then
            remap_args+=("--remap" "$2")
            shift 2
        else
            extra_clang_args+=("$1")
            shift
        fi
    done

    if [ -z "$CLANG_TIDY" ]; then
        return 0
    fi

    if [ ! -f "${compile_commands_dir}/compile_commands.json" ]; then
        echo "  [clang-tidy] SKIP — compile_commands.json not found at ${compile_commands_dir}"
        echo "               Build the firmware first to generate it."
        return 0
    fi

    echo ""
    echo "  [clang-tidy] ${src_root} (using ${compile_commands_dir}/compile_commands.json)"

    # Normalise ARM GCC include paths for host clang-tidy.
    # compile_commands.json from CMake/STM32CubeMX contains non-canonical paths
    # like -I../../cmake/stm32cubemx/../../Drivers/... that clang-tidy cannot
    # resolve. fix_compile_commands.py also strips GCC-only flags (--specs=*).
    # When the build ran inside Docker (e.g. espressif/esp-idf-ci-action mounts
    # the project at /project), file and -I paths use the container prefix.
    # Pass --remap /project/:<src_root>/ to translate them to host paths.
    local FIXED_DB_DIR
    FIXED_DB_DIR="$(mktemp -d)"
    python3 "${TOOLS_DIR}/fix_compile_commands.py" \
        "${compile_commands_dir}/compile_commands.json" \
        "${FIXED_DB_DIR}/compile_commands.json" \
        "${remap_args[@]}"

    # Derive the file list from compile_commands.json so that every file
    # analysed has the correct -I flags from the actual build.
    # Using 'find' instead would pick up test files, docs, and other .c files
    # that were never compiled and therefore have no include paths, causing
    # spurious "file not found" errors that mask real findings.
    mapfile -t SRC_FILES < <(
        python3 -c "
import json, sys

db_path  = '${FIXED_DB_DIR}/compile_commands.json'
src_root = '${src_root}'
excluded = ['/build/', '/Drivers/', '/Middlewares/', '/Core/', '/SEGGER/',
            '/Drivers_BSP/External/']  # vendored third-party display/sensor drivers

try:
    db = json.load(open(db_path))
except Exception:
    sys.exit(0)

seen = set()
for entry in db:
    f = entry.get('file', '')
    if not f.endswith('.c') or not f.startswith(src_root):
        continue
    if any(ex in f for ex in excluded):
        continue
    if f not in seen:
        seen.add(f)
        print(f)
" 2>/dev/null
    )

    if [ ${#SRC_FILES[@]} -eq 0 ]; then
        echo "  [clang-tidy] No source files found in ${src_root}."
        return 0
    fi

    # Run in parallel (4 threads).
    # Output is captured to a temp file so xargs exit code is not swallowed
    # by a grep pipe. TIDY_EXIT is non-zero if any file had a WarningsAsErrors hit.
    local TIDY_OUT
    TIDY_OUT="$(mktemp)"
    local TIDY_EXIT=0

    printf '%s\n' "${SRC_FILES[@]}" | \
    xargs -P4 -I{} "$CLANG_TIDY" \
        -p "${FIXED_DB_DIR}" \
        --config-file="${TOOLS_DIR}/.clang-tidy" \
        "${extra_clang_args[@]}" \
        {} -- \
    > "$TIDY_OUT" 2>&1 || true   # exit code checked by content below

    grep -E "(error|warning):" "$TIDY_OUT" || true

    # clang-diagnostic-error means clang couldn't parse the file due to
    # cross-compilation include path issues (ARM GCC sysroot vs host clang).
    # These are infrastructure noise — the file still gets partial analysis.
    # Only count semantic findings (bugprone-*, clang-analyzer-*, cert-*, etc.)
    # as build-breaking errors.
    SEMANTIC_ERRORS=$(grep ": error:" "$TIDY_OUT" \
        | grep -cv "\[clang-diagnostic-" || true)
    rm -f "$TIDY_OUT"
    rm -rf "$FIXED_DB_DIR"

    if [ "${SEMANTIC_ERRORS}" -gt 0 ]; then
        echo "  [clang-tidy] ${SEMANTIC_ERRORS} semantic error(s) — see output above"
        ERRORS=$((ERRORS + 1))
    fi
}

# ---------------------------------------------------------------------------
# Main analysis targets
# ---------------------------------------------------------------------------
echo "========================================================"
echo " Static Analysis — target: ${TARGET}"
echo " Repository:        ${REPO_ROOT}"
echo "========================================================"

# ---- ESP32 ----
if [[ "$TARGET" == "esp32" || "$TARGET" == "all" ]]; then
    echo ""
    echo "--- ESP32 ---"
    run_cppcheck \
        "${REPO_ROOT}/ESP32" \
        "-DESP_PLATFORM=1" \
        "-DIDF_VER=\"v5.5\"" \
        "-DESP32=1" \
        "-DCONFIG_IDF_TARGET_ESP32S3=1" \
        "-DCONFIG_LWIP_LOCAL_HOSTNAME=\"esp32\""
    # The build runs inside Docker (espressif/esp-idf-ci-action), which mounts
    # the ESP32/ directory as /project. Remap /project/ → the real host path
    # so that file and -I paths in compile_commands.json resolve on the runner.
    run_clang_tidy \
        "${REPO_ROOT}/ESP32/build" \
        "${REPO_ROOT}/ESP32" \
        --remap "/project/:${REPO_ROOT}/ESP32/"
fi

# ---- STM32 ----
if [[ "$TARGET" == "stm32" || "$TARGET" == "all" ]]; then
    echo ""
    echo "--- STM32 ---"
    run_cppcheck \
        "${REPO_ROOT}/STM32" \
        "-DSTM32F767xx=1" \
        "-DSTM32F7=1" \
        "-DUSE_HAL_DRIVER=1" \
        "-DDEBUG=1" \
        "-i" "${REPO_ROOT}/STM32/Middlewares" \
        "-i" "${REPO_ROOT}/STM32/Drivers" \
        "-i" "${REPO_ROOT}/STM32/Core" \
        "-i" "${REPO_ROOT}/STM32/Middleware/SEGGER"

    # Try CI preset output first, fall back to Release, then Debug
    STM32_BUILD=""
    for preset in CI Release Debug; do
        if [ -f "${REPO_ROOT}/STM32/build/${preset}/compile_commands.json" ]; then
            STM32_BUILD="${REPO_ROOT}/STM32/build/${preset}"
            break
        fi
    done
    # Explicitly inject project include dirs so clang-tidy can resolve them
    # when running on the host against cross-compiled (ARM GCC) compile commands.
    run_clang_tidy "${STM32_BUILD:-}" "${REPO_ROOT}/STM32" \
        "--extra-arg=-I${REPO_ROOT}/common/include" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Application" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Middleware" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Middleware/Control" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Middleware/Services" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Middleware/Features" \
        "--extra-arg=-I${REPO_ROOT}/STM32/HAL" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Drivers_BSP" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Drivers_BSP/Custom" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Drivers_BSP/BSP" \
        "--extra-arg=-I${REPO_ROOT}/STM32/OS" \
        "--extra-arg=-I${REPO_ROOT}/STM32/Utils"
fi

# ---- Common ----
if [[ "$TARGET" == "common" || "$TARGET" == "all" ]]; then
    echo ""
    echo "--- common ---"
    run_cppcheck "${REPO_ROOT}/common/src"
fi

# ---------------------------------------------------------------------------
echo ""
echo "========================================================"
if [ "$ERRORS" -gt 0 ]; then
    echo " FAILED: ${ERRORS} check(s) reported errors."
    echo "========================================================"
    exit 1
else
    echo " All checks passed."
    echo "========================================================"
fi
