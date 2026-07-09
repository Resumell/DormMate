#include "dht11.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DHT11_TIMEOUT_US  1000

static int s_pin = -1;

esp_err_t dht11_init(int gpio_pin)
{
    s_pin = gpio_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(gpio_pin, 1);

    /* Let DHT11 settle after power-up */
    vTaskDelay(pdMS_TO_TICKS(1500));

    return ESP_OK;
}

/* Wait while pin == level, return elapsed us or -1 on timeout */
static int64_t wait_while(int pin, int level, int64_t timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) == level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return -1;
        }
    }
    return esp_timer_get_time() - start;
}

esp_err_t dht11_read(dht11_data_t *data)
{
    uint8_t bits[5] = {0};

    /* ---- Step 1: Send start signal ---- */
    gpio_set_direction(s_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(s_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(s_pin, 1);
    esp_rom_delay_us(40);

    /* ---- Step 2: Switch to input, wait for DHT11 response ---- */
    gpio_set_direction(s_pin, GPIO_MODE_INPUT);
    esp_rom_delay_us(10);

    /* DHT11 should pull low for 80us */
    if (wait_while(s_pin, 1, 100) < 0)
        return ESP_ERR_TIMEOUT;
    /* DHT11 low -> high (80us low done) */
    if (wait_while(s_pin, 0, 100) < 0)
        return ESP_ERR_TIMEOUT;
    /* DHT11 high -> low (80us high done) */
    if (wait_while(s_pin, 1, 100) < 0)
        return ESP_ERR_TIMEOUT;

    /* ---- Step 3: Read 40 bits ---- */
    for (int i = 0; i < 40; i++) {
        /* Wait for bit start (line low) */
        if (wait_while(s_pin, 0, 80) < 0)
            return ESP_ERR_TIMEOUT;

        /* Line is now high. Measure high duration:
         * ~26us = bit 0, ~70us = bit 1 */
        int64_t high_us = wait_while(s_pin, 1, 80);
        if (high_us < 0)
            return ESP_ERR_TIMEOUT;

        if (high_us > 45) {
            bits[i / 8] |= (uint8_t)(1 << (7 - (i % 8)));
        }
    }

    /* ---- Step 4: Checksum ---- */
    uint8_t sum = (uint8_t)(bits[0] + bits[1] + bits[2] + bits[3]);
    if (sum != bits[4])
        return ESP_ERR_INVALID_CRC;

    data->humidity    = bits[0];
    data->temperature = bits[2];

    /* Pull high for next read */
    gpio_set_direction(s_pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_pin, 1);

    return ESP_OK;
}
