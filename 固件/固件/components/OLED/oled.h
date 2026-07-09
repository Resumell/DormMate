#ifndef OLED_H
#define OLED_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the SSD1306 OLED display via I2C
 * @param sda_pin GPIO pin for I2C SDA
 * @param scl_pin GPIO pin for I2C SCL
 * @param i2c_addr I2C address of SSD1306 (usually 0x3C)
 * @return ESP_OK on success
 */
esp_err_t oled_init(int sda_pin, int scl_pin, uint8_t i2c_addr);

/**
 * @brief Clear the entire display buffer
 */
void oled_clear(void);

/**
 * @brief Show a string at the specified line (0-7)
 * @param line Line number (0-7)
 * @param str String to display (null-terminated)
 */
void oled_show_string(uint8_t line, const char *str);

/**
 * @brief Refresh the display (flush buffer to OLED)
 * @return ESP_OK on success
 */
esp_err_t oled_refresh(void);

/**
 * @brief Fill the display buffer (set all pixels on)
 */
void oled_fill(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_H */
