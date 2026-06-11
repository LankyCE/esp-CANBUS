#pragma once

#include "driver/gpio.h"

#define BOARD_LCD_H_RES              800
#define BOARD_LCD_V_RES              480
#define BOARD_LCD_PIXEL_CLOCK_HZ     (16 * 1000 * 1000)

// Waveshare ESP32-S3-Touch-LCD-4.3B RGB pins
#define BOARD_PIN_NUM_HSYNC          46
#define BOARD_PIN_NUM_VSYNC          3
#define BOARD_PIN_NUM_DE             5
#define BOARD_PIN_NUM_PCLK           7
#define BOARD_PIN_NUM_DATA0          14
#define BOARD_PIN_NUM_DATA1          38
#define BOARD_PIN_NUM_DATA2          18
#define BOARD_PIN_NUM_DATA3          17
#define BOARD_PIN_NUM_DATA4          10
#define BOARD_PIN_NUM_DATA5          39
#define BOARD_PIN_NUM_DATA6          0
#define BOARD_PIN_NUM_DATA7          45
#define BOARD_PIN_NUM_DATA8          48
#define BOARD_PIN_NUM_DATA9          47
#define BOARD_PIN_NUM_DATA10         21
#define BOARD_PIN_NUM_DATA11         1
#define BOARD_PIN_NUM_DATA12         2
#define BOARD_PIN_NUM_DATA13         42
#define BOARD_PIN_NUM_DATA14         41
#define BOARD_PIN_NUM_DATA15         40

#define BOARD_PIN_NUM_DISP_EN        -1

// Shared I2C bus: touch + CH422G
#define BOARD_I2C_PORT               0
#define BOARD_I2C_SCL_IO             9
#define BOARD_I2C_SDA_IO             8
#define BOARD_I2C_FREQ_HZ            400000
#define BOARD_I2C_TIMEOUT_MS         1000

// CH422G + touch control addresses used by Waveshare reference firmware
#define BOARD_I2C_IO_EXPANDER_ADDR   0x24
#define BOARD_I2C_TOUCH_CTRL_ADDR    0x38

#define BOARD_TOUCH_IRQ_GPIO         GPIO_NUM_4

// CH422G write values for this board
#define BOARD_CH422G_MODE_OUTPUT     0x01
#define BOARD_CH422G_TP_RST_LOW      0x2C
#define BOARD_CH422G_TP_RST_HIGH     0x2E
#define BOARD_CH422G_DISP_ON         0x1E
