#!/bin/bash
# Run tests in QEMU and capture output.
# Exit code 0 = all tests passed, non-zero = failure.
set -e

cd /workspace/test_app

# Create flash image if not present
if [ ! -f build/qemu_flash.bin ]; then
    esptool.py --chip esp32 merge_bin \
        --output build/qemu_flash.bin \
        --fill-flash-size 4MB \
        0x1000 build/bootloader/bootloader.bin \
        0x8000 build/partition_table/partition-table.bin \
        0x10000 build/rebel-espresso-tests.bin
fi

# Run QEMU with timeout, capture output
OUTPUT=$(timeout 45 qemu-system-xtensa \
    -nographic \
    -machine esp32 \
    -drive file=build/qemu_flash.bin,if=mtd,format=raw \
    -serial mon:stdio \
    -no-reboot 2>&1 || true)

echo "$OUTPUT"

# Check for test results
if echo "$OUTPUT" | grep -q "FAIL"; then
    echo "TESTS FAILED"
    exit 1
elif echo "$OUTPUT" | grep -q "Tests Complete\|0 Failures"; then
    echo "ALL TESTS PASSED"
    exit 0
else
    echo "QEMU output did not contain expected test results"
    exit 2
fi
