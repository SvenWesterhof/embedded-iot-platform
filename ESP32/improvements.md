# ESP32 Improvement Notes

## WiFi Manager - Race Condition on Shared State

**File:** `Middleware/Control/cont_wifi_manager.c`

**Issue:** The `wifi_state_t` struct is accessed from two unsynchronized contexts:
1. **ESP system event task** — the `wifi_event_handler` modifies `state.status`, `state.retry_count`, `state.auto_reconnect`
2. **Application task(s)** — public API functions like `wifi_manager_disconnect()`, `wifi_manager_connect()` modify the same fields

**Example race:**
- `wifi_manager_disconnect()` sets `auto_reconnect = false`
- The disconnect event could fire on the system task *before* that assignment
- Event handler sees `auto_reconnect == true` and starts reconnecting

**Severity:** Low — in practice the ESP event loop processes sequentially and the fields are small enough to be effectively atomic on ESP32. No issues observed.

**Possible fixes:**
- Add a mutex around `state` access
- Use atomic flags for `auto_reconnect` / `started`
- Reorder: set `auto_reconnect = false` *before* calling `esp_wifi_disconnect()`

## Event Groups Not Wrapped in OS Abstraction

**File:** `Middleware/Control/cont_wifi_manager.c`

**Issue:** FreeRTOS `xEventGroupCreate`, `xEventGroupSetBits`, `xEventGroupClearBits`, `xEventGroupWaitBits` are used directly instead of going through the `os_wrapper` abstraction layer.

**Severity:** Low — event groups are only used in the wifi manager, which is inherently ESP-specific. No portability concern today.

**Action:** Wrap in `os_wrapper` if event groups are ever needed in shared/common code.

## MQTT Client - Inconsistent OS Wrapper Usage for Mutex

**File:** `Middleware/Services/serv_mqtt_client.c`

**Issue:** The mutex is created via `os_wrapper` but deleted using the direct FreeRTOS call `vSemaphoreDelete()` instead of `os_mutex_delete()`. This breaks the abstraction layer — if the `os_wrapper` implementation ever changes (e.g., adds cleanup tracking or switches RTOS), the direct delete call would be missed.

**Severity:** Low — functionally identical today since `os_mutex_delete` wraps `vSemaphoreDelete`. But inconsistent and error-prone.

**Fix:** Replace `vSemaphoreDelete(ctx.mutex)` with `os_mutex_delete(ctx.mutex)` in all cleanup paths.

## MQTT Client - Wrong Prefix on Event Type

**File:** `Middleware/Services/serv_mqtt_client.h`

**Issue:** The MQTT event enum and typedef use the `feat_` prefix instead of `serv_`, violating the layer naming convention:
- `feat_mqtt_event_t` → should be `serv_mqtt_event_t`
- `FEAT_MQTT_EVT_*` → should be `SERV_MQTT_EVT_*`

**Severity:** Low — no functional impact, but misleading. `feat_` implies the Feature layer, while this type belongs to the Service layer.

**Fix:** Rename `feat_mqtt_event_t` → `serv_mqtt_event_t` and `FEAT_MQTT_EVT_*` → `SERV_MQTT_EVT_*` in the header and all call sites.

## STM32 OTA Service - Wrong Return Error Codes

**File:** `Middleware/Services/serv_stm32_ota.c`

**Issue:** Three functions return semantically incorrect error codes:

| Location | Condition | Returns | Should Return |
|---|---|---|---|
| `serv_stm32_ota_start()` line 75 | Not initialized | `STM32_OTA_ERR_NO_MEM` | `STM32_OTA_ERR_NOT_INITIALIZED` or similar |
| `serv_stm32_ota_start()` line 83 | Failed to take mutex | `STM32_OTA_ERR_IN_PROGRESS` | `STM32_OTA_ERR_TIMEOUT` or `STM32_OTA_ERR_BUSY` |
| `serv_stm32_ota_abort()` line 153 | Not initialized | `STM32_OTA_ERR_NO_MEM` | `STM32_OTA_ERR_NOT_INITIALIZED` or similar |

**Severity:** Medium — wrong error codes make debugging harder. A caller seeing `ERR_NO_MEM` when the service was never initialized, or `ERR_IN_PROGRESS` when the mutex timed out, would diagnose the wrong root cause.

**Fix:** Add appropriate error codes to the enum if missing, and return the correct code at each site.

## HTTPS Download Service - Direct vTaskDelay Call

**File:** `Middleware/Services/serv_https_download.c` line 171

**Issue:** `vTaskDelay(pdMS_TO_TICKS(200))` is used directly instead of `os_delay_ms(200)`, bypassing the OS abstraction layer.

**Severity:** Low — functionally identical, but inconsistent with the rest of the codebase.

**Fix:** Replace with `os_delay_ms(200)`.

## Signature Verify Service - Direct ESP Logging

**File:** `Middleware/Services/serv_signature_verify.c`

**Issue:** Uses `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE` and `#include "esp_log.h"` directly instead of the `portable_log` abstraction (`LOG_I`, `LOG_W`, `LOG_E`) used consistently across the rest of the codebase.

**Severity:** Low — functionally identical on ESP32, but ties this service to ESP-IDF logging and breaks portability.

**Fix:** Replace `#include "esp_log.h"` with `#include "portable_log.h"` and replace all `ESP_LOG*` calls with the corresponding `LOG_*` macros.

## Event Bus - Unused Define

**File:** `OS/event_bus.c` line 12

**Issue:** `#define EVENT_BUS_TASK_STACK 3072` is defined but never used — the dispatch task stack size is hardcoded as `4096` directly in the `os_task_create_pinned()` call.

**Severity:** Low — dead code, causes a compiler warning on strict builds.

**Fix:** Either remove the define, or use it in the `os_task_create_pinned()` call to replace the hardcoded `4096`.
