#include "app_control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "led_strip.h"
#include "esp_log.h"
#include "oled.h"
#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LAMP_R_PIN      2
#define LAMP_G_PIN      3
#define LAMP_B_PIN      14

#define AC_R_PIN        10
#define AC_G_PIN        13
#define AC_B_PIN        15

#define AC_R_CH         LEDC_CHANNEL_3
#define AC_G_CH         LEDC_CHANNEL_4
#define AC_B_CH         LEDC_CHANNEL_5

static led_strip_handle_t led_strip = NULL;
static const char *TAG = "CTRL";

static int lamp_state = 0;
static int ac_state = 0;

__attribute__((weak)) void servo_set(int angle)
{
    ESP_LOGW(TAG, "【占位】舵机驱动未接入，目标角度=%d（等A交代码）", angle);
}

__attribute__((weak)) void buzzer_set(int on_off)
{
    ESP_LOGW(TAG, "【占位】蜂鸣器驱动未接入，状态=%d（等A交代码）", on_off);
}

__attribute__((weak)) void voice_speak(const char* text)
{
    ESP_LOGW(TAG, "【占位】语音TTS未接入，内容=%s（你自己接火山TTS+功放）",
             text && strlen(text) ? text : "(空)");
}

static void lamp_set_status(int status)
{
    switch (status) {
        case 0:  led_set(true,  false, true);  lamp_state = 0;  break;
        case 1:  led_set(false, true,  true);  lamp_state = 1;  break;
        default: led_set(true,  false, true);  lamp_state = 0;  break;
    }
}

static void ac_set_status(int status)
{
    uint32_t duty = 2200;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, AC_R_CH, (status == 0) ? duty : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, AC_R_CH);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, AC_G_CH, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, AC_G_CH);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, AC_B_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, AC_B_CH);
    ac_state = status;
    ESP_LOGI(TAG, "空调灯状态=%d (%s)", status, status ? "浅蓝" : "粉色");
}

void board_led_rainbow_cycle(void)
{
    if (!led_strip) return;

    uint8_t colors[][3] = {
        {40, 0,  0}, {40, 20, 0}, {40, 40, 0}, {0,  40, 0},
        {0,  40, 20}, {0,  0,  40}, {20, 0,  40}, {20, 20, 20}
    };

    for (int i = 0; i < 8; i++) {
        led_strip_set_pixel(led_strip, 0, colors[i][0], colors[i][1], colors[i][2]);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(125));
    }

    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

void control_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = 38,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_ERROR_CHECK(led_strip_clear(led_strip));

    ESP_ERROR_CHECK(led_init(LAMP_R_PIN, LAMP_G_PIN, LAMP_B_PIN));
    lamp_set_status(0);

    ledc_channel_config_t ac_r_ch = {
        .gpio_num = AC_R_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = AC_R_CH,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ac_r_ch));

    ledc_channel_config_t ac_g_ch = {
        .gpio_num = AC_G_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = AC_G_CH,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ac_g_ch));

    ledc_channel_config_t ac_b_ch = {
        .gpio_num = AC_B_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = AC_B_CH,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ac_b_ch));

    ac_set_status(0);

    ESP_LOGI(TAG, "控制模块初始化完成（台灯RGB=2/3/14，空调RGB=10/13/15，无继电器）");
}

void execute_action(ActionData_t *action)
{
    /* 台灯：只有明确收到 0 或 1 才操作，-1 无视 */
    if (action->relay == 1) {
        lamp_set_status(1);
        if (action->manual == 1) {
            led_strip_set_pixel(led_strip, 0, 0, 30, 60);
            led_strip_refresh(led_strip);
        }
        ESP_LOGI(TAG, ">>> 执行: 台灯开");
    } else if (action->relay == 0) {
        lamp_set_status(0);
        if (action->manual == 1) {
            led_strip_clear(led_strip);
            led_strip_refresh(led_strip);
        }
        ESP_LOGI(TAG, ">>> 执行: 台灯关");
    }

    /* 空调灯：只有明确收到 0 或 1 才操作，-1 无视 */
    if (action->ac_relay == 1) {
        ac_set_status(1);
        ESP_LOGI(TAG, ">>> 执行: 空调开");
    } else if (action->ac_relay == 0) {
        ac_set_status(0);
        ESP_LOGI(TAG, ">>> 执行: 空调关");
    }

    if (action->servo >= 0) {
        servo_set(action->servo);
    }
    
    if (action->buzzer == 1) {
        buzzer_set(1);
    } else if (action->buzzer == 0) {
        buzzer_set(0);
    }
    
    if (strlen(action->tts) > 0) {
        voice_speak(action->tts);
    }
    
    if (strlen(action->oled_line1) > 0 || strlen(action->oled_line2) > 0) {
        oled_clear();
        if (strlen(action->oled_line1) > 0) {
            oled_show_string(0, action->oled_line1);
        }
        if (strlen(action->oled_line2) > 0) {
            oled_show_string(2, action->oled_line2);
        }
        oled_refresh();
    }
}

int lamp_get_level(void)
{
    return lamp_state;
}

int ac_get_level(void)
{
    return ac_state;
}