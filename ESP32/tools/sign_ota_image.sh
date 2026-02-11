#!/bin/bash
# Sign OTA image for secure delivery
# Usage: ./sign_ota_image.sh <input.bin> <output_signed.bin>

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 <input.bin> <output_signed.bin>"
    echo ""
    echo "Example:"
    echo "  $0 build/iot_gateway.bin build/iot_gateway_signed.bin"
    exit 1
fi

INPUT_BIN=$1
OUTPUT_BIN=$2
SIGNING_KEY="../keys/ota_signing_key.pem"

# Check if input file exists
if [ ! -f "$INPUT_BIN" ]; then
    echo "Error: Input file not found: $INPUT_BIN"
    exit 1
fi

# Check if signing key exists
if [ ! -f "$SIGNING_KEY" ]; then
    echo "Error: Signing key not found at $SIGNING_KEY"
    echo "Please generate keys first using:"
    echo "  cd ../keys"
    echo "  openssl genrsa -out ota_signing_key.pem 3072"
    exit 1
fi

echo "Signing OTA image..."
echo "  Input:  $INPUT_BIN"
echo "  Output: $OUTPUT_BIN"
echo "  Key:    $SIGNING_KEY"

# Sign the binary using ESP-IDF secure signing tool
espsecure.py sign_data --version 2 \
    --keyfile "$SIGNING_KEY" \
    --output "$OUTPUT_BIN" \
    "$INPUT_BIN"

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ OTA image signed successfully: $OUTPUT_BIN"
    ls -lh "$OUTPUT_BIN"
else
    echo ""
    echo "✗ Signing failed!"
    exit 1
fi
