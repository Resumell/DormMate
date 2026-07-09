#include "pir.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdio.h>

/* 静态变量 */
static int s_count = 0;
static int s_state = 0;
static int64_t s_last_us = 0;
static int64_t s_first_us = 0;

/* 中断服务函数 */
static void IRAM_ATTR pir_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();

    if (s_last_us != 0 && (now - s_last_us) < 300000) {
        return;
    }
    s_last_us = now;
    s_state = 1;
    s_count++;

    if (s_first_us == 0) {
        s_first_us = now;
    }

    printf("[PIR] 检测到人! 计数: %d\n", s_count);
}

/* 公有函数实现 */
esp_err_t pir_init(int gpio_pin)
{
    esp_err_t ret;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = gpio_isr_handler_add(gpio_pin, pir_isr_handler, NULL);
    if (ret != ESP_OK) return ret;

    printf("PIR传感器就绪 (GPIO%d) 中断模式\n", gpio_pin);
    return ESP_OK;
}

int pir_get_count(void)
{
    return s_count;
}

int pir_get_state(void)
{
    return s_state;
}

int64_t pir_get_first_sec(void)
{
    if (s_first_us == 0) return -1;
    int64_t now = esp_timer_get_time();
    return (now - s_first_us) / 1000000;
}

void pir_reset(void)
{
    s_count = 0;
    s_state = 0;
    s_last_us = 0;
    s_first_us = 0;
    printf("PIR计数已重置\n");
}
