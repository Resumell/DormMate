#ifndef DHT11_H
#define DHT11_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DHT11 sensor data structure
 */
typedef struct {
    int temperature;    /**< Temperature in Celsius */
    int humidity;       /**< Humidity in percent */
} dht11_data_t;

/**
 * @brief Initialize DHT11 sensor GPIO
 * @param gpio_pin GPIO pin connected to DHT11 DATA line
 * @return ESP_OK on success
 */
esp_err_t dht11_init(int gpio_pin);

/**
 * @brief Read temperature and humidity from DHT11
 * @param data Pointer to store the sensor reading
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on failure
 */
esp_err_t dht11_read(dht11_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* DHT11_H */
