#ifndef BSP_WT32_SC01_H
#define BSP_WT32_SC01_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CORRECT PIN CONFIGURATION FOR WT32-SC01
 * 
 * Based on LovyanGFX library and multiple confirmed working examples
 * Reference: https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1/panel/Panel_ST7796.hpp
 * 
 * The WT32-SC01 uses ST7796 controller with 8-bit parallel interface
 */

// LCD Control Pins - VERIFIED WORKING CONFIGURATION
#define LCD_BK_LIGHT_PIN    23  // Backlight control
#define LCD_RST_PIN         -1  // Reset tied to ESP32 EN (use -1 to disable)
#define LCD_CS_PIN          15  // Chip Select
#define LCD_DC_PIN          2   // Data/Command (also called RS)
#define LCD_WR_PIN          4   // Write strobe

// LCD 8-bit Data Bus (D0-D7)
#define LCD_D0_PIN          12
#define LCD_D1_PIN          13  
#define LCD_D2_PIN          14
#define LCD_D3_PIN          15  // Same as CS - this is normal for this board!
#define LCD_D4_PIN          16
#define LCD_D5_PIN          17
#define LCD_D6_PIN          18  // Also used for Touch I2C_SDA
#define LCD_D7_PIN          19  // Also used for Touch I2C_SCL

// Display specifications
#define LCD_H_RES           320
#define LCD_V_RES           480
#define LCD_PIXEL_CLK_HZ    (10 * 1000 * 1000)  // 10MHz for stability

// Touch I2C configuration (FT6336)
#define TOUCH_I2C_SDA       18  // Shared with LCD_D6
#define TOUCH_I2C_SCL       19  // Shared with LCD_D7  
#define TOUCH_INT_PIN       39  // SENSOR_VN - GPIO39
#define TOUCH_RST_PIN       -1  // Not used, set to -1
#define TOUCH_I2C_NUM       I2C_NUM_0
#define TOUCH_I2C_ADDR      0x38  // FT6336 I2C address

/**
 * @brief Initialize WT32-SC01 display
 * 
 * @param lv_disp Pointer to store LVGL display object
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_display_init(lv_disp_t **lv_disp);

/**
 * @brief Initialize touch controller
 * 
 * @param lv_indev Pointer to store LVGL input device object
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_touch_init(lv_indev_t **lv_indev);

/**
 * @brief Set LCD backlight brightness
 * 
 * @param brightness_percent Brightness percentage (0-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bsp_display_brightness_set(int brightness_percent);

#ifdef __cplusplus
}
#endif

#endif // BSP_WT32_SC01_H