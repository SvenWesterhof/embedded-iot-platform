# Embedded IoT Platform

A dual-MCU IoT platform with an **ESP32-S3 WiFi gateway** and **STM32F767 sensor node**, featuring secure over-the-air firmware updates for both targets, real-time sensor streaming, and a cloud-connected dashboard.

```
    ┌─────────────┐       MQTTS / HTTPS       ┌──────────────┐
    │  AWS Cloud   │◄────────────────────────►│  ESP32-S3    │
    │  IoT Core    │   mTLS + pre-signed URLs  │  Gateway     │
    │  S3 Storage  │                           │              │
    └─────────────┘                            │  WiFi + MQTT │
                                               │  WebSocket   │
          ┌───────────────┐                    │  OTA Manager │
          │  Browser      │◄──── WebSocket ───►│              │
          │  Dashboard    │     (port 80)      └──────┬───────┘
          └───────────────┘                           │
                                                      │ UART 115200 8N1
                                                      │ Binary protocol
                                               ┌──────┴───────┐
                                               │  STM32F767   │
                                               │  Sensor Node │
                                               │              │
                                               │  Temperature │
                                               │  Current/V   │
                                               │  Display     │
                                               └──────────────┘
```

## Key Features

- **Dual-target OTA** — ESP32 self-updates via HTTPS streaming to dual partitions; STM32 updates streamed over UART with a custom dual-bank bootloader
- **RSA-signed firmware** — RSA-2048 (STM32) and RSA-3072 (ESP32) signature verification before any flash operation
- **Real-time sensor dashboard** — WebSocket server on ESP32 pushes live sensor data to browser clients
- **MQTT cloud telemetry** — Sensor data and device status published to AWS IoT Core over mTLS
- **Layered architecture** — Strict layer separation with event-driven communication on both MCUs

## Repository Structure

```
embedded-iot-platform/
├── ESP32/                      # ESP32-S3 WiFi Gateway
│   ├── Application/            #   App init, main loop, credentials
│   ├── Middleware/
│   │   ├── Control/            #   cont_wifi_manager, cont_ota_manager
│   │   ├── Services/           #   serv_mqtt_client, serv_*_ota, serv_ntp_sync
│   │   └── Features/           #   feat_dashboard_server, feat_stm32_protocol
│   ├── OS/                     #   Event bus, FreeRTOS task wrappers
│   ├── Drivers_BSP/            #   Board support package
│   └── keys/iot/               #   TLS certificates (not private keys)
│
├── STM32/                      # STM32F767 Sensor Node
│   ├── Application/            #   App init, main loop
│   ├── Middleware/
│   │   ├── Control/            #   State machines
│   │   ├── Services/           #   serv_firmware_update, sensor services
│   │   └── Features/           #   feat_stm32_protocol (UART responder)
│   ├── OS/                     #   Event bus, FreeRTOS task wrappers
│   ├── HAL/                    #   Hardware abstraction layer
│   ├── Bootloader/             #   Dual-bank OTA bootloader (32KB)
│   └── Core/                   #   STM32CubeMX generated code
│
├── common/                     # Shared code (both MCUs)
│   ├── include/                #   protocol_common.h (UART packet definitions)
│   └── src/                    #   Shared utilities
│
├── tools/                      # Build & deployment scripts
│   ├── upload_firmware.py      #   Sign + upload firmware to S3
│   ├── provision_device.py     #   Per-device TLS certificate provisioning
│   ├── generate_rsa_keys.*     #   RSA keypair generation (sh + bat)
│   └── firmware_manifest.json  #   Deployed firmware version tracking
│
├── docs/                       # Documentation
│   ├── OTA_DEEP_DIVE.md        #   Technical deep dive on the OTA system
│   └── ci-setup.md             #   CI/CD pipeline setup guide
│
└── .github/workflows/          # CI/CD
    ├── esp32-ci.yml            #   ESP32 build + static analysis
    ├── stm32-ci.yml            #   STM32 app + bootloader build
    ├── unit-tests.yml          #   Unity/CMock tests (host x86-64)
    └── release.yml             #   Build → sign → S3 → MQTT notify
```

## Architecture

Both MCUs follow the same layered pattern with strict downward-only direct calls and an event bus for horizontal/upward communication:

```
Application (app_*)
       │ direct calls ↓
OS (event_bus, os_task_*)
       │ direct calls ↓
Middleware
├── Control   (cont_*)  ─── State machines, orchestration
├── Services  (serv_*)  ─── Background infrastructure
└── Features  (feat_*)  ─── User-facing functionality
       │ direct calls ↓
Drivers / HAL (hal_*, bsp_*)
```

| Layer | Prefix | Purpose |
|-------|--------|---------|
| Application | `app_` | System init, main loop |
| Control | `cont_` | State machines, orchestration (WiFi manager, OTA manager) |
| Services | `serv_` | Background infrastructure (MQTT, NTP, OTA, sensors) |
| Features | `feat_` | User-facing (WebSocket dashboard, UART protocol) |
| OS | `event_*`, `os_*` | Event bus pub/sub, FreeRTOS wrappers |
| Drivers | `hal_*`, `bsp_*` | Hardware abstraction, board support |

## UART Protocol

The ESP32 and STM32 communicate over UART (115200 baud, 8N1) using a binary protocol defined in `common/include/protocol_common.h`:

```
┌──────┬──────┬──────┬──────┬────────┬──────┬─────────────────┬──────┐
│ 0xAA │ TYPE │ CMD  │ SEQ  │ STATUS │ LEN  │    PAYLOAD      │ 0x55 │
│  1B  │  1B  │  1B  │  1B  │   1B   │  2B  │   0-256 bytes   │  1B  │
└──────┴──────┴──────┴──────┴────────┴──────┴─────────────────┴──────┘
```

**Commands:** sensor data requests, live streaming control, RTC sync, configuration, and firmware update (start/chunk/end/abort/status).

## OTA Updates

Both targets support secure over-the-air firmware updates. See [docs/OTA_DEEP_DIVE.md](docs/OTA_DEEP_DIVE.md) for the full technical deep dive.

**TL;DR:**
1. CI/CD builds, signs (RSA), and uploads firmware to AWS S3
2. MQTT notification tells the device a new firmware is available
3. ESP32 downloads over HTTPS and either flashes itself (dual OTA partitions) or streams to STM32 over UART
4. STM32 bootloader copies verified firmware from Bank 2 → Bank 1 on next boot
5. Automatic rollback on both platforms if the new firmware fails to boot

**Security layers:** mTLS for MQTT, TLS for HTTPS, RSA digital signatures, SHA256 hashing, CRC32 integrity checks at every stage, verify-before-erase, bootloader write-protection.

## Building

### ESP32 (ESP-IDF v5.5.x)

```bash
cd ESP32
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

### STM32 Application (CMake + ARM GCC)

```bash
cd STM32
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
cmake --build build
```

### STM32 Bootloader

```bash
cd STM32
cmake -B build_bl -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DBUILD_BOOTLOADER=ON
cmake --build build_bl
```

Flash the bootloader to `0x08000000` and the application to `0x08008000`.

## Device Provisioning

Each device needs a unique TLS certificate for AWS IoT Core:

```bash
cd tools
pip install -r requirements.txt
python provision_device.py --device-id my-gateway-001
```

This generates and registers the device certificate with AWS IoT Core and stores the private key in the ESP32's NVS partition.

## Configuration

Copy the credentials template and fill in your values:

```bash
cp ESP32/Application/credentials.h.template ESP32/Application/credentials.h
```

```c
#define WIFI_SSID       "YourWiFiSSID"
#define WIFI_PASSWORD   "YourWiFiPassword"
#define MQTT_BROKER_URI "mqtts://your-iot-endpoint.iot.region.amazonaws.com:8883"
```

> `credentials.h` is in `.gitignore` — never commit it.

## Hardware

| Component | MCU | Interface | Purpose |
|-----------|-----|-----------|---------|
| ESP32-S3 DevKit | — | — | WiFi gateway, OTA coordinator |
| STM32F767ZI Nucleo | — | — | Sensor acquisition, display |
| AHT25 | STM32 | I2C | Temperature / humidity |
| INA226 | STM32 | I2C | Current / voltage monitoring |
| ST7735 IPS | STM32 | SPI | Local display |

**Wiring (UART):**

| ESP32 | STM32 | Signal |
|-------|-------|--------|
| GPIO16 | RX (PA3) | ESP32 TX → STM32 RX |
| GPIO17 | TX (PA2) | STM32 TX → ESP32 RX |
| GND | GND | Common ground |

Both MCUs operate at 3.3V logic — no level shifters needed.

## CI/CD

| Workflow | Trigger | What it does |
|----------|---------|--------------|
| `esp32-ci.yml` | Push/PR (ESP32 or common changes) | Build + clang-tidy + cppcheck |
| `stm32-ci.yml` | Push/PR (STM32 or common changes) | Build app + bootloader |
| `unit-tests.yml` | Push/PR (common changes) | Unity/CMock tests on x86-64 host |
| `release.yml` | Version tag (`v*.*.*`) | Build → sign → upload to S3 → MQTT notify |

See [docs/ci-setup.md](docs/ci-setup.md) for GitHub Secrets configuration and pipeline setup.

## Documentation

- [OTA Technical Deep Dive](docs/OTA_DEEP_DIVE.md) — Full analysis of the dual-target OTA system, security model, and attack vector analysis
- [CI/CD Setup Guide](docs/ci-setup.md) — GitHub Actions configuration, secrets, and deployment pipeline
- [RSA Signature Setup](tools/RSA_SIGNATURE_SETUP.md) — Key generation and firmware signing guide
