#include "board/board.h"

#include <string.h>

#include "board/board_config.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "canbus_board";

static esp_err_t board_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_I2C_SDA_IO,
        .scl_io_num = BOARD_I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_PORT, &conf), TAG, "i2c param config failed");
    return i2c_driver_install(BOARD_I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t ch422g_write(uint8_t value)
{
    return i2c_master_write_to_device(BOARD_I2C_PORT, BOARD_I2C_TOUCH_CTRL_ADDR, &value, 1,
                                      BOARD_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t board_ch422g_set_mode_output(void)
{
    uint8_t write_buf = BOARD_CH422G_MODE_OUTPUT;
    return i2c_master_write_to_device(BOARD_I2C_PORT, BOARD_I2C_IO_EXPANDER_ADDR, &write_buf, 1,
                                      BOARD_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
}

static esp_err_t board_backlight_enable(void)
{
    ESP_RETURN_ON_ERROR(board_ch422g_set_mode_output(), TAG, "ch422g mode set failed");
    return ch422g_write(BOARD_CH422G_DISP_ON);
}

static esp_err_t board_touch_reset_sequence(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BOARD_TOUCH_IRQ_GPIO,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "touch irq gpio config failed");

    ESP_RETURN_ON_ERROR(board_ch422g_set_mode_output(), TAG, "ch422g mode set failed");
    ESP_RETURN_ON_ERROR(ch422g_write(BOARD_CH422G_TP_RST_LOW), TAG, "touch reset low failed");

    esp_rom_delay_us(100 * 1000);
    gpio_set_level(BOARD_TOUCH_IRQ_GPIO, 0);
    esp_rom_delay_us(100 * 1000);

    ESP_RETURN_ON_ERROR(ch422g_write(BOARD_CH422G_TP_RST_HIGH), TAG, "touch reset high failed");
    esp_rom_delay_us(200 * 1000);
    return ESP_OK;
}

static esp_err_t board_panel_init(esp_lcd_panel_handle_t *out_panel)
{
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .psram_trans_align = 64,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = BOARD_PIN_NUM_DISP_EN,
        .pclk_gpio_num = BOARD_PIN_NUM_PCLK,
        .vsync_gpio_num = BOARD_PIN_NUM_VSYNC,
        .hsync_gpio_num = BOARD_PIN_NUM_HSYNC,
        .de_gpio_num = BOARD_PIN_NUM_DE,
        .data_gpio_nums = {
            BOARD_PIN_NUM_DATA0,
            BOARD_PIN_NUM_DATA1,
            BOARD_PIN_NUM_DATA2,
            BOARD_PIN_NUM_DATA3,
            BOARD_PIN_NUM_DATA4,
            BOARD_PIN_NUM_DATA5,
            BOARD_PIN_NUM_DATA6,
            BOARD_PIN_NUM_DATA7,
            BOARD_PIN_NUM_DATA8,
            BOARD_PIN_NUM_DATA9,
            BOARD_PIN_NUM_DATA10,
            BOARD_PIN_NUM_DATA11,
            BOARD_PIN_NUM_DATA12,
            BOARD_PIN_NUM_DATA13,
            BOARD_PIN_NUM_DATA14,
            BOARD_PIN_NUM_DATA15,
        },
        .timings = {
            .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, &panel_handle), TAG, "new rgb panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "panel init failed");
    *out_panel = panel_handle;
    return ESP_OK;
}

static esp_err_t board_touch_init(esp_lcd_touch_handle_t *out_touch)
{
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_touch_handle_t tp = NULL;

    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)BOARD_I2C_PORT, &tp_io_config, &tp_io_handle),
        TAG, "touch io init failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = BOARD_TOUCH_IRQ_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp), TAG, "touch init failed");
    *out_touch = tp;
    return ESP_OK;
}

esp_err_t board_init(board_display_t *out_display)
{
    ESP_RETURN_ON_FALSE(out_display, ESP_ERR_INVALID_ARG, TAG, "null display handle");
    memset(out_display, 0, sizeof(*out_display));

    ESP_LOGI(TAG, "Initializing I2C bus");
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "i2c init failed");

    ESP_LOGI(TAG, "Initializing RGB panel");
    ESP_RETURN_ON_ERROR(board_panel_init(&out_display->panel_handle), TAG, "panel init failed");

    ESP_LOGI(TAG, "Enabling display backlight (CH422G EXIO2)");
    ESP_RETURN_ON_ERROR(board_backlight_enable(), TAG, "backlight enable failed");

    ESP_LOGI(TAG, "Resetting touch controller (CH422G EXIO1)");
    ESP_RETURN_ON_ERROR(board_touch_reset_sequence(), TAG, "touch reset failed");

    ESP_LOGI(TAG, "Initializing GT911 touch");
    ESP_RETURN_ON_ERROR(board_touch_init(&out_display->touch_handle), TAG, "touch init failed");

    return ESP_OK;
}
