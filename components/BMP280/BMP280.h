#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// BMP280 I2C address options
#define BMP280_I2C_ADDR_PRIM    0x76  // Primary I2C address (SDO to GND)
#define BMP280_I2C_ADDR_SEC     0x77  // Secondary I2C address (SDO to VDDIO)

// BMP280 register addresses
#define BMP280_REG_TEMP_XLSB    0xFC
#define BMP280_REG_TEMP_LSB     0xFB
#define BMP280_REG_TEMP_MSB     0xFA
#define BMP280_REG_PRESS_XLSB   0xF9
#define BMP280_REG_PRESS_LSB    0xF8
#define BMP280_REG_PRESS_MSB    0xF7
#define BMP280_REG_CONFIG       0xF5
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_STATUS       0xF3
#define BMP280_REG_RESET        0xE0
#define BMP280_REG_ID           0xD0
#define BMP280_REG_CALIB_START  0x88

// BMP280 chip ID
#define BMP280_CHIP_ID          0x58

// BMP280 reset command
#define BMP280_RESET_CMD        0xB6

// Oversampling settings
#define BMP280_OVERSAMP_SKIPPED 0x00
#define BMP280_OVERSAMP_1X      0x01
#define BMP280_OVERSAMP_2X      0x02
#define BMP280_OVERSAMP_4X      0x03
#define BMP280_OVERSAMP_8X      0x04
#define BMP280_OVERSAMP_16X     0x05

// Power modes
#define BMP280_SLEEP_MODE       0x00
#define BMP280_FORCED_MODE      0x01
#define BMP280_NORMAL_MODE      0x03

// Standby time settings (ms)
#define BMP280_STANDBY_0_5_MS   0x00
#define BMP280_STANDBY_62_5_MS  0x01
#define BMP280_STANDBY_125_MS   0x02
#define BMP280_STANDBY_250_MS   0x03
#define BMP280_STANDBY_500_MS   0x04
#define BMP280_STANDBY_1000_MS  0x05
#define BMP280_STANDBY_2000_MS  0x06
#define BMP280_STANDBY_4000_MS  0x07

// Filter settings
#define BMP280_FILTER_OFF       0x00
#define BMP280_FILTER_COEFF_2   0x01
#define BMP280_FILTER_COEFF_4   0x02
#define BMP280_FILTER_COEFF_8   0x03
#define BMP280_FILTER_COEFF_16  0x04

// Calibration data structure
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp280_calib_data_t;

// BMP280 device structure
typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    bmp280_calib_data_t calib;
    int32_t t_fine;  // Temperature fine value for pressure compensation
    SemaphoreHandle_t i2c_mutex;  // Mutex for I2C bus access (optional, can be NULL)
} bmp280_dev_t;

/**
 * @brief Initialize BMP280 sensor
 *
 * @param dev Pointer to BMP280 device structure
 * @param i2c_port I2C port number (I2C_NUM_0 or I2C_NUM_1)
 * @param i2c_addr I2C address (BMP280_I2C_ADDR_PRIM or BMP280_I2C_ADDR_SEC)
 * @param i2c_mutex Optional I2C mutex for coordinating bus access (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bmp280_init(bmp280_dev_t *dev, i2c_port_t i2c_port, uint8_t i2c_addr, SemaphoreHandle_t i2c_mutex);

/**
 * @brief Configure BMP280 sensor
 *
 * @param dev Pointer to BMP280 device structure
 * @param osrs_t Temperature oversampling (BMP280_OVERSAMP_xxx)
 * @param osrs_p Pressure oversampling (BMP280_OVERSAMP_xxx)
 * @param mode Power mode (BMP280_SLEEP_MODE, BMP280_FORCED_MODE, BMP280_NORMAL_MODE)
 * @param t_sb Standby time (BMP280_STANDBY_xxx)
 * @param filter IIR filter coefficient (BMP280_FILTER_xxx)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bmp280_config(bmp280_dev_t *dev, uint8_t osrs_t, uint8_t osrs_p,
                        uint8_t mode, uint8_t t_sb, uint8_t filter);

/**
 * @brief Read raw temperature and pressure data
 *
 * @param dev Pointer to BMP280 device structure
 * @param raw_temp Pointer to store raw temperature
 * @param raw_press Pointer to store raw pressure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bmp280_read_raw(bmp280_dev_t *dev, int32_t *raw_temp, int32_t *raw_press);

/**
 * @brief Read compensated temperature in degrees Celsius
 *
 * @param dev Pointer to BMP280 device structure
 * @param temperature Pointer to store temperature in °C
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bmp280_read_temperature(bmp280_dev_t *dev, float *temperature);

/**
 * @brief Read compensated pressure in hPa
 *
 * @param dev Pointer to BMP280 device structure
 * @param pressure Pointer to store pressure in hPa
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bmp280_read_pressure(bmp280_dev_t *dev, float *pressure);

/**
 * @brief Read both temperature and pressure
 *
 * @param dev Pointer to BMP280 device structure
 * @param temperature Pointer to store temperature in °C
 * @param pressure Pointer to store pressure in hPa
 * @return esp_err_t ESP_OK on success
 */
esp_err_t bmp280_read_data(bmp280_dev_t *dev, float *temperature, float *pressure);

/**
 * @brief Calculate altitude based on pressure
 *
 * @param pressure Current pressure in hPa
 * @param sea_level_pressure Sea level pressure in hPa (default: 1013.25)
 * @return float Altitude in meters
 */
float bmp280_calc_altitude(float pressure, float sea_level_pressure);

#ifdef __cplusplus
}
#endif

#endif // BMP280_H
