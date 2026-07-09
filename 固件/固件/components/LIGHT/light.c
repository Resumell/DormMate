#include "light.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define LIGHT_ADC_UNIT    ADC_UNIT_1

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static int s_adc_channel = -1;
static bool s_calibrated = false;

esp_err_t light_init(int adc_channel)
{
    s_adc_channel = adc_channel;

    // ADC1 init
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = LIGHT_ADC_UNIT,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc_handle);
    if (ret != ESP_OK) return ret;

    // ADC channel config
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(adc_handle, adc_channel, &chan_cfg);
    if (ret != ESP_OK) return ret;

    // Calibration
    adc_cali_curve_fitting_config_t cali_config = {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
};
ret=adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        s_calibrated = true;
    }
    // Calibration failure is not fatal - we will use raw values

    return ESP_OK;
}

esp_err_t light_get_adc(int *adc_val)
{
    return adc_oneshot_read(adc_handle, s_adc_channel, adc_val);
}

esp_err_t light_get_voltage(int *voltage_mv)
{
    int raw;
    esp_err_t ret = adc_oneshot_read(adc_handle, s_adc_channel, &raw);
    if (ret != ESP_OK) return ret;

    if (s_calibrated) {
        ret = adc_cali_raw_to_voltage(cali_handle, raw, voltage_mv);
    } else {
        // Approximate: 12-bit, 0-3.1V range
        *voltage_mv = (int)((float)raw / 4095.0f * 3100.0f);
    }
    return ret;
}
