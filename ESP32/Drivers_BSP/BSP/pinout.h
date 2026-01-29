#ifndef PINOUT_H
#define PINOUT_H

#include <driver/gpio.h>

// ============================================================================
// GPIO Pin Definitions
// ============================================================================
#define LED_BUILTIN         GPIO_NUM_2

// ============================================================================
// STM32 UART Communication Pins
// ============================================================================
// UART1 is used for STM32 communication (UART0 reserved for console)
#define STM32_UART_PORT     1                   // Use UART1
#define STM32_UART_TX_PIN   GPIO_NUM_17         // ESP32 TX -> STM32 RX
#define STM32_UART_RX_PIN   GPIO_NUM_16         // ESP32 RX <- STM32 TX
#define STM32_UART_RTS_PIN  GPIO_NUM_5          // Hardware flow control RTS
#define STM32_UART_CTS_PIN  GPIO_NUM_4          // Hardware flow control CTS

// STM32 Handshake GPIO Pins
#define STM32_WAKEUP_PIN    GPIO_NUM_18         // GPIO to wake STM32 from sleep
#define STM32_READY_PIN     GPIO_NUM_19         // GPIO handshake from STM32 (input)

#endif // PINOUT_H
