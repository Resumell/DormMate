#include "app_control.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"

#define RELAY_GPIO 5

static led_strip_handle_t led_strip = NULL;
static const char *TAG = "CTRL";

void control_init(void)
{
    /* RGB LED 初始化（你板子上的那颗灯） */
    led_strip_config_t strip_config = {
        .strip_gpio_num = 38,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));

    /* 继电器初始化 */
    gpio_reset_pin(RELAY_GPIO);
    gpio_set_direction(RELAY_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_GPIO, 0);

    ESP_LOGI(TAG, "控制模块初始化完成");
}

void execute_action(ActionData_t *action)
{
    /* 继电器 + 蓝灯 */
    if (action->relay == 1) {
        gpio_set_level(RELAY_GPIO, 1);
        led_strip_set_pixel(led_strip, 0, 0, 30, 60);  // 蓝灯亮
        led_strip_refresh(led_strip);
        ESP_LOGI(TAG, ">>> 执行: 开灯");
    } else if (action->relay == 0) {
        gpio_set_level(RELAY_GPIO, 0);
        led_strip_clear(led_strip);
        ESP_LOGI(TAG, ">>> 执行: 关灯");
    }
    
    /* 舵机（A 还没交代码，先打印） */
    if (action->servo >= 0) {
        ESP_LOGI(TAG, ">>> 舵机: 转到 %d 度（等A的驱动）", action->servo);
    }
    
    /* 语音（模块还没接线，先打印） */
    if (strlen(action->tts) > 0) {
        ESP_LOGI(TAG, ">>> 喇叭应该说: %s", action->tts);
    }
}