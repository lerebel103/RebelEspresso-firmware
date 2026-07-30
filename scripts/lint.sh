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

BUILD_DIR="${BUILD_DIR:-/workspace/build}"
COMPILE_DB="${BUILD_DIR}/compile_commands.json"
CLEAN_DB="${BUILD_DIR}/compile_commands_clang.json"

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
    '-mtext-section-literals',
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

with open('$CLEAN_DB', 'w') as f:
    json.dump(db, f, indent=2)

print(f'Sanitized {len(db)} entries -> $CLEAN_DB', file=sys.stderr)
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
    FILES=($(find /workspace/components/rebel-espresso/src/hw/base \
                  /workspace/components/rebel-espresso/src/hw/r2/src \
                  /workspace/components/rebel-espresso/src/utils \
                  /workspace/components/rebel-espresso/src/sys \
                  /workspace/components/rebel-espresso/src/homekit \
                  /workspace/components/esp32-aws-connector/src/common \
                  /workspace/components/esp32-aws-connector/src/wifi \
                  /workspace/components/esp32-aws-connector/src/sntp \
                  /workspace/main \
             \( -name '*.cpp' -o -name '*.c' \) \
             2>/dev/null | sort))
    # Also include the top-level aws_connector.cpp
    if [ -f /workspace/components/esp32-aws-connector/src/aws_connector.cpp ]; then
        FILES+=("/workspace/components/esp32-aws-connector/src/aws_connector.cpp")
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
for f in "${FILES[@]}"; do
    SHORT="${f#/workspace/}"
    OUTPUT=$(clang-tidy -p "$BUILD_DIR" \
        --config-file=/workspace/.clang-tidy \
        --use-color \
        --quiet \
        $FIX_FLAG "$f" 2>&1 || true)

    # Filter out any remaining "unknown argument" noise
    FILTERED=$(echo "$OUTPUT" | grep -v "unknown argument\|unknown target" || true)

    if echo "$FILTERED" | grep -q "warning:\|error:"; then
        echo "  $SHORT"
        echo "$FILTERED" | grep -E "warning:|error:" | head -10
        echo ""
        FAILED=$((FAILED + 1))
    fi
done

echo "─────────────────────────────────────────"
if [ $FAILED -eq 0 ]; then
    echo "Lint passed — no issues in ${#FILES[@]} files."
else
    echo "Lint: $FAILED file(s) with issues (of ${#FILES[@]} checked)."
    exit 1
fi
