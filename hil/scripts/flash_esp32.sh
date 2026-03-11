#!/bin/bash
# Flash ESP32 firmware via USB-UART using esptool
# Usage: ./flash_esp32.sh <firmware.bin> [port] [baud]
set -e

FIRMWARE=${1:?"Usage: $0 <firmware.bin> [port] [baud]"}
PORT=${2:-/dev/ttyACM0}   # ESP32-S3 built-in USB JTAG appears as ttyACM, not ttyUSB
BAUD=${3:-921600}

# Flash offset for OTA partition 0 (or factory depending on build)
# We flash the merged binary at offset 0x0 (produced by idf.py build as flash_args)
FLASH_OFFSET=0x0

if [ ! -f "$FIRMWARE" ]; then
    echo "Error: Firmware not found: $FIRMWARE"
    exit 1
fi

if [ ! -c "$PORT" ]; then
    echo "Error: Serial port not found: $PORT"
    echo "Available ports:"
    ls /dev/ttyACM* 2>/dev/null || true
    exit 1
fi

echo "Flashing ESP32:"
echo "  Firmware: $FIRMWARE"
echo "  Port:     $PORT"
echo "  Baud:     $BAUD"

# Erase NVS partition (0x9000, 24KB) so the device falls back to credentials.h
# rather than using a previously provisioned AWS IoT Core broker URI from NVS.
echo "Erasing NVS partition..."
esptool.py \
    --chip esp32s3 \
    --port "$PORT" \
    --baud "$BAUD" \
    erase_region 0x9000 0x6000

# Erase OTA data partition (0xF000, 8KB) so the bootloader resets to factory slot.
# Without this, the bootloader tries to boot the previously OTA-updated slot which
# has a different SHA-256 than the newly flashed firmware → boot loop.
echo "Erasing OTA data partition..."
esptool.py \
    --chip esp32s3 \
    --port "$PORT" \
    --baud "$BAUD" \
    erase_region 0xF000 0x2000

# Use esptool - supports merged binary or component binaries
# Check if this is a merged binary (single file) or if we need flash_args
if [[ "$FIRMWARE" == *.bin ]]; then
    esptool.py \
        --chip esp32s3 \
        --port "$PORT" \
        --baud "$BAUD" \
        --before default_reset \
        --after hard_reset \
        write_flash \
        --flash_mode dio \
        --flash_freq 80m \
        --flash_size 8MB \
        "$FLASH_OFFSET" "$FIRMWARE"
fi

echo "ESP32 flash complete. Waiting for boot..."
sleep 3
