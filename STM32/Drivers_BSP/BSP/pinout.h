#ifndef PINOUT_H
#define PINOUT_H

#include "stm32f7xx_hal.h"
#include "../../HAL/hal_uart.h"

#define EXT_LED_PIN         GPIO_PIN_15 //D2 on board
#define EXT_LED_GPIO_PORT   GPIOF

// IPS Display Pins
#define DISPLAY_SCL_PIN     GPIO_PIN_6
#define DISPLAY_SCL_PORT    GPIOB

#define DISPLAY_MI_PIN    GPIO_PIN_4
#define DISPLAY_MI_PORT   GPIOB

#define DISPLAY_MO_PIN    GPIO_PIN_5
#define DISPLAY_MO_PORT   GPIOB

#define DISPLAY_CS_PIN      GPIO_PIN_7
#define DISPLAY_CS_PORT     GPIOB

#define DISPLAY_BACKLIGHT_PIN   GPIO_PIN_14
#define DISPLAY_BACKLIGHT_PORT  GPIOD

// Temperature Sensor Pins
#define TEMP_SENSOR_ON_OFF_PIN GPIO_PIN_14 //D1 on board
#define TEMP_SENSOR_ON_OFF_PORT GPIOG 

// INA226 Power Monitor conversion ready pin
#define INA226_ALERT_PIN    GPIO_PIN_10
#define INA226_ALERT_PORT   GPIOB

// ESP32 UART (USART2) - PD3/PD4/PD5/PD6
#define STM32_UART_PORT         HAL_UART_PORT_1  // Maps to USART2 in hal_uart.c
#define STM32_UART_TX_PIN       GPIO_PIN_5
#define STM32_UART_TX_PORT      GPIOD
#define STM32_UART_RX_PIN       GPIO_PIN_6
#define STM32_UART_RX_PORT      GPIOD
#define STM32_UART_RTS_PIN      GPIO_PIN_4
#define STM32_UART_RTS_PORT     GPIOD
#define STM32_UART_CTS_PIN      GPIO_PIN_3
#define STM32_UART_CTS_PORT     GPIOD

#endif // PINOUT_H
