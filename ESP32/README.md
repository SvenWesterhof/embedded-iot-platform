# ESP32 Gateway

A professional-grade ESP32 firmware implementing a WiFi gateway with STM32 UART communication, WebSocket dashboard, MQTT cloud integration, and NTP time synchronization.

## Features

- **WiFi Connectivity** - Auto-connect with NVS credential storage
- **WebSocket Dashboard** - Real-time browser-based monitoring
- **MQTT Client** - Publish sensor data to cloud brokers
- **NTP Time Sync** - Accurate timestamps for data logging
- **STM32 Protocol** - UART communication with external MCU
- **Event Bus** - Decoupled component communication

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         main/main.c                              │
│                      (Entry Point)                               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Application Layer                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐  │
│  │   app_main.c    │  │  credentials.h  │  │    config.h     │  │
│  │  (Init & Loop)  │  │  (WiFi/MQTT)    │  │  (Constants)    │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                         OS Layer                                 │
│  ┌─────────────────┐  ┌─────────────────┐                       │
│  │   event_bus.c   │  │   os_tasks.c    │                       │
│  │ (Pub/Sub Events)│  │ (Task Manager)  │                       │
│  └─────────────────┘  └─────────────────┘                       │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Middleware Layer                            │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ Control/                                                     ││
│  │   cont_wifi_manager.c  - WiFi STA connection management     ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │ Services/                                                    ││
│  │   serv_ntp_sync.c      - NTP time synchronization           ││
│  │   serv_mqtt_client.c   - MQTT broker connectivity           ││
│  ├─────────────────────────────────────────────────────────────┤│
│  │ Features/                                                    ││
│  │   feat_dashboard_server.c - WebSocket dashboard             ││
│  │   feat_stm32_protocol.c   - UART protocol handler           ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Drivers Layer                               │
│  ┌─────────────────┐  ┌─────────────────┐                       │
│  │    Drivers_BSP/ │  │   HAL_Wrapper/  │                       │
│  │   (Board Deps)  │  │ (GPIO,SPI,I2C)  │                       │
│  └─────────────────┘  └─────────────────┘                       │
└─────────────────────────────────────────────────────────────────┘
```

### Layer Descriptions

| Layer | Purpose | Prefix |
|-------|---------|--------|
| **Application** | Init sequence, main loop, credentials | `app_` |
| **OS** | FreeRTOS tasks, event bus pub/sub | `event_`, `os_` |
| **Control** | State machines, orchestration | `cont_` |
| **Services** | Background infrastructure | `serv_` |
| **Features** | User-facing functionality | `feat_` |
| **Drivers** | Hardware abstraction | `hal_`, `bsp_` |

---

## Component Reference

### Control Layer

#### `cont_wifi_manager` - WiFi Connection Manager
Manages WiFi STA mode with automatic reconnection and NVS credential storage.

| Function | Description |
|----------|-------------|
| `cont_wifi_manager_init()` | Initialize WiFi driver |
| `cont_wifi_manager_start()` | Connect to configured network |
| `cont_wifi_manager_stop()` | Disconnect and stop |
| `wifi_manager_set_credentials()` | Set SSID/password |
| `wifi_manager_is_connected()` | Check connection status |

**Events Published:** `EVENT_WIFI_CONNECTED`, `EVENT_WIFI_DISCONNECTED`

---

### Services Layer

#### `serv_ntp_sync` - NTP Time Synchronization
Background service for accurate time via SNTP protocol.

| Function | Description |
|----------|-------------|
| `serv_ntp_init()` | Initialize SNTP client |
| `serv_ntp_start()` | Begin synchronization |
| `serv_ntp_get_time()` | Get Unix timestamp |
| `serv_ntp_is_valid()` | Check if time is synced |
| `serv_ntp_set_timezone()` | Set UTC offset |

**Default Server:** `pool.ntp.org`

---

#### `serv_mqtt_client` - MQTT Cloud Client
Publishes sensor data and receives commands via MQTT.

| Function | Description |
|----------|-------------|
| `serv_mqtt_init()` | Configure broker connection |
| `serv_mqtt_start()` | Connect to broker |
| `serv_mqtt_publish()` | Publish raw data |
| `serv_mqtt_publish_sensor()` | Publish JSON sensor data |
| `serv_mqtt_subscribe()` | Subscribe to command topics |
| `serv_mqtt_is_connected()` | Check broker connection |

**Default Broker:** `mqtt://broker.hivemq.com:1883` (public, no auth)

---

### Features Layer

#### `feat_dashboard_server` - WebSocket Dashboard
Embedded web server with real-time WebSocket updates.

| Function | Description |
|----------|-------------|
| `feat_dashboard_server_init()` | Initialize HTTP server |
| `feat_dashboard_server_start()` | Start listening |
| `dashboard_broadcast_json()` | Send JSON to all clients |
| `dashboard_get_client_count()` | Get connected clients |

**Port:** 80 (HTTP) with WebSocket at `/ws`

---

#### `feat_stm32_protocol` - STM32 UART Communication
Binary protocol for bidirectional MCU communication.

| Function | Description |
|----------|-------------|
| `feat_stm32_protocol_init()` | Initialize UART |
| `feat_stm32_protocol_start()` | Start RX task |
| `feat_stm32_protocol_send()` | Send command to STM32 |

**UART Config:** 115200 baud, 8N1, GPIO16 (TX), GPIO17 (RX)

---

## Hardware Wiring

### ESP32 Pinout

| ESP32 Pin | Function | Connect To |
|-----------|----------|------------|
| GPIO16 | UART TX | STM32 RX |
| GPIO17 | UART RX | STM32 TX |
| GND | Ground | STM32 GND |

### STM32 Connection Diagram

```
   ESP32                    STM32
┌─────────┐              ┌─────────┐
│         │              │         │
│   TX ───┼──────────────┼─── RX   │
│   RX ───┼──────────────┼─── TX   │
│  GND ───┼──────────────┼─── GND  │
│         │              │         │
└─────────┘              └─────────┘
```

> **Note:** Ensure both devices use 3.3V logic levels or add level shifters.

---

## Configuration

### 1. Set Credentials

Copy the template and fill in your values:

```bash
cp Application/credentials.h.template Application/credentials.h
```

Edit `Application/credentials.h`:

```c
#define WIFI_SSID           "YourWiFiName"
#define WIFI_PASSWORD       "YourWiFiPassword"

#define MQTT_BROKER_URI     "mqtt://broker.hivemq.com:1883"
#define MQTT_USERNAME       NULL    // or "your_username"
#define MQTT_PASSWORD       NULL    // or "your_password"
#define MQTT_TOPIC_PREFIX   "esp32_gateway"

#define NTP_TIMEZONE_OFFSET 1       // UTC+1 for CET
```

> **Never commit credentials.h** - it's in `.gitignore`

### 2. Build & Flash

```bash
idf.py build
idf.py -p COM6 flash monitor
```

---

## Dashboard Access

1. **Boot the ESP32** and wait for WiFi connection
2. **Find IP address** in serial monitor:
   ```
   Got IP: 192.168.1.100
   ```
3. **Open browser** to: `http://192.168.1.100`

### Dashboard Features

| Card | Description |
|------|-------------|
| UPTIME | Seconds since boot |
| WIFI | Connection status |
| MQTT | Broker connection |
| NTP | Time sync status |
| STM32 DATA | Latest received data |
| CLIENTS | Connected browsers |

The dashboard auto-reconnects and updates every 2 seconds via WebSocket.

---

## MQTT Usage

### Topic Structure

```
<prefix>/sensors/<type>     - Sensor data (published by ESP32)
<prefix>/status             - Online/offline status (retained)
<prefix>/data/stm32         - Raw STM32 data
<prefix>/cmd/#              - Commands (subscribed by ESP32)
```

Default prefix: `esp32_gateway`

### Subscribe to Data

Using `mosquitto_sub`:

```bash
# All sensor data
mosquitto_sub -h broker.hivemq.com -t "esp32_gateway/sensors/#"

# STM32 data
mosquitto_sub -h broker.hivemq.com -t "esp32_gateway/data/stm32"

# Status
mosquitto_sub -h broker.hivemq.com -t "esp32_gateway/status"
```

### Publish Commands

```bash
# Send command to ESP32
mosquitto_pub -h broker.hivemq.com -t "esp32_gateway/cmd/test" -m "hello"
```

### Sensor Data Format

Published as JSON:

```json
{
  "type": "temperature",
  "value": 25.5,
  "unit": "°C",
  "timestamp": 1736784000
}
```

---

## Event Bus

Components communicate via a publish/subscribe event bus:

| Event | Published By | Description |
|-------|--------------|-------------|
| `EVENT_WIFI_CONNECTED` | WiFi Manager | WiFi connection established |
| `EVENT_WIFI_DISCONNECTED` | WiFi Manager | WiFi connection lost |
| `EVENT_MQTT_CONNECTED` | MQTT Client | Broker connected |
| `EVENT_MQTT_DISCONNECTED` | MQTT Client | Broker disconnected |
| `EVENT_NTP_TIME_SYNCED` | NTP Service | Time synchronized |
| `EVENT_STM32_DATA_READY` | STM32 Protocol | Data received from STM32 |
| `EVENT_DASHBOARD_CONNECTED` | Dashboard | First client connected |

### Usage Example

```c
// Subscribe to event
event_bus_subscribe(EVENT_WIFI_CONNECTED, my_handler);

// Publish event
event_bus_publish(EVENT_STM32_DATA_READY, &sensor_data);
```

---

## Build Requirements

- **ESP-IDF:** v5.5.x
- **Target:** ESP32 / ESP32-S3
- **Flash:** 4MB minimum
- **Required Components:** esp_wifi, mqtt, esp_http_server, nvs_flash

---

## FreeRTOS Configuration

This project uses FreeRTOS (integrated in ESP-IDF) for multitasking and real-time operation.

### Dual-Core Architecture

The ESP32-S3 has **two Xtensa LX7 cores** running at 240 MHz:

| Core | Name | Default Usage |
|------|------|---------------|
| **Core 0** | PRO_CPU | WiFi/BT protocol stack, system tasks |
| **Core 1** | APP_CPU | Application tasks (FreeRTOS default) |

Tasks can be pinned to a specific core or allowed to run on any:

```c
// Pin to Core 1 (application core)
xTaskCreatePinnedToCore(my_task, "task", 4096, NULL, 5, NULL, 1);

// Run on any core (load balanced)
xTaskCreatePinnedToCore(my_task, "task", 4096, NULL, 5, NULL, tskNO_AFFINITY);
```

> **Best Practice:** Keep WiFi-critical code on Core 0, computation-heavy tasks on Core 1.

### Task Overview

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| Main App | 5 | 4096 | Status broadcast loop |
| WiFi | 23 | 3584 | Network stack (ESP-IDF) |
| MQTT | 5 | 6144 | Broker communication |
| Dashboard | 5 | 4096 | HTTP/WebSocket server |
| STM32 RX | 10 | 2048 | UART receive handler |
| Event Bus | 8 | 2048 | Event dispatch |

### Synchronization Primitives

| Type | Used By | Purpose |
|------|---------|---------|
| **Mutex** | MQTT, Dashboard | Protect shared client lists |
| **Semaphore** | STM32 Protocol | Signal data ready |
| **Queue** | Event Bus | Async event delivery |
| **Task Notifications** | NTP | Sync completion signal |

### Configuration (os_config.h)

```c
#define OS_TASK_STACK_SIZE_DEFAULT  4096
#define OS_TASK_PRIORITY_LOW        3
#define OS_TASK_PRIORITY_NORMAL     5
#define OS_TASK_PRIORITY_HIGH       10
#define OS_TASK_PRIORITY_CRITICAL   15

#define EVENT_BUS_QUEUE_SIZE        16
#define EVENT_BUS_MAX_SUBSCRIBERS   8
```

### Creating New Tasks

```c
#include "os_tasks.h"

void my_task(void *pvParameters) {
    while (1) {
        // Task work
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Create task
os_task_create(my_task, "my_task", 
               OS_TASK_STACK_SIZE_DEFAULT,
               NULL, 
               OS_TASK_PRIORITY_NORMAL);
```

### Memory Considerations

- **Heap:** ~300KB available (ESP32-S3)
- **Stack Overflow Protection:** Enabled via `CONFIG_FREERTOS_CHECK_STACKOVERFLOW`
- **Idle Task:** Handles WiFi/TCP background work

> WebSocket + MQTT + TLS requires significant stack. Monitor with `uxTaskGetStackHighWaterMark()`.

---

## License

MIT License - See LICENSE file for details