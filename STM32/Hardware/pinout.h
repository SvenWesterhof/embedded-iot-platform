#ifndef PINOUT_H
#define PINOUT_H

#include "stm32f7xx_hal.h"

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

// Temperature Sensor Pins
#define TEMP_SENSOR_ON_OFF_PIN GPIO_PIN_14 //D1 o n board
#define TEMP_SENSOR_ON_OFF_PORT GPIOG 

#define TEMP_SENSOR_SCL_PIN GPIO_PIN_1 //D69 on board
#define TEMP_SENSOR_SCL_PORT GPIOF

#define TEMP_SENSOR_SDA_PIN GPIO_PIN_0 //D68 on board
#define TEMP_SENSOR_SDA_PORT GPIOF

//

#endif // PINOUT_H