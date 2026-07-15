#ifndef LED_H
#define LED_H
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize RGB LED (common cathode)
 * @param r_pin GPIO for Red channel
 * @param g_pin GPIO for Green channel
 * @param b_pin GPIO for Blue channel
 * @return ESP_OK on success
 */
esp_err_t led_init(int r_pin, int g_pin, int b_pin);

/**
 * @brief Set RGB LED state
 * @param r Red on/off
 * @param g Green on/off
 * @param b Blue on/off
 */
void led_set(bool r, bool g, bool b);

/**
 * @brief Turn all channels off
 */
void led_off(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_H */