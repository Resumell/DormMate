#ifndef LIGHT_H
#define LIGHT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the photoresistor ADC
 * @param adc_channel ADC channel (e.g., ADC_CHANNEL_0 for GPIO1)
 * @return ESP_OK on success
 */
esp_err_t light_init(int adc_channel);

/**
 * @brief Read raw ADC value from photoresistor
 * @param adc_val Pointer to store the ADC reading (0-4095)
 * @return ESP_OK on success
 */
esp_err_t light_get_adc(int *adc_val);

/**
 * @brief Read voltage from photoresistor (in mV)
 * @param voltage_mv Pointer to store the voltage in millivolts
 * @return ESP_OK on success
 */
esp_err_t light_get_voltage(int *voltage_mv);

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_H */
