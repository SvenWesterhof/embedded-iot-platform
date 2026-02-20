# STM32F767 Sensor Node

Sensor acquisition firmware for the STM32F767ZI, with temperature/humidity sensing, current/voltage monitoring, an IPS display, and a dual-bank OTA bootloader for field updates via the ESP32 gateway.

---

## Architecture

The firmware follows a layered event-driven architecture identical in structure to the ESP32 gateway, with strict downward-only dependencies and an event bus for decoupled communication.

```
┌─────────────────────────────────────────────────────────────────────┐
│  Application Layer                                                  │
│    app_main.c         - Init sequence, FreeRTOS task orchestration   │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  OS Layer                                                           │
│    event_bus.c        - Pub/sub event system                        │
│    os_wrapper.c       - FreeRTOS task/mutex/semaphore abstraction    │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Middleware Layer                                                    │
│                                                                     │
│  Control/                                                           │
│    (state machines, orchestration — prepared for future use)        │
│                                                                     │
│  Services/                                                          │
│    serv_temperature_sensor  - AHT25 reading (1s interval)           │
│    serv_display             - ST7735 display, event-driven updates   │
│    serv_current_monitor     - INA226 current/voltage (implemented)   │
│    serv_blinky              - LED heartbeat (2s toggle)              │
│    serv_firmware_update     - OTA chunk receiver + state machine     │
│                                                                     │
│  Features/                                                          │
│    feat_stm32_protocol      - UART binary protocol responder        │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Drivers Layer                                                      │
│    Drivers_BSP/Custom/  - AHT25, INA226, IPS display drivers        │
│    Drivers_BSP/BSP/     - Board init, pinout, peripheral handles    │
│    HAL/                 - GPIO, I2C, SPI, UART, RTC, Flash, Delay   │
└─────────────────────────────────────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Bootloader (separate binary, 32KB at 0x08000000)                   │
│    bootloader_main.c  - Update check, Bank 2→1 copy, app jump      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Components

### Sensor Services

**`serv_temperature_sensor`** — Reads the AHT25 sensor over I2C at 1-second intervals. Publishes `EVENT_TEMPERATURE_UPDATED` with temperature, humidity, and timestamp. Data is stored in a ring buffer for historical queries from the ESP32.

**`serv_current_monitor`** — Reads the INA226 current/voltage sensor over I2C. Implemented but currently deactivated in the service registry.

**`serv_display`** — Drives the ST7735 IPS display over SPI. Subscribes to temperature and error events, updates the display on data changes. Fully event-driven — no polling.

**`serv_blinky`** — Heartbeat LED toggle every 2 seconds. Visual indicator that the firmware is running.

### Firmware Update Service

**`serv_firmware_update`** — Receives firmware chunks from the ESP32 over UART and writes them to Bank 2 flash. Implements a strict state machine:

```
CMD_FW_UPDATE_START
       │
       ▼
┌──────────────┐
│  IDLE        │
└──────┬───────┘
       │ Erase Bank 2
       ▼
┌──────────────┐
│  ERASING     │
└──────┬───────┘
       │ Erase complete
       ▼
┌──────────────┐
│  RECEIVING   │ ← CMD_FW_UPDATE_CHUNK (252 bytes, sequential)
└──────┬───────┘
       │ CMD_FW_UPDATE_END
       ▼
┌──────────────┐
│  VALIDATING  │ CRC32 check over Bank 2
└──────┬───────┘
       │ CRC OK
       ▼
┌──────────────┐
│  READY       │ → Set RTC backup registers → NVIC_SystemReset()
└──────────────┘
```

Each chunk is validated for sequential ordering and size bounds. Flash writes are verified. On any failure, the service transitions to `FW_UPDATE_ERROR` and waits for an abort or new start command.

### UART Protocol

**`feat_stm32_protocol`** — Responds to commands from the ESP32 over UART (115200 baud, 8N1). The protocol is defined in `common/include/protocol_common.h`, shared between both MCUs.

| Command | ID | Description |
|---------|-----|-------------|
| `CMD_GET_BUFFER_DATA` | `0x01` | Return historical sensor samples from ring buffer |
| `CMD_START_MEASUREMENT` | `0x02` | Start live sensor streaming (periodic notifications) |
| `CMD_STOP_MEASUREMENT` | `0x03` | Stop live streaming |
| `CMD_SET_RTC` | `0x04` | Synchronize RTC from ESP32 (NTP time) |
| `CMD_GET_STATUS` | `0x05` | Return device state, uptime, buffer count |
| `CMD_GET_CONFIG` | `0x07` | Return current configuration |
| `CMD_SET_CONFIG` | `0x08` | Update configuration |
| `CMD_FW_UPDATE_START` | `0x10` | Begin OTA session (size, CRC, version) |
| `CMD_FW_UPDATE_CHUNK` | `0x11` | Receive 252-byte firmware chunk |
| `CMD_FW_UPDATE_END` | `0x12` | Validate and commit (or validate-only) |
| `CMD_FW_UPDATE_ABORT` | `0x13` | Cancel update, return to idle |
| `CMD_FW_UPDATE_STATUS` | `0x14` | Query update progress and state |

---

## Bootloader

The bootloader is a **separate binary** (32KB) that occupies sectors 0-1 of Bank 1. It runs before the application on every boot and handles firmware updates via dual-bank flash.

See [../docs/OTA_DEEP_DIVE.md](../docs/OTA_DEEP_DIVE.md) (section 6) for the full technical deep dive.

### Flash Layout

```
Bank 1 (0x08000000 - 0x080FFFFF, 1MB)
┌──────────────────────────────────────────────┐
│ Sectors 0-1:  Bootloader     (32KB)          │  Write-protected
│               0x08000000 - 0x08007FFF        │
├──────────────────────────────────────────────┤
│ Sectors 2-11: Application    (992KB)         │  Active firmware
│               0x08008000 - 0x080FFFFF        │
└──────────────────────────────────────────────┘

Bank 2 (0x08100000 - 0x081FFFFF, 1MB)
┌──────────────────────────────────────────────┐
│ All sectors:  OTA Staging    (1MB)           │  Written by UART receiver
│               0x08100000 - 0x081FFFFF        │
└──────────────────────────────────────────────┘
```

### Required Option Bytes

| Option Byte | Value | Meaning |
|---|---|---|
| **nDBANK** | `0` | Dual-bank mode enabled (2 x 1MB) |
| **nDBOOT** | `1` | Always boot from Bank 1 |
| **BOOT_ADD0** | `0x2000` | Boot address = `0x08000000` |

The bootloader enforces these automatically on first boot.

### Boot Flow

```
Reset → UART init → Print reset cause → Enable backup domain
  │
  ├── Check option bytes (fix + reset if wrong)
  ├── Write-protect bootloader sectors 0-1
  ├── Check boot attempt counter
  │
  ├── BKP0R == 0xDEADBEEF?  (update pending)
  │     │
  │     YES → Verify Bank 2 CRC → Erase Bank 1 app → Copy → Verify → Clear flags
  │     NO  → Skip
  │
  ├── Validate app vector table (SP in RAM, PC in flash range)
  │     FAIL → Error blink (halt)
  │
  └── Jump to application at 0x08008000
```

### RTC Backup Register Mailbox

The bootloader and application communicate through battery-backed RTC registers:

| Register | Purpose | Magic Value |
|----------|---------|-------------|
| `BKP0R` | Update pending flag | `0xDEADBEEF` |
| `BKP1R` | Firmware size (bytes) | — |
| `BKP2R` | Expected CRC32 | — |
| `BKP3R` | Boot attempt counter | — |
| `BKP4R` | Version (packed `major<<16\|minor<<8\|patch`) | — |
| `BKP5R` | Boot confirmed by app | `0xB007C0DE` |
| `BKP6R` | Update retry counter | — |

### Anti-Brick Mechanisms

- **Verify-before-erase:** CRC32 of Bank 2 is checked *before* Bank 1 is erased. Corrupt staged firmware cannot destroy the running app.
- **Update retries:** If copy/verify fails, the bootloader retries up to 3 times.
- **Boot attempt counter:** Tracks consecutive boots without app confirmation. After 3 failed boots, the bootloader flags the application as potentially faulty.
- **Bootloader write-protection:** Sectors 0-1 are write-protected via option bytes. The application cannot accidentally overwrite the bootloader.

---

## Event Bus

| Event | Published By | Subscribers |
|-------|-------------|-------------|
| `EVENT_TEMPERATURE_UPDATED` | Temperature Sensor | Display, Protocol Handler |
| `EVENT_SENSOR_ERROR` | Temperature Sensor | Display |

---

## Hardware

| Component | Interface | Pins | Purpose |
|-----------|-----------|------|---------|
| AHT25 | I2C2 | PF0 (SCL), PF1 (SDA) | Temperature + humidity |
| INA226 | I2C4 | PD12 (SCL), PD13 (SDA) | Current + voltage |
| ST7735 IPS | SPI1 | PA5 (SCK), PA7 (MOSI), PA4 (CS), PA6 (DC), PB0 (RST) | Display |
| ESP32 UART | USART2 | PA2 (TX), PA3 (RX) | Gateway communication |
| Debug UART | USART3 | PD8 (TX), PD9 (RX) | ST-Link VCP (115200) |
| Status LED | GPIO | PF13 | Heartbeat blinky |

All interfaces operate at 3.3V.

---

## Building

### Application

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build
```

Flash to `0x08008000` using STM32CubeProgrammer or OpenOCD.

### Bootloader

```bash
cmake -B build_bl -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DBUILD_BOOTLOADER=ON
cmake --build build_bl
```

Flash to `0x08000000`. The bootloader uses its own linker script (`Bootloader/bootloader_flash.ld`) limiting it to 32KB.

### Option Bytes (first-time setup)

Using STM32CubeProgrammer, verify:
- **nDBANK = 0** (dual-bank enabled)
- **nDBOOT = 1** (always boot Bank 1)
- **BOOT_ADD0 = 0x2000** (boot from `0x08000000`)

The bootloader will auto-correct these on first boot, but setting them manually avoids a surprise reset.

**Requirements:**
- CMake 3.22+
- ARM GCC Embedded Toolchain (14.x recommended)
- Target: STM32F767ZI (Nucleo-144 board)

---

## HAL Portability

The entire application above the HAL layer is platform-independent. To port to a different MCU:

1. Reimplement `HAL/` — `hal_gpio.c`, `hal_i2c.c`, `hal_spi.c`, `hal_uart.c`, `hal_rtc.c`, `hal_flash.c`, `hal_delay.c`
2. Update `Drivers_BSP/BSP/` — `bsp.c`, `pinout.h`
3. Everything else (Application, Middleware, OS, custom drivers) stays unchanged.
