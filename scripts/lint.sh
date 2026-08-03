#!/bin/bash
###############################################################################
# lint.sh — Run clang-tidy static analysis on project source files.
#
# Uses compile_commands.json from the build tree, with Xtensa/GCC-specific
# flags stripped so clang-tidy (which uses libclang) can parse the code.
#
# Usage:
#   ./scripts/lint.sh              # lint all project sources
#   ./scripts/lint.sh --fix        # lint and auto-fix where possible
#   ./scripts/lint.sh file.cpp     # lint specific file(s)
###############################################################################
set -euo pipefail

# Resolve project root (parent of scripts/)
PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${PROJ_ROOT}/build}"
COMPILE_DB="${BUILD_DIR}/compile_commands.json"

if [ ! -f "$COMPILE_DB" ]; then
    echo "ERROR: $COMPILE_DB not found. Run 'idf.py build' first."
    exit 1
fi

# Create a sanitized compile_commands.json with Xtensa-incompatible flags removed
# so clang-tidy (which uses libclang, not GCC) can parse the compilation database.
python3 -c "
import json, re, sys

with open('$COMPILE_DB') as f:
    db = json.load(f)

# Flags that are GCC/Xtensa-specific and not understood by clang
remove_flags = [
    '-mlongcalls', '-fno-shrink-wrap', '-fno-tree-switch-conversion',
    '-fstrict-volatile-bitfields', '-fno-jump-tables',
    '-fzero-init-padding-bits=all', '-fno-malloc-dce',
    '-mtext-section-literals', '-Wno-old-style-declaration',
]
# Regex for flags with values
remove_patterns = [
    r'-fmacro-prefix-map=\S+',
    r'@\S+',  # response files (@/path/to/flags)
]

def clean_command(cmd):
    for flag in remove_flags:
        cmd = cmd.replace(' ' + flag, '')
    for pat in remove_patterns:
        cmd = re.sub(pat, '', cmd)
    # Replace xtensa target with a generic one clang understands
    cmd = cmd.replace('xtensa-esp32-elf-gcc', 'clang')
    cmd = cmd.replace('xtensa-esp32-elf-g++', 'clang++')
    cmd = cmd.replace('xtensa-esp-elf-gcc', 'clang')
    cmd = cmd.replace('xtensa-esp-elf-g++', 'clang++')
    return cmd

for entry in db:
    if 'command' in entry:
        entry['command'] = clean_command(entry['command'])

with open('$COMPILE_DB', 'w') as f:
    json.dump(db, f, indent=2)

print(f'Sanitized {len(db)} entries -> $COMPILE_DB', file=sys.stderr)
"

# Parse arguments
FIX_FLAG=""
FILES=()
for arg in "$@"; do
    if [ "$arg" = "--fix" ]; then
        FIX_FLAG="--fix"
    else
        FILES+=("$arg")
    fi
done

# If no files specified, find all project C/C++ source files
if [ ${#FILES[@]} -eq 0 ]; then
    FILES=($(find "${PROJ_ROOT}/components/rebel-espresso/src/control" \
                  "${PROJ_ROOT}/components/rebel-espresso/src/utils" \
                  "${PROJ_ROOT}/components/rebel-espresso/src/sys" \
                  "${PROJ_ROOT}/components/rebel-espresso/src/homekit" \
                  "${PROJ_ROOT}/components/esp-connectivity/src/common" \
                  "${PROJ_ROOT}/components/esp-connectivity/src/wifi" \
                  "${PROJ_ROOT}/components/esp-connectivity/src/sntp" \
                  "${PROJ_ROOT}/main" \
             \( -name '*.cpp' -o -name '*.c' \) \
             2>/dev/null | sort))
    # Also include the top-level connectivity.cpp
    if [ -f "${PROJ_ROOT}/components/esp-connectivity/src/connectivity.cpp" ]; then
        FILES+=("${PROJ_ROOT}/components/esp-connectivity/src/connectivity.cpp")
    fi
fi

if [ ${#FILES[@]} -eq 0 ]; then
    echo "No source files found to lint."
    exit 0
fi

echo "Running clang-tidy on ${#FILES[@]} files..."
echo "  Fix mode: ${FIX_FLAG:-off}"
echo ""

# Run clang-tidy with the sanitized database
FAILED=0
WARNED=0
for f in "${FILES[@]}"; do
    SHORT="${f#${PROJ_ROOT}/}"
    OUTPUT=$(clang-tidy -p "$BUILD_DIR" \
        --config-file="${PROJ_ROOT}/.clang-tidy" \
        --use-color \
        --quiet \
        $FIX_FLAG "$f" 2>&1 || true)

    # Filter out "unknown argument" noise, system header errors, and missing GCC-internal headers
    FILTERED=$(echo "$OUTPUT" | grep -v "unknown argument\|unknown target\|/opt/esp/\|file not found \[clang-diagnostic" || true)

    if echo "$FILTERED" | grep -q "error:"; then
        echo "  $SHORT"
        echo "$FILTERED" | grep -E "error:" | head -10
        echo ""
        FAILED=$((FAILED + 1))
    elif echo "$FILTERED" | grep -q "warning:"; then
        WARNED=$((WARNED + 1))
    fi
done

echo "─────────────────────────────────────────"
if [ $FAILED -gt 0 ]; then
    echo "Lint FAILED: $FAILED file(s) with errors (of ${#FILES[@]} checked)."
    exit 1
elif [ $WARNED -gt 0 ]; then
    echo "Lint passed with $WARNED warning(s) in ${#FILES[@]} files."
else
    echo "Lint passed — no issues in ${#FILES[@]} files."
fi
