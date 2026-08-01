#!/bin/bash
# Run tests in QEMU and capture output.
# Exit code 0 = all tests passed, non-zero = failure.
set -e

# Work from the directory where this script lives (test_app)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Verify required binaries exist
for f in build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/rebel-espresso-tests.bin; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found. Run 'idf.py build' in test_app/ first."
        exit 1
    fi
done

# Create flash image (always regenerated — caller should rm beforehand if stale)
if [ ! -f build/qemu_flash.bin ]; then
    echo "Creating QEMU flash image..."
    esptool --chip esp32 merge-bin \
        --output build/qemu_flash.bin \
        --pad-to-size 4MB \
        0x1000 build/bootloader/bootloader.bin \
        0x8000 build/partition_table/partition-table.bin \
        0x10000 build/rebel-espresso-tests.bin
fi

# Verify QEMU is available
if ! command -v qemu-system-xtensa &>/dev/null; then
    echo "ERROR: qemu-system-xtensa not found in PATH."
    echo "  This script must run inside the espressif/idf Docker container."
    exit 1
fi

# Run QEMU in background with serial output to file.
QEMU_LOG="build/qemu_output.log"
rm -f "$QEMU_LOG"
touch "$QEMU_LOG"
echo "Running QEMU..."

qemu-system-xtensa \
    -nographic \
    -machine esp32 \
    -drive file=build/qemu_flash.bin,if=mtd,format=raw \
    -serial file:"$QEMU_LOG" \
    -monitor none \
    -no-reboot &
QEMU_PID=$!

# Poll for test completion or timeout
TIMEOUT=60
ELAPSED=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 1
    ELAPSED=$((ELAPSED + 1))

    # Check if QEMU exited on its own (due to -no-reboot)
    if ! kill -0 $QEMU_PID 2>/dev/null; then
        break
    fi

    # Check if tests completed
    if grep -q "Tests Complete\|0 Failures\|FAIL" "$QEMU_LOG" 2>/dev/null; then
        break
    fi
done

# Kill QEMU if still running
if kill -0 $QEMU_PID 2>/dev/null; then
    kill $QEMU_PID 2>/dev/null
    wait $QEMU_PID 2>/dev/null || true
fi

# Show output
echo "--- QEMU Output ---"
cat "$QEMU_LOG"
echo "---"

# Check results
if grep -q "FAIL" "$QEMU_LOG"; then
    echo "TESTS FAILED"
    exit 1
elif grep -q "Tests Complete\|0 Failures" "$QEMU_LOG"; then
    echo "ALL TESTS PASSED"
    exit 0
else
    CHARS=$(wc -c < "$QEMU_LOG")
    echo ""
    echo "======================================================================"
    echo "QEMU output did not contain expected test results"
    echo "  Output length: $CHARS bytes (timeout: ${TIMEOUT}s, elapsed: ${ELAPSED}s)"
    echo "  QEMU PID was: $QEMU_PID"
    echo "  Possible causes:"
    echo "    - Test binary crashed before running tests"
    echo "    - QEMU timed out waiting for tests to complete"
    echo "    - Flash image is corrupt (try: rm -rf test_app/build && rebuild)"
    echo "======================================================================"
    exit 2
fi
