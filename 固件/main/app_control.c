#include "app_control.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"
#include "app_oled.h" 

#define RELAY_GPIO 5

static led_strip_handle_t led_strip = NULL;
static const char *TAG = "CTRL";

/* ========== 占位函数（weak：现在能编译，等正式实现来了自动覆盖）========== */
__attribute__((weak)) void servo_set(int angle)
{
    ESP_LOGW(TAG, "【占位】舵机驱动未接入，目标角度=%d（等A交代码）", angle);
}

__attribute__((weak)) void buzzer_set(int on_off)
{
    ESP_LOGW(TAG, "【占位】蜂鸣器驱动未接入，状态=%d（等A交代码）", on_off);
}
/*app_oled.c 已经有真实实现了
__attribute__((weak)) void oled_show(const char* line1, const char* line2)
{
    ESP_LOGW(TAG, "【占位】OLED驱动未接入，line1=%s, line2=%s（你自己写I2C）",
             line1 && strlen(line1) ? line1 : "(空)",
             line2 && strlen(line2) ? line2 : "(空)");
}
*/
__attribute__((weak)) void voice_speak(const char* text)
{
    ESP_LOGW(TAG, "【占位】语音TTS未接入，内容=%s（你自己接火山TTS+功放）",
             text && strlen(text) ? text : "(空)");
}

void control_init(void)
{
    /* RGB LED 初始化 */
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
    /* 1. 继电器 + RGB状态灯（你已会，现在就能干活） */
    if (action->relay == 1) {
        gpio_set_level(RELAY_GPIO, 1);
        led_strip_set_pixel(led_strip, 0, 0, 30, 60);
        led_strip_refresh(led_strip);
        ESP_LOGI(TAG, ">>> 执行: 开灯");
    } else if (action->relay == 0) {
        gpio_set_level(RELAY_GPIO, 0);
        led_strip_clear(led_strip);
        ESP_LOGI(TAG, ">>> 执行: 关灯");
    }
    
    /* 2. 舵机（A 提供 servo_set，-1 表示不操作） */
    if (action->servo >= 0) {
        servo_set(action->servo);
    }
    
    /* 3. 蜂鸣器（A 提供 buzzer_set，1=响，0=停） */
    if (action->buzzer == 1) {
        buzzer_set(1);
    } else if (action->buzzer == 0) {
        buzzer_set(0);
    }
    
    /* 4. TTS 语音（你自己写 voice_speak，空字符串表示不播报） */
    if (strlen(action->tts) > 0) {
        voice_speak(action->tts);
    }
    
    /* 5. OLED 显示（你自己写 oled_show，空字符串表示不更新） */
    if (strlen(action->oled_line1) > 0 || strlen(action->oled_line2) > 0) {
        oled_show(action->oled_line1, action->oled_line2);
    }
}