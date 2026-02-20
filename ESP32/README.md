# ESP32-S3 WiFi Gateway

WiFi gateway firmware for the ESP32-S3, handling cloud connectivity (MQTT over mTLS), real-time browser dashboard (WebSocket), OTA firmware updates for both itself and the STM32, and bidirectional UART communication with the sensor node.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         main/main.c (Entry Point)                   │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Application Layer                                                  │
│    app_main.c         - Init sequence, startup orchestration        │
│    credentials.h      - WiFi, MQTT, TLS certificate paths           │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  OS Layer                                                           │
│    event_bus.c        - Pub/sub event system (16 queued, 8 subs)    │
│    os_wrapper.c       - FreeRTOS task/mutex/semaphore abstraction    │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Middleware Layer                                                    │
│                                                                     │
│  Control/                                                           │
│    cont_wifi_manager    - WiFi STA, auto-reconnect, NVS creds      │
│    cont_ota_manager     - OTA router: MQTT notify → ESP32/STM32    │
│                                                                     │
│  Services/                                                          │
│    serv_mqtt_client     - AWS IoT Core mTLS, pub/sub                │
│    serv_ntp_sync        - SNTP time synchronization                 │
│    serv_esp32_ota       - ESP32 self-OTA (HTTPS streaming)          │
│    serv_stm32_ota       - STM32 OTA (download → verify → UART)     │
│    serv_https_download  - Reusable HTTPS streaming downloader       │
│    serv_signature_verify - RSA-2048 firmware signature verification │
│                                                                     │
│  Features/                                                          │
│    feat_dashboard_server - HTTP + WebSocket server (port 80)        │
│    feat_stm32_protocol   - UART binary protocol handler             │
└──────────────────────────────┬──────────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Drivers Layer                                                      │
│    Drivers_BSP/       - Board-specific peripheral config            │
│    HAL_Wrapper/       - GPIO, SPI, I2C, UART abstraction            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Components

### Control Layer

**`cont_wifi_manager`** — WiFi station mode with automatic reconnection and NVS credential storage.

| Function | Description |
|----------|-------------|
| `cont_wifi_manager_init()` | Initialize WiFi driver and load NVS credentials |
| `cont_wifi_manager_start()` | Connect to configured network |
| `cont_wifi_manager_stop()` | Disconnect and stop |
| `wifi_manager_is_connected()` | Check connection status |

Events: `EVENT_WIFI_CONNECTED`, `EVENT_WIFI_DISCONNECTED`

**`cont_ota_manager`** — Unified OTA router. Subscribes to `gateway/ota/notify` via MQTT, parses the JSON notification, and routes to the correct OTA service based on the `target` field (`esp32` or `stm32`). Reports status back to the cloud via `devices/{id}/ota/status`.

| Function | Description |
|----------|-------------|
| `cont_ota_manager_init()` | Initialize both OTA services |
| `cont_ota_manager_start()` | Subscribe to MQTT OTA topic |
| `cont_ota_validate_after_boot()` | Confirm new firmware after OTA reboot |
| `cont_ota_cancel_update()` | Abort in-progress update |

### Services Layer

**`serv_mqtt_client`** — MQTT client connected to AWS IoT Core over mTLS (port 8883). Device certificate embedded, private key stored in NVS.

| Function | Description |
|----------|-------------|
| `serv_mqtt_init()` | Configure broker, TLS certs, client ID |
| `serv_mqtt_start()` | Connect to broker |
| `serv_mqtt_publish()` | Publish to any topic (QoS 0 or 1) |
| `serv_mqtt_subscribe()` | Subscribe to topic |

**`serv_esp32_ota`** — ESP32 self-OTA via `esp_https_ota`. Streams firmware over HTTPS directly to the inactive OTA partition (ping-pong between ota_0 and ota_1). Supports automatic rollback if new firmware fails.

**`serv_stm32_ota`** — STM32 OTA proxy. Downloads firmware over HTTPS, computes SHA256 incrementally, streams 252-byte chunks to STM32 over UART, verifies RSA-2048 signature, then commits. Only 4KB of RAM used — no full firmware buffer.

**`serv_signature_verify`** — RSA-2048 PKCS#1 v1.5 signature verification using mbedTLS. Public key compiled in from `stm32_public_key.h`.

**`serv_https_download`** — Reusable HTTPS downloader with streaming callback support. Used by both OTA services.

**`serv_ntp_sync`** — SNTP time synchronization for accurate timestamps.

### Features Layer

**`feat_dashboard_server`** — Embedded HTTP server with WebSocket endpoint at `/ws`. Broadcasts JSON status updates to all connected browser clients. Auto-reconnect on the client side.

| Function | Description |
|----------|-------------|
| `feat_dashboard_server_init()` | Initialize HTTP server |
| `feat_dashboard_server_start()` | Start listening on port 80 |
| `dashboard_broadcast_json()` | Push JSON to all WebSocket clients |

**`feat_stm32_protocol`** — UART binary protocol handler for bidirectional communication with the STM32. Supports sensor data requests, live streaming, RTC sync, configuration, and firmware update commands.

---

## Flash Partition Layout

Defined in `partitions_ota.csv` (8MB flash):

```
┌────────────────────────────────────────────────┐
│ Bootloader          (0x00000 - 0x08000,  32KB) │
│ Partition Table     (0x08000 - 0x09000,   4KB) │
├────────────────────────────────────────────────┤
│ NVS                 (0x09000 - 0x0F000,  24KB) │  WiFi creds, device private key
│ OTA Data            (0x0F000 - 0x11000,   8KB) │  Tracks active OTA partition
│ PHY Init            (0x11000 - 0x12000,   4KB) │
├────────────────────────────────────────────────┤
│ Factory             (0x20000 - 0x220000,  2MB) │  Disaster recovery fallback
├────────────────────────────────────────────────┤
│ OTA_0               (0x220000 - 0x420000, 2MB) │  ┐ Ping-pong
│ OTA_1               (0x420000 - 0x620000, 2MB) │  ┘ OTA slots
├────────────────────────────────────────────────┤
│ Free                (0x620000 - 0x800000, ~2MB) │
└────────────────────────────────────────────────┘
```

---

## OTA Updates

The ESP32 handles OTA for both itself and the STM32. See [../docs/OTA_DEEP_DIVE.md](../docs/OTA_DEEP_DIVE.md) for the full technical deep dive.

**ESP32 self-update flow:**
1. MQTT notification received with firmware URL
2. `esp_https_ota` streams download directly to inactive OTA partition
3. Image validated, boot partition switched
4. On reboot: `cont_ota_validate_after_boot()` confirms the new firmware
5. If firmware crashes before confirmation → automatic rollback

**STM32 proxy update flow:**
1. MQTT notification received with firmware URL + RSA signature
2. ESP32 tells STM32 to erase Bank 2, waits for completion
3. HTTPS download streamed through SHA256 hash + UART chunking callback
4. RSA-2048 signature verified over final SHA256 hash
5. `CMD_FW_UPDATE_END` sent → STM32 resets → bootloader applies update

---

## Event Bus

| Event | Published By | Subscribers |
|-------|-------------|-------------|
| `EVENT_WIFI_CONNECTED` | WiFi Manager | MQTT, NTP, Dashboard |
| `EVENT_WIFI_DISCONNECTED` | WiFi Manager | MQTT, Dashboard |
| `EVENT_MQTT_CONNECTED` | MQTT Client | OTA Manager |
| `EVENT_MQTT_DATA_RECEIVED` | MQTT Client | OTA Manager |
| `EVENT_OTA_STARTED` | ESP32 OTA Svc | OTA Manager (→ MQTT status) |
| `EVENT_OTA_PROGRESS` | ESP32 OTA Svc | OTA Manager (→ MQTT progress) |
| `EVENT_OTA_COMPLETED` | ESP32 OTA Svc | OTA Manager (→ MQTT status) |
| `EVENT_OTA_VALIDATED` | ESP32 OTA Svc | OTA Manager (→ MQTT status) |
| `EVENT_STM32_OTA_STARTED` | STM32 OTA Svc | OTA Manager |
| `EVENT_STM32_OTA_COMPLETED` | STM32 OTA Svc | OTA Manager |
| `EVENT_STM32_DATA_READY` | STM32 Protocol | Dashboard |
| `EVENT_NTP_TIME_SYNCED` | NTP Service | — |

---

## FreeRTOS Tasks

| Task | Priority | Stack | Core | Purpose |
|------|----------|-------|------|---------|
| WiFi | 23 | 3584 | 0 | Network stack (ESP-IDF managed) |
| MQTT | 5 | 6144 | any | Broker communication + TLS |
| ESP32 OTA | 5 | 8192 | any | HTTPS download + flash (spawned on demand) |
| STM32 OTA | 5 | 8192 | 1 | HTTPS download + UART streaming (spawned on demand) |
| Dashboard | 5 | 4096 | any | HTTP/WebSocket server |
| STM32 RX | 10 | 2048 | any | UART receive handler |
| Event Bus | 8 | 2048 | any | Event dispatch |
| Main App | 5 | 4096 | any | Status broadcast loop |

---

## Building

```bash
# Set target (first time)
idf.py set-target esp32s3

# Build
idf.py build

# Flash and monitor
idf.py -p COMx flash monitor
```

**Requirements:**
- ESP-IDF v5.5.x
- Target: ESP32-S3 (dual-core, 8MB flash)

## Configuration

```bash
cp Application/credentials.h.template Application/credentials.h
```

```c
#define WIFI_SSID       "YourSSID"
#define WIFI_PASSWORD   "YourPassword"
#define MQTT_BROKER_URI "mqtts://your-endpoint.iot.eu-west-1.amazonaws.com:8883"
#define MQTT_CLIENT_ID  "embedded-iot-gateway"
```

> `credentials.h` is in `.gitignore` — never commit it.

## TLS Certificates

| File | Location | Purpose |
|------|----------|---------|
| `AmazonRootCA1.pem` | `keys/iot/` | AWS IoT Core root CA (embedded in binary) |
| `device-cert.pem` | `keys/iot/` | Device X.509 certificate (embedded in binary) |
| Device private key | NVS partition | Provisioned per-device, never compiled in |
| `stm32_public_key.h` | `Middleware/Services/` | RSA-2048 public key for firmware verification |

## Hardware

| Pin | Function |
|-----|----------|
| GPIO16 | UART TX → STM32 RX |
| GPIO17 | UART RX ← STM32 TX |

UART config: 115200 baud, 8N1, 3.3V logic.
