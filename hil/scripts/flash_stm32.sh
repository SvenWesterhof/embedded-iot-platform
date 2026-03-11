#!/bin/bash
# Flash STM32F767 firmware via ST-Link using OpenOCD
# Usage: ./flash_stm32.sh <app.bin> [bootloader.bin]
set -e

APP_BIN=${1:?"Usage: $0 <app.bin> [bootloader.bin]"}
BOOTLOADER_BIN=${2:-}

APP_FLASH_ADDR=0x08008000
BOOTLOADER_FLASH_ADDR=0x08000000

if [ ! -f "$APP_BIN" ]; then
    echo "Error: App firmware not found: $APP_BIN"
    exit 1
fi

echo "Flashing STM32F767 via ST-Link:"
echo "  App:        $APP_BIN @ $APP_FLASH_ADDR"

if [ -n "$BOOTLOADER_BIN" ]; then
    if [ ! -f "$BOOTLOADER_BIN" ]; then
        echo "Error: Bootloader not found: $BOOTLOADER_BIN"
        exit 1
    fi
    echo "  Bootloader: $BOOTLOADER_BIN @ $BOOTLOADER_FLASH_ADDR"
fi

# Flash using OpenOCD with STM32F7 config
OPENOCD_CMD="openocd \
    -f interface/stlink.cfg \
    -f target/stm32f7x.cfg"

if [ -n "$BOOTLOADER_BIN" ]; then
    $OPENOCD_CMD \
        -c "init" \
        -c "reset halt" \
        -c "flash write_image erase \"$BOOTLOADER_BIN\" $BOOTLOADER_FLASH_ADDR" \
        -c "flash write_image erase \"$APP_BIN\" $APP_FLASH_ADDR" \
        -c "reset run" \
        -c "shutdown"
else
    $OPENOCD_CMD \
        -c "init" \
        -c "reset halt" \
        -c "flash write_image erase \"$APP_BIN\" $APP_FLASH_ADDR" \
        -c "reset run" \
        -c "shutdown"
fi

echo "STM32 flash complete. Waiting for boot..."
sleep 2
