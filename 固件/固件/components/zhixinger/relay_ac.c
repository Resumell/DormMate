#include "relay_ac.h"
#include "driver/gpio.h"
#include <stdio.h>

static esp_err_t gpio_output_init(int gpio_pin)
{
    gpio_reset_pin(gpio_pin);
    gpio_set_direction(gpio_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_pin, 0);
    return ESP_OK;
}

esp_err_t relay_ac_init(void)
{
    esp_err_t ret = gpio_output_init(RELAY_AC_PIN);
    if (ret != ESP_OK) {
        printf("空调灯继电器初始化失败\n");
        return ret;
    }
    printf("空调灯继电器就绪 (GPIO%d)\n", RELAY_AC_PIN);
    return ESP_OK;
}

void relay_ac_on(void)
{
    gpio_set_level(RELAY_AC_PIN, RELAY_ON);
    printf("空调灯: ON\n");
}

void relay_ac_off(void)
{
    gpio_set_level(RELAY_AC_PIN, RELAY_OFF);
    printf("空调灯: OFF\n");
}

void relay_ac_toggle(void)
{
    int current = gpio_get_level(RELAY_AC_PIN);
    int new_state = current ? RELAY_OFF : RELAY_ON;
    gpio_set_level(RELAY_AC_PIN, new_state);
    printf("空调灯: %s\n", new_state ? "ON" : "OFF");
}

int relay_ac_get(void)
{
    return gpio_get_level(RELAY_AC_PIN);
}