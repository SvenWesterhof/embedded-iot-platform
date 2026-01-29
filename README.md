# Embedded IoT Platform

A professional-grade embedded IoT platform consisting of an ESP32 WiFi gateway and STM32 sensor node, connected via UART protocol.

## Architecture

Both projects follow a **layered event-driven architecture** with strict downward-only dependencies, ensuring low coupling and high portability.

```
embedded-iot-platform/
├── common/                 # Shared MCU-agnostic code
│   ├── include/            # Shared headers (protocol_common.h)
│   ├── src/                # Shared source files
│   └── esp_component/      # ESP-IDF component wrapper
├── ESP32/                  # ESP32 IoT Gateway
│   ├── Application/        # Application layer
│   ├── Middleware/         # Services, Features, Control
│   ├── OS/                 # Event bus, FreeRTOS wrappers
│   ├── Drivers_BSP/        # Board support package
│   ├── HAL_Wrapper/        # Hardware abstraction layer
│   └── main/               # ESP-IDF entry point
├── STM32/                  # STM32 Sensor Node
│   ├── Application/        # Application layer
│   ├── Middleware/         # Services, Features, Control
│   ├── OS/                 # Event bus, FreeRTOS wrappers
│   ├── Drivers_BSP/        # Board support package
│   ├── HAL/                # Hardware abstraction layer
│   ├── Core/               # STM32CubeMX generated code
│   └── Drivers/            # STM32 HAL drivers
└── README.md               # This file
```

## Layer Descriptions

| Layer | Purpose |
|-------|---------|
| **Application** | System orchestration, initialization, main loop |
| **Middleware** | Services (sensors, display), Features (protocols), Control (state machines) |
| **OS** | Event bus, FreeRTOS task management |
| **Drivers/BSP** | Board-specific code, peripheral configuration |
| **HAL** | Platform-independent hardware abstraction |

## Communication Protocol

The ESP32 and STM32 communicate via UART using a binary protocol:

- **Packet framing**: Start marker (0xAA) + payload + CRC16 + End marker (0x55)
- **Baud rate**: 921600 bps with RTS/CTS flow control
- **Commands**: GET_BUFFER_DATA, START/STOP_MEASUREMENT, SET_RTC, GET_STATUS

Shared protocol definitions are in `common/include/protocol_common.h`.

## Building

### ESP32 (ESP-IDF)

```bash
cd ESP32

# Configure (first time or when changing target)
idf.py set-target esp32s3

# Build
idf.py build

# Flash and monitor
idf.py -p COMx flash monitor
```

**Requirements:**
- ESP-IDF v5.5.x
- Target: ESP32 / ESP32-S3

### STM32 (CMake + GCC ARM)

```bash
cd STM32

# Configure
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake

# Build
cmake --build build

# Flash (using STM32CubeProgrammer or OpenOCD)
```

**Requirements:**
- CMake 3.22+
- GCC ARM Embedded Toolchain
- Target: STM32F767ZI

## Hardware

### ESP32 Gateway
- WiFi connectivity with auto-reconnect
- WebSocket dashboard for real-time monitoring
- MQTT cloud integration
- NTP time synchronization

### STM32 Sensor Node
- ATH25 temperature/humidity sensor (I2C)
- ST7735 IPS display (SPI)
- INA226 current/voltage monitoring (I2C)
- Sensor data buffering with ring buffer

### Wiring

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

> Note: Ensure both devices use 3.3V logic levels or add level shifters.

## Configuration

### ESP32 Credentials

Copy the template and add your credentials:

```bash
cp ESP32/Application/credentials.h.template ESP32/Application/credentials.h
```

Edit `ESP32/Application/credentials.h`:

```c
#define WIFI_SSID           "YourWiFiName"
#define WIFI_PASSWORD       "YourWiFiPassword"
#define MQTT_BROKER_URI     "mqtt://broker.hivemq.com:1883"
```

> Never commit credentials.h - it's in .gitignore

## License

MIT License - See LICENSE file for details
