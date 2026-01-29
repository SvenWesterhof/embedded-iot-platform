# STM32 Embedded Project - Professionele Architectuur

Een productie-grade embedded firmware project dat industry best practices demonstreert voor de STM32F767 microcontroller met volledige architecturele scheiding, event-driven ontwerp en complete portabiliteit.

---

## Projectoverzicht

Dit project implementeert een geavanceerd temperatuurmonitoringsysteem met ESP32 communicatie, displayuitvoer en LED-besturing, waarbij een **layered event-driven architecture** wordt getoond.

### Functies
- **Real-time temperatuur- en vochtigheidsmeting** (ATH25 sensor via I2C)
- **IPS display uitvoer** (ST7735 via SPI met live sensor data)
- **LED-knipperbediening** (visuele feedback)
- **Stroom- en spanningsmonitoring** (INA226 via I2C) - *geïmplementeerd maar momenteel niet actief*
- **ESP32 UART communicatie protocol** met packet framing, CRC16 validatie en streaming support
- **Event-driven communicatie** via publisher-subscriber patroon
- **Volledige hardware-abstractie** voor portabiliteit
- **Sensor data buffering** met ring buffer voor historische data opslag
- **FreeRTOS integratie** voor multitasking

---

## Architectuur

Het project volgt een **layered event-driven architecture** met strikte afhankelijkheden die alleen naar beneden gelden, wat zorgt voor lage koppeling en hoge portabiliteit.

![Architectuur Diagram](docs/images/LayeredEventDrivenArchitecture_light.svg)

### Laagbeschrijving

**Application Layer (Applicatielaag)**
- Hoogwaardige orkestratie van het systeem
- Initialisatie van subsystemen via `app_init()`
- Hoofdbesturingslus `app_run()` met 10ms cyclustime
- FreeRTOS task management

**Middleware Layer (Middlewarelaag)**
- **Services**: Bieden specifieke mogelijkheden
  - `serv_blinky` - LED toggle om de 2 seconden
  - `serv_temperature_sensor` - ATH25 sensor uitlezen (1s interval), publiceert events
  - `serv_display` - ST7735 display management, event-driven updates
  - `serv_current_monitor` - INA226 monitoring (geïmplementeerd, momenteel gedeactiveerd)

- **Features**: Complexe functionaliteit en protocollen (**ACTIEF GEÏMPLEMENTEERD**)
  - `protocol_handler` - ESP32-STM32 communicatie manager
  - `esp32_packet_framing` - UART packet framing met CRC16 validatie
  - Protocol ondersteunt: command-response, streaming data, RTC synchronisatie

- **Control**: Bedrijfslogica en besluitvorming (momenteel voorbereid voor toekomstig gebruik)

**Event Bus (OS Layer)**
- Publisher-subscriber patroon voor ontkoppelde communicatie
- Asynchrone event queue (16 events diep, max 64 bytes per event)
- Max 5 subscribers per event type
- Processed in main loop via `event_bus_process()`

**Drivers & BSP Layer**
- **BSP (Board Support Package)**:
  - Board-specifieke initialisatie
  - LED-besturing via HAL abstractie
  - Centrale toegang tot peripherals (I2C, UART handles)
  - Pinout centralisatie in `pinout.h`

- **Custom Drivers**: Device-specifieke drivers
  - `ath25` - Temperatuur/vochtigheid sensor driver (I2C)
  - `ina226` - Stroom/spanning sensor driver (I2C)
  - `ips_display` - IPS display driver (SPI) ⚠️ *Bevat enkele directe HAL calls*

- **External**: Third-party drivers
  - `ST7735` - Display controller driver

**Hardware Abstraction Layer (HAL)**
- Platform-onafhankelijke interface
- Abstracties voor: GPIO, I2C, SPI, UART, RTC, Delay
- Wikkelt vendor HAL (STM32 HAL)
- Maakt code portabel naar andere MCU's

**Hardware**
- STM32F767ZI microcontroller
- Peripherals: GPIO, I2C2, I2C4, SPI1, USART2, RTC

---

## ESP32 Communication Protocol (Belangrijke Feature!)

Een van de kernfunctionaliteiten van dit project is de **robuuste UART-communicatie** met ESP32 voor data export en controle.

### Protocol Stack Architectuur

```
┌─────────────────────────────────────────────┐
│     protocol_handler.c                      │
│  - Command routing & handling               │
│  - Streaming state machine                  │
│  - Event subscriptions                      │
└──────────────┬──────────────────────────────┘
               │
┌──────────────▼──────────────────────────────┐
│     esp32_packet_framing.c                  │
│  - Packet assembly (0xAA...0x55)            │
│  - CRC16 validation                         │
│  - DMA transmission support                 │
│  - Event-driven reception callbacks         │
└──────────────┬──────────────────────────────┘
               │
┌──────────────▼──────────────────────────────┐
│     HAL UART (hal_uart.c)                   │
│  - Hardware abstraction                     │
│  - 921600 baud @ 8N1, RTS/CTS               │
└─────────────────────────────────────────────┘
```

### Ondersteunde Commands

| Command ID | Naam | Functie | Response |
|-----------|------|---------|----------|
| `0x01` | GET_BUFFER_DATA | Historische sensor data ophalen | Array van samples |
| `0x02` | START_MEASUREMENT | Live meting starten (streaming) | ACK + periodic notifications |
| `0x03` | STOP_MEASUREMENT | Streaming stoppen | ACK |
| `0x04` | SET_RTC | Real-Time Clock instellen | ACK |
| `0x05` | GET_STATUS | Systeem status ophalen | Status packet |
| `0x06` | CLEAR_BUFFER | Sensor buffer wissen | ACK |
| `0x80` | NOTIFY_SENSOR_DATA | Live sensor data notificatie | N/A (notification) |

### Packet Framing

```
┌────────┬─────────┬────────┬──────────┬────────┬─────────┐
│ START  │ CMD_ID  │ LENGTH │ PAYLOAD  │  CRC16 │  END    │
│  0xAA  │ 1 byte  │ 2 bytes│ N bytes  │ 2 bytes│  0x55   │
└────────┴─────────┴────────┴──────────┴────────┴─────────┘
```

### Streaming State Machine

Wanneer `CMD_START_MEASUREMENT` wordt ontvangen:
1. Protocol handler start een dedicated FreeRTOS task
2. Task leest sensor data op gespecificeerd interval (bijv. 10ms voor live measurement)
3. Data wordt gestuurd als `NOTIFY_SENSOR_DATA` packets
4. Bij `CMD_STOP_MEASUREMENT` wordt task netjes gestopt

**Event Integration**: Protocol handler subscribes op `EVENT_TEMPERATURE_UPDATED` en slaat data op voor GET_BUFFER_DATA requests.

---

## Belangrijkste Sterke Punten

### 1. **Volledige Scheiding van Verantwoordelijkheden (Separation of Concerns)**

Elke laag heeft één goed gedefinieerde verantwoordelijkheid:

- **Application**: Systeeminitialisatie en orkestratie van de hoofdlus
- **Services**: Bieden specifieke mogelijkheden (sensor uitlezen, displaybeheer, LED-besturing)
- **Features**: Complexe protocollen en functionaliteit (ESP32 communicatie)
- **Control**: Bedrijfslogica en besluitvorming (voorbereid)
- **Event Bus**: Ontkoppelde communicatie tussen componenten
- **BSP**: Board-specifieke hardwareconfiguratie
- **Custom Drivers**: Device-specifieke drivers (sensor, display)
- **HAL**: Platformabstractie

### 2. **Volledige Portabiliteit** ✅ **100% BEREIKT**

De architectuur is ontworpen voor **complete platformonafhankelijkheid** en dit is nu volledig gerealiseerd:

#### ✅ **100% Draagbare Lagen**
- **Application** - Geen hardware-afhankelijkheden
- **Middleware (Services & Features)** - Gebruikt alleen BSP en HAL abstracties
- **Event Bus** - Platform-agnostisch eventsysteem
- **Custom Drivers** - Generieke I2C/SPI device drivers ✅ **Volledig via HAL abstractie**

#### ⚠️ **Platform-Specifieke Lagen** (Zoals Ontworpen)
- **BSP** - Board-specifiek (pinout, peripheralconfiguratie)
- **HAL** - Platform-specifieke abstractie-implementatie

**Om te porteren naar een ander MCU/platform:**
1. Herimplementeer HAL-laag ([hal_gpio.c](HAL/hal_gpio.c), [hal_i2c.c](HAL/hal_i2c.c), [hal_uart.c](HAL/hal_uart.c), etc.)
2. Update BSP-laag ([pinout.h](Drivers_BSP/BSP/pinout.h), [bsp.c](Drivers_BSP/BSP/bsp.c))
3. **Alle andere lagen blijven ongewijzigd!**

### 3. **Event-Driven Architectuur**

Implementeert **publisher-subscriber patroon** voor lage koppeling:

```c
// Services publiceren events (geen kennis van consumenten)
event_bus_publish(EVENT_TEMPERATURE_UPDATED, &data, sizeof(data));

// Andere services abonneren zich en reageren
event_bus_subscribe(EVENT_TEMPERATURE_UPDATED, on_temperature_handler);
```

**Concrete Event Flow**:
```
serv_temperature_sensor (elke 1s)
  └─► event_bus_publish(EVENT_TEMPERATURE_UPDATED)
       │
       ├─► serv_display: on_temperature_updated()
       │   └─► Update ST7735 display met nieuwe waarden
       │
       └─► protocol_handler: temperature_event_handler()
           └─► Store data voor buffer requests
```

**Voordelen:**
- Services weten niets van elkaar (zero coupling)
- Gemakkelijk nieuwe subscribers toe te voegen (logging, netwerk, opslag)
- Testbaar in isolatie
- Asynchrone processing via event queue

**Event Queue Specificaties**:
- Max queue size: 16 events
- Max event payload: 64 bytes
- Max subscribers per event: 5
- Processing: Via `event_bus_process()` in main loop (10ms interval)

### 4. **Geen Laagschendingen** *(met één uitzondering)*

Strikte naleving van **alleen neerwaartse afhankelijkheden**:

```
Application → Middleware
Middleware → Event Bus / BSP
BSP → HAL
HAL → Hardware
```

✅ **Correct geïmplementeerd**:
- Application roept alleen Services en Event Bus aan
- Services gebruiken alleen BSP en HAL abstracties
- BSP gebruikt alleen HAL functies
- Custom drivers (ath25.c, ina226.c) gebruiken consequent HAL

### 5. **Modulair & Testbaar**

- Elke service is onafhankelijk
- Mock HAL-laag mogelijk voor unit testing
- Duidelijke interfaces tussen lagen
- Gemakkelijk functies toe te voegen/verwijderen
- Debug service beschikbaar: `serv_uart_test` voor logic analyzer verificatie

**Service Lifecycle**:
```c
// services.c
void services_init(void) {
    blinky_init();
    temperature_sensor_init();
    display_init();
    current_monitor_init();
    protocol_handler_init();
#ifdef ENABLE_UART_TEST
    serv_uart_test_init();
#endif
}

void services_run(void) {
    blinky_run();
    temperature_sensor_run();
    display_run();
    // current_monitor_process(); // NOTE: Currently commented out
#ifdef ENABLE_UART_TEST
    serv_uart_test_loop();
#endif
}
```

---

## Directory Structure (Werkelijke Implementatie)

```
stm32_development/
├── Application/              # Application layer
│   ├── app_main.c           # Main application logic & initialization
│   ├── app_main.h
│   └── state_machine.c      # State machine implementation
│
├── Middleware/              # Middleware layer
│   ├── Services/            # Service implementations
│   │   ├── services.c       # Service registry & lifecycle management
│   │   ├── serv_blinky.*    # LED control service (2s toggle)
│   │   ├── serv_temperature_sensor.*  # ATH25 sensor service
│   │   ├── serv_display.*   # ST7735 display service
│   │   ├── serv_current_monitor.*     # INA226 monitoring (inactive)
│   │   └── service_events.h # Event data structures & definitions
│   │
│   ├── Features/            # Complex features & protocols
│   │   ├── protocol_handler.c/h       # ESP32-STM32 protocol manager
│   │   ├── esp32_packet_framing.c/h   # UART packet framing (CRC16)
│   │   └── protocol_common.h          # Shared protocol definitions
│   │
│   └── Control/             # Control/business logic
│       └── control.h        # Prepared for future use (TODO's)
│
├── OS/                      # Event bus & OS utilities
│   ├── event_bus.*          # Publish-subscribe event system
│   ├── os_wrapper.*         # FreeRTOS abstraction layer
│   └── os_tasks.*           # RTOS task definitions
│
├── Drivers_BSP/            # Board Support Package & Drivers
│   ├── BSP/                # Board-specific code
│   │   ├── bsp.*           # Board initialization & peripheral access
│   │   └── pinout.h        # Centralized pin definitions
│   │
│   ├── Custom/             # Custom device drivers
│   │   ├── ath25.*         # Temperature/humidity sensor driver (I2C)
│   │   ├── ina226.*        # Current/voltage sensor driver (I2C)
│   │   └── ips_display.*   # IPS display driver (SPI) 
│   │
│   └── External/           # Third-party drivers
│       └── ST7735/         # ST7735 display controller driver
│
├── HAL/                    # Hardware Abstraction Layer
│   ├── hal_gpio.*          # GPIO abstraction
│   ├── hal_i2c.*           # I2C abstraction
│   ├── hal_spi.*           # SPI abstraction
│   ├── hal_uart.*          # UART abstraction (complex, DMA support)
│   ├── hal_rtc.*           # RTC abstraction
│   └── hal_delay.*         # Timing abstraction
│
├── Utils/                  # Utility modules
│   └── sensor_ring_buffer.* # Ring buffer for sensor data history
│
├── Tests/                  # Test services
│   └── uart_test/
│       └── serv_uart_test.* # UART logic analyzer test (115200 baud)
│
├── Core/                   # STM32 CubeMX generated code
│   ├── Inc/                # System headers
│   └── Src/                # System sources (main.c, syscalls, etc.)
│
├── Drivers/                # STM32 HAL drivers (vendor)
│   ├── CMSIS/              # CMSIS core & device files
│   └── STM32F7xx_HAL_Driver/ # STM32F7 HAL library
│
├── cmake/                  # Build system
│   └── gcc-arm-none-eabi.cmake
│
├── CMakeLists.txt          # Root CMake configuration
├── STM32F767XX_FLASH.ld    # Linker script
└── README.md               # This file
```

---

## Service & Event Flow Diagram

```
┌────────────────────────────────────────────────────────────────────┐
│                        Application Layer                            │
│  app_main.c: app_init() → services_init() → app_run() loop (10ms) │
└────────────────┬───────────────────────────────────────────────────┘
                 │
┌────────────────▼───────────────────────────────────────────────────┐
│                     Middleware Services                             │
├─────────────────────────────────────────────────────────────────────┤
│ serv_blinky (2s)          : No events, just toggles LED             │
│                                                                      │
│ serv_temperature_sensor (1s) : Publishes events                     │
│   ├─► EVENT_TEMPERATURE_UPDATED (temp, humidity, timestamp)        │
│   └─► EVENT_SENSOR_ERROR (error code)                              │
│                                                                      │
│ serv_display              : Event-driven subscriber                 │
│   ├─◄ EVENT_TEMPERATURE_UPDATED → on_temperature_updated()         │
│   │    └─► ips_display_write_temp_data()                           │
│   └─◄ EVENT_SENSOR_ERROR → on_sensor_error()                       │
│        └─► ips_display_write_error()                               │
│                                                                      │
│ protocol_handler          : ESP32 communication & subscriber        │
│   ├─◄ EVENT_TEMPERATURE_UPDATED → temperature_event_handler()      │
│   │    └─► Stores data for buffer requests                         │
│   ├─► Handles UART commands (GET_BUFFER, START/STOP_STREAM, etc.)  │
│   └─► Publishes NOTIFY_SENSOR_DATA via UART                        │
│                                                                      │
│ serv_current_monitor      : INA226 sensor (commented out)           │
└─────────────────┬───────────────────────────────────────────────────┘
                  │
┌─────────────────▼───────────────────────────────────────────────────┐
│                        Event Bus (OS Layer)                         │
│  - Queue: 16 events, 64 bytes/event                                │
│  - Processing: event_bus_process() in main loop                     │
│  - Max 5 subscribers per event type                                 │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Design Patterns Used

1. **Layered Architecture** - Clear separation of concerns
2. **Publisher-Subscriber** - Event-driven communication via Event Bus
3. **Hardware Abstraction** - Platform independence via HAL
4. **Service-Oriented** - Modular capabilities (Services)
5. **Dependency Inversion** - Upper layers depend on abstractions, not concretions
6. **Command Pattern** - Protocol handler command routing
7. **State Machine** - Protocol streaming state management
8. **Ring Buffer** - Efficient circular buffer voor sensor data history

---

## Performance Metrics

### Timing
- **Main loop cycle**: 10ms (via `osDelay(10)`)
- **Temperature sensor sampling**: 1000ms
- **Blinky toggle**: 2000ms
- **Event processing**: Sub-millisecond per event

### Memory (estimated)
- **Event queue**: ~1KB (16 events × 64 bytes)
- **Sensor ring buffer**: Configured size (zie [sensor_ring_buffer.h](Utils/sensor_ring_buffer.h))
- **Stack per task**: FreeRTOS configured (zie [FreeRTOSConfig.h](Core/Inc/FreeRTOSConfig.h))

### UART Performance
- **Protocol baud rate**: 921600 bps (115.2 KB/s theoretical)
- **Hardware flow control**: RTS/CTS enabled
- **DMA**: Supported for async transmission
- **CRC overhead**: 2 bytes per packet

---

## Lessons Learned & Best Practices

### Wat Goed Werkte
1. **Event-driven design** vermindert coupling dramatisch
2. **HAL abstractie** maakt code testbaar en portabel
3. **Service architectuur** maakt features modular en herbruikbaar
4. **BSP als gatekeeper** centraliseert hardware access
5. **Protocol stack separatie** (framing vs. handler) is clean

### Pitfalls Vermeden
1. ❌ **Circular dependencies** - Voorkomen door strict layering
2. ❌ **Global state** - Minimalized, prefer service context structs
3. ❌ **Blocking operations** - Event-driven + RTOS voorkomt dit
4. ❌ **Hard-coded pins** - Alles in pinout.h
5. ❌ **Tight coupling** - Event bus breekt directe dependencies

### Aanbevelingen voor Developers
- **Volg de lagen**: Nooit omhoog roepen, geen lagen overslaan
- **Gebruik events**: Voor cross-service communicatie
- **Test met mocks**: HAL kan gemocked worden voor unit tests
- **Documenteer protocol**: Binary protocols zijn moeilijk te debuggen
- **Monitor event queue**: Runtime diagnostics zijn essentieel

---

## License & Attribution

**Gebouwd met professionele architectuurprincipes voor productie-grade embedded systemen.**

Ontwikkeld voor STM32F767ZI Nucleo board met FreeRTOS.

**Voor verdere vragen of bijdragen, zie de issue tracker of contact de ontwikkelaar.**
