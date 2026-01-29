# ESP32 Layered Architecture Template

## Project Structure Overview

```
├── main/                    # ESP-IDF entry point
├── Application/             # Application logic layer
├── OS/                      # OS abstractions (events, tasks)
├── Middleware/              # Business logic (features, services)
├── Drivers_BSP/            # Board support & device drivers
└── HAL_Wrapper/            # Hardware abstraction layer
```

---

## Architecture Flow

### Downward Flow (Function Calls)
**Top → Bottom: Direct function calls**

```
main → Application → Middleware → Drivers_BSP → HAL_Wrapper → Hardware
```

**Example:**
- `main.c` calls `app_init()` 
- Application calls `os_tasks_init()`, `feature_init_all()`
- Middleware calls driver functions like `aht25_init()`
- Drivers call HAL functions like `hal_i2c_init()`
- HAL calls ESP-IDF APIs like `i2c_new_master_bus()`

### Upward Flow (Events/Callbacks)
**Bottom ↑ Top: Events, queues, callbacks**

```
Hardware → HAL_Wrapper → Drivers_BSP → Middleware → Application
            (interrupts)   (callbacks)    (events)    (tasks)
```

**Example:**
- I2C interrupt → HAL callback → Driver callback → Publish event → Application receives event

---

## Layer Responsibilities

### 1. **main/** (ESP-IDF Entry Point)
**Purpose:** Platform initialization only
- Initialize NVS flash
- Initialize BSP
- Call Application layer
- **NO business logic here**

**Template Status:** ✅ Generic (use as-is)

```c
void app_main(void) {
    nvs_flash_init();
    bsp_init();
    app_init();
    app_run();
}
```

---

### 2. **Application/** (Application Coordinator)
**Purpose:** Coordinate high-level application flow
- Initialize OS services (event bus, tasks)
- Start state machines
- Application-level configuration
- **NO hardware access**

**Template Status:** ✅ Generic (minimal changes needed)

**Files:**
- `app_main.h/c` - Application initialization
- `config.h` - Application-wide configuration (task priorities, stack sizes)

---

### 3. **OS/** (OS Abstractions)
**Purpose:** Provide OS-agnostic services
- Event bus (pub-sub messaging)
- Task management
- OS configuration

**Template Status:** ✅ Generic (use as-is)

**Key Components:**
- **event_bus** - Publish-subscribe event system
  - Add event types to `event_type_t` enum
  - Subscribe: `event_bus_subscribe(EVENT_TYPE, callback)`
  - Publish: `event_bus_publish(EVENT_TYPE, data)`

- **os_tasks** - Task initialization and registration
  - Register features/services
  - Start background tasks

---

### 4. **Middleware/** (Business Logic)
**Purpose:** Application-specific features
- Features (modular functionality units)
- Services (shared business logic)
- Control logic

**Template Status:** ⚠️ Project-specific (modify per project)

**Structure:**
```
Middleware/
├── Features/        # Modular features (audio, sensors, etc.)
├── Services/        # Shared services (currently empty)
└── Control/         # Control logic (currently empty)
```

**Current Content:** Empty stubs - **replace with your features**

---

### 5. **Drivers_BSP/** (Hardware Drivers)
**Purpose:** Device-specific drivers and board configuration
- BSP (board support package) - pin definitions, board init
- Custom device drivers (sensors, actuators, etc.)

**Template Status:** ⚠️ Board-specific (modify per project)

**Structure:**
```
Drivers_BSP/
├── BSP/
│   ├── bsp.h/c       # Board initialization
│   └── pinout.h      # Pin definitions
└── Custom/
    ├── aht25.h/c     # Example sensor driver
    └── peripheral.h  # Peripheral definitions
```

**Current Content:**
- `bsp.c` - Empty (add your board init)
- `pinout.h` - Example GPIO pins (modify for your board)
- `aht25.c` - Example I2C sensor (replace with your devices)

---

### 6. **HAL_Wrapper/** (Hardware Abstraction)
**Purpose:** Wrap ESP-IDF APIs for portability
- Abstract peripheral APIs (GPIO, I2C, SPI, etc.)
- Isolate ESP-IDF specifics
- Makes porting to other platforms easier

**Template Status:** ✅ Generic (use as-is)

**Files:**
- `hal_gpio.h/c` - GPIO abstraction
- `hal_i2c.h/c` - I2C abstraction (ESP-IDF 5.5 i2c_master API)
- `hal_spi.h/c` - SPI abstraction
- `hal_delay.h/c` - Delay functions

**Note:** These use ESP-IDF 5.5 APIs. Update if ESP-IDF version changes.

---

## Communication Patterns

### Downward (Command Flow)
Use **direct function calls**:
```c
// Application calls Middleware
feature_init("audio_bridge");

// Middleware calls Driver
aht25_read_temperature(&temp);

// Driver calls HAL
hal_i2c_read(device, buffer, len);
```

### Upward (Event Flow)
Use **events/callbacks**:
```c
// Driver detects event → Publish to event bus
event_bus_publish(EVENT_TEMP_UPDATED, &temp_data);

// Application subscribes and receives
void temp_callback(event_type_t type, void *data) {
    temperature_t *temp = (temperature_t*)data;
    // Handle temperature update
}
event_bus_subscribe(EVENT_TEMP_UPDATED, temp_callback);
```

---

## Template Usage Guide

### Starting a New Project

1. **Copy the entire structure**
   ```bash
   cp -r klavis-audio-bridge my-new-project
   ```

2. **Keep as-is (Generic layers):**
   - ✅ `main/` - No changes needed
   - ✅ `Application/app_main.c` - Minimal changes
   - ✅ `OS/` - Use as-is
   - ✅ `HAL_Wrapper/` - Use as-is

3. **Modify for your project:**
   - ⚠️ `Drivers_BSP/BSP/pinout.h` - Define your GPIO pins
   - ⚠️ `Drivers_BSP/BSP/bsp.c` - Add board initialization
   - ⚠️ `Drivers_BSP/Custom/` - Replace with your device drivers
   - ⚠️ `Middleware/Features/` - Replace with your features

4. **Update CMakeLists.txt:**
   - Root `CMakeLists.txt` - Update project name
   - Update `REQUIRES` in each component as needed

5. **Configure events:**
   - Add event types to `OS/event_bus.h`
   - Subscribe to events in Application layer
   - Publish events from Middleware/Drivers

---

## Current Template Quality

### ✅ Ready to Use (No Changes)
- **main/** - Generic ESP-IDF entry
- **OS/event_bus** - Generic pub-sub system
- **HAL_Wrapper/** - ESP-IDF 5.5 peripheral abstractions

### 🔧 Need Minor Updates
- **Application/app_main.c** - Currently just initializes OS
  - Add your state machine logic
  - Add application-specific initialization

### ⚠️ Project-Specific (Replace)
- **Middleware/** - Currently has empty feature stubs
  - Delete example features
  - Create your own features

- **Drivers_BSP/** - Has example content
  - `pinout.h` - Replace with your pin definitions
  - `bsp.c` - Currently empty, add your board init
  - `aht25.c` - Example driver, replace with your devices

---

## Best Practices

### ✅ DO
- Keep business logic in Middleware
- Use event bus for upward communication
- Keep HAL generic (don't add project-specific code)
- Define all pins in `pinout.h`
- Use `config.h` for task priorities and sizes

### ❌ DON'T
- Put hardware access in Application layer
- Put business logic in drivers
- Use relative paths in includes (use component REQUIRES)
- Hardcode GPIO numbers in driver code

---

## Adding a New Feature

1. **Create Feature** (Middleware/Features/)
   ```c
   // feat_my_feature.h
   feature_t* feat_my_feature_get(void);
   ```

2. **Register in OS** (OS/os_tasks.c)
   ```c
   feature_register(feat_my_feature_get());
   ```

3. **Add Driver** (Drivers_BSP/Custom/)
   ```c
   // my_device.h/c - device-specific driver
   ```

4. **Define Events** (OS/event_bus.h)
   ```c
   typedef enum {
       EVENT_MY_DEVICE_READY,
       // ...
   } event_type_t;
   ```

5. **Subscribe to Events** (Application/app_main.c)
   ```c
   event_bus_subscribe(EVENT_MY_DEVICE_READY, my_callback);
   ```

---

## Summary

**Template Quality:** Good foundation, needs customization

**Generic (reusable):**
- Main entry point
- OS abstractions (event bus, tasks)
- HAL wrappers

**Project-specific (customize):**
- Middleware features
- Device drivers
- Board configuration

**Recommended Action:** Use as template, delete example drivers (AHT25), and replace with your project-specific code.
