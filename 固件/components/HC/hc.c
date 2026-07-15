#include "hc.h"
#include "driver/gpio.h"

static int s_pin = -1;

esp_err_t hc_init(int gpio_pin)
{
    s_pin = gpio_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}

bool hc_is_motion(void)
{
    return gpio_get_level(s_pin) == 1;
}