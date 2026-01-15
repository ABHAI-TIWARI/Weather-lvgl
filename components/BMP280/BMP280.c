#include "BMP280.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "BMP280";

// I2C communication timeout
#define BMP280_I2C_TIMEOUT_MS   1000

/**
 * @brief Write data to BMP280 register (internal - mutex must be held by caller)
 */
static esp_err_t bmp280_write_reg_internal(bmp280_dev_t *dev, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_write_to_device(dev->i2c_port, dev->i2c_addr,
                                                write_buf, sizeof(write_buf),
                                                pdMS_TO_TICKS(BMP280_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02X: %s", reg_addr, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Write data to BMP280 register (with mutex)
 */
static esp_err_t bmp280_write_reg(bmp280_dev_t *dev, uint8_t reg_addr, uint8_t data)
{
    esp_err_t ret = ESP_FAIL;

    // Take I2C mutex if available
    if (dev->i2c_mutex != NULL) {
        if (xSemaphoreTake(dev->i2c_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to take I2C mutex for write");
            return ESP_ERR_TIMEOUT;
        }
    }

    ret = bmp280_write_reg_internal(dev, reg_addr, data);

    // Release mutex
    if (dev->i2c_mutex != NULL) {
        xSemaphoreGive(dev->i2c_mutex);
    }

    return ret;
}

/**
 * @brief Read data from BMP280 register (internal - mutex must be held by caller)
 */
static esp_err_t bmp280_read_reg_internal(bmp280_dev_t *dev, uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = i2c_master_write_read_device(dev->i2c_port, dev->i2c_addr,
                                                  &reg_addr, 1, data, len,
                                                  pdMS_TO_TICKS(BMP280_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02X: %s", reg_addr, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Read data from BMP280 register (with mutex)
 */
static esp_err_t bmp280_read_reg(bmp280_dev_t *dev, uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_FAIL;

    // Take I2C mutex if available
    if (dev->i2c_mutex != NULL) {
        if (xSemaphoreTake(dev->i2c_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to take I2C mutex for read");
            return ESP_ERR_TIMEOUT;
        }
    }

    ret = bmp280_read_reg_internal(dev, reg_addr, data, len);

    // Release mutex
    if (dev->i2c_mutex != NULL) {
        xSemaphoreGive(dev->i2c_mutex);
    }

    return ret;
}

/**
 * @brief Read calibration data from BMP280
 */
static esp_err_t bmp280_read_calib_data(bmp280_dev_t *dev)
{
    uint8_t calib_data[24];
    esp_err_t ret = bmp280_read_reg(dev, BMP280_REG_CALIB_START, calib_data, 24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data");
        return ret;
    }

    // Parse calibration data according to BMP280 datasheet
    dev->calib.dig_T1 = (uint16_t)(calib_data[1] << 8) | calib_data[0];
    dev->calib.dig_T2 = (int16_t)(calib_data[3] << 8) | calib_data[2];
    dev->calib.dig_T3 = (int16_t)(calib_data[5] << 8) | calib_data[4];
    dev->calib.dig_P1 = (uint16_t)(calib_data[7] << 8) | calib_data[6];
    dev->calib.dig_P2 = (int16_t)(calib_data[9] << 8) | calib_data[8];
    dev->calib.dig_P3 = (int16_t)(calib_data[11] << 8) | calib_data[10];
    dev->calib.dig_P4 = (int16_t)(calib_data[13] << 8) | calib_data[12];
    dev->calib.dig_P5 = (int16_t)(calib_data[15] << 8) | calib_data[14];
    dev->calib.dig_P6 = (int16_t)(calib_data[17] << 8) | calib_data[16];
    dev->calib.dig_P7 = (int16_t)(calib_data[19] << 8) | calib_data[18];
    dev->calib.dig_P8 = (int16_t)(calib_data[21] << 8) | calib_data[20];
    dev->calib.dig_P9 = (int16_t)(calib_data[23] << 8) | calib_data[22];

    ESP_LOGI(TAG, "Calibration data read successfully");
    return ESP_OK;
}

esp_err_t bmp280_init(bmp280_dev_t *dev, i2c_port_t i2c_port, uint8_t i2c_addr, SemaphoreHandle_t i2c_mutex)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    dev->i2c_port = i2c_port;
    dev->i2c_addr = i2c_addr;
    dev->i2c_mutex = i2c_mutex;
    dev->t_fine = 0;

    // Take mutex for entire initialization sequence
    bool mutex_taken = false;
    if (dev->i2c_mutex != NULL) {
        if (xSemaphoreTake(dev->i2c_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            mutex_taken = true;
            ESP_LOGI(TAG, "I2C mutex acquired for init");
        } else {
            ESP_LOGW(TAG, "Failed to acquire I2C mutex for init");
            return ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t ret;

    // Read and verify chip ID
    uint8_t chip_id;
    ret = bmp280_read_reg_internal(dev, BMP280_REG_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID");
        goto cleanup;
    }

    if (chip_id != BMP280_CHIP_ID) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02X (expected 0x%02X)", chip_id, BMP280_CHIP_ID);
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    ESP_LOGI(TAG, "BMP280 chip ID verified: 0x%02X", chip_id);

    // Soft reset
    ret = bmp280_write_reg_internal(dev, BMP280_REG_RESET, BMP280_RESET_CMD);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    // Wait for reset to complete (increased delay for stability)
    vTaskDelay(pdMS_TO_TICKS(50));

    // Read calibration data
    uint8_t calib_data[24];
    ret = bmp280_read_reg_internal(dev, BMP280_REG_CALIB_START, calib_data, 24);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data");
        goto cleanup;
    }

    // Parse calibration data
    dev->calib.dig_T1 = (uint16_t)(calib_data[1] << 8) | calib_data[0];
    dev->calib.dig_T2 = (int16_t)(calib_data[3] << 8) | calib_data[2];
    dev->calib.dig_T3 = (int16_t)(calib_data[5] << 8) | calib_data[4];
    dev->calib.dig_P1 = (uint16_t)(calib_data[7] << 8) | calib_data[6];
    dev->calib.dig_P2 = (int16_t)(calib_data[9] << 8) | calib_data[8];
    dev->calib.dig_P3 = (int16_t)(calib_data[11] << 8) | calib_data[10];
    dev->calib.dig_P4 = (int16_t)(calib_data[13] << 8) | calib_data[12];
    dev->calib.dig_P5 = (int16_t)(calib_data[15] << 8) | calib_data[14];
    dev->calib.dig_P6 = (int16_t)(calib_data[17] << 8) | calib_data[16];
    dev->calib.dig_P7 = (int16_t)(calib_data[19] << 8) | calib_data[18];
    dev->calib.dig_P8 = (int16_t)(calib_data[21] << 8) | calib_data[20];
    dev->calib.dig_P9 = (int16_t)(calib_data[23] << 8) | calib_data[22];

    ESP_LOGI(TAG, "Calibration data read successfully");

    // Configure config register (t_sb, filter, spi3w_en=0)
    uint8_t config_val = (BMP280_STANDBY_500_MS << 5) | (BMP280_FILTER_COEFF_16 << 2);
    ret = bmp280_write_reg_internal(dev, BMP280_REG_CONFIG, config_val);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    // Configure ctrl_meas register (osrs_t, osrs_p, mode)
    uint8_t ctrl_meas_val = (BMP280_OVERSAMP_16X << 5) | (BMP280_OVERSAMP_16X << 2) | BMP280_NORMAL_MODE;
    ret = bmp280_write_reg_internal(dev, BMP280_REG_CTRL_MEAS, ctrl_meas_val);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ESP_LOGI(TAG, "BMP280 initialized successfully");

cleanup:
    // Release mutex
    if (mutex_taken && dev->i2c_mutex != NULL) {
        xSemaphoreGive(dev->i2c_mutex);
        ESP_LOGI(TAG, "I2C mutex released after init");
    }

    return ret;
}

esp_err_t bmp280_config(bmp280_dev_t *dev, uint8_t osrs_t, uint8_t osrs_p,
                        uint8_t mode, uint8_t t_sb, uint8_t filter)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Configure config register (t_sb, filter, spi3w_en=0)
    uint8_t config_val = (t_sb << 5) | (filter << 2);
    esp_err_t ret = bmp280_write_reg(dev, BMP280_REG_CONFIG, config_val);
    if (ret != ESP_OK) {
        return ret;
    }

    // Configure ctrl_meas register (osrs_t, osrs_p, mode)
    uint8_t ctrl_meas_val = (osrs_t << 5) | (osrs_p << 2) | mode;
    ret = bmp280_write_reg(dev, BMP280_REG_CTRL_MEAS, ctrl_meas_val);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "BMP280 configured: osrs_t=%d, osrs_p=%d, mode=%d", osrs_t, osrs_p, mode);
    return ESP_OK;
}

esp_err_t bmp280_read_raw(bmp280_dev_t *dev, int32_t *raw_temp, int32_t *raw_press)
{
    if (dev == NULL || raw_temp == NULL || raw_press == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[6];
    esp_err_t ret = bmp280_read_reg(dev, BMP280_REG_PRESS_MSB, data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    // Combine pressure bytes (20-bit)
    *raw_press = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));

    // Combine temperature bytes (20-bit)
    *raw_temp = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));

    return ESP_OK;
}

/**
 * @brief Compensate temperature (from BMP280 datasheet)
 */
static float bmp280_compensate_temperature(bmp280_dev_t *dev, int32_t adc_T)
{
    int32_t var1, var2;

    var1 = ((((adc_T >> 3) - ((int32_t)dev->calib.dig_T1 << 1))) *
            ((int32_t)dev->calib.dig_T2)) >> 11;

    var2 = (((((adc_T >> 4) - ((int32_t)dev->calib.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)dev->calib.dig_T1))) >> 12) *
            ((int32_t)dev->calib.dig_T3)) >> 14;

    dev->t_fine = var1 + var2;

    float T = (dev->t_fine * 5 + 128) >> 8;
    return T / 100.0f;
}

/**
 * @brief Compensate pressure (from BMP280 datasheet)
 */
static float bmp280_compensate_pressure(bmp280_dev_t *dev, int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)dev->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)dev->calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)dev->calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)dev->calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dev->calib.dig_P3) >> 8) +
           ((var1 * (int64_t)dev->calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dev->calib.dig_P1) >> 33;

    if (var1 == 0) {
        return 0; // Avoid division by zero
    }

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)dev->calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dev->calib.dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)dev->calib.dig_P7) << 4);

    return (float)p / 256.0f / 100.0f; // Convert to hPa
}

esp_err_t bmp280_read_temperature(bmp280_dev_t *dev, float *temperature)
{
    if (dev == NULL || temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t raw_temp, raw_press;
    esp_err_t ret = bmp280_read_raw(dev, &raw_temp, &raw_press);
    if (ret != ESP_OK) {
        return ret;
    }

    *temperature = bmp280_compensate_temperature(dev, raw_temp);
    return ESP_OK;
}

esp_err_t bmp280_read_pressure(bmp280_dev_t *dev, float *pressure)
{
    if (dev == NULL || pressure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t raw_temp, raw_press;
    esp_err_t ret = bmp280_read_raw(dev, &raw_temp, &raw_press);
    if (ret != ESP_OK) {
        return ret;
    }

    // Must read temperature first to calculate t_fine
    bmp280_compensate_temperature(dev, raw_temp);
    *pressure = bmp280_compensate_pressure(dev, raw_press);

    return ESP_OK;
}

esp_err_t bmp280_read_data(bmp280_dev_t *dev, float *temperature, float *pressure)
{
    if (dev == NULL || temperature == NULL || pressure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int32_t raw_temp, raw_press;
    esp_err_t ret = bmp280_read_raw(dev, &raw_temp, &raw_press);
    if (ret != ESP_OK) {
        return ret;
    }

    *temperature = bmp280_compensate_temperature(dev, raw_temp);
    *pressure = bmp280_compensate_pressure(dev, raw_press);

    return ESP_OK;
}

float bmp280_calc_altitude(float pressure, float sea_level_pressure)
{
    // Calculate altitude using barometric formula
    return 44330.0f * (1.0f - powf(pressure / sea_level_pressure, 0.1903f));
}
