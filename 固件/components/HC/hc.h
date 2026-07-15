#ifndef HC_H
#define HC_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize HC-SR501 PIR motion sensor
 * @param gpio_pin GPIO pin connected to sensor OUT
 * @return ESP_OK on success
 */
esp_err_t hc_init(int gpio_pin);

/**
 * @brief Check if human motion is detected
 * @return true if motion detected (HIGH), false otherwise
 */
bool hc_is_motion(void);

#ifdef __cplusplus
}
#endif

#endif /* HC_H */