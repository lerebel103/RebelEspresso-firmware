#!/bin/bash
###############################################################################
# Build the web UI: gzip the files and output to data/ directory.
# The data/ directory is what gets flashed to the FAT "data" partition.
###############################################################################
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/../data"

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

# Gzip the HTML (single file SPA)
gzip -9 -k "${SCRIPT_DIR}/index.html"
mv "${SCRIPT_DIR}/index.html.gz" "$OUTPUT_DIR/index.html.gz"

# Also keep the uncompressed version for browsers that don't accept gzip
cp "${SCRIPT_DIR}/index.html" "$OUTPUT_DIR/index.html"

echo "Web UI built:"
ls -la "$OUTPUT_DIR"
echo ""
echo "Gzipped size: $(wc -c < "$OUTPUT_DIR/index.html.gz") bytes"
echo "Uncompressed: $(wc -c < "$OUTPUT_DIR/index.html") bytes"
