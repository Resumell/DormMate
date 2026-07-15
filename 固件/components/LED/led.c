#include "led.h"
#include "driver/ledc.h"
#include "esp_log.h"

static int s_r_pin = -1;
static int s_g_pin = -1;
static int s_b_pin = -1;

/* LEDC PWM 配置：固定低亮度，不刺眼 */
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ    5000
#define LEDC_RES        LEDC_TIMER_13_BIT
#define LEDC_R_CH       LEDC_CHANNEL_0
#define LEDC_G_CH       LEDC_CHANNEL_1
#define LEDC_B_CH       LEDC_CHANNEL_2
#define LEDC_DUTY       2200   /* 13bit 下约 27%，柔和不刺眼 */

esp_err_t led_init(int r_pin, int g_pin, int b_pin)
{
    s_r_pin = r_pin;
    s_g_pin = g_pin;
    s_b_pin = b_pin;

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&timer);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t r_ch = {
        .gpio_num = r_pin,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_R_CH,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&r_ch);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t g_ch = {
        .gpio_num = g_pin,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_G_CH,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&g_ch);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t b_ch = {
        .gpio_num = b_pin,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_B_CH,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&b_ch);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

void led_set(bool r, bool g, bool b)
{
    ledc_set_duty(LEDC_MODE, LEDC_R_CH, r ? LEDC_DUTY : 0);
    ledc_update_duty(LEDC_MODE, LEDC_R_CH);
    ledc_set_duty(LEDC_MODE, LEDC_G_CH, g ? LEDC_DUTY : 0);
    ledc_update_duty(LEDC_MODE, LEDC_G_CH);
    ledc_set_duty(LEDC_MODE, LEDC_B_CH, b ? LEDC_DUTY : 0);
    ledc_update_duty(LEDC_MODE, LEDC_B_CH);
}

void led_off(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_R_CH, 0);
    ledc_update_duty(LEDC_MODE, LEDC_R_CH);
    ledc_set_duty(LEDC_MODE, LEDC_G_CH, 0);
    ledc_update_duty(LEDC_MODE, LEDC_G_CH);
    ledc_set_duty(LEDC_MODE, LEDC_B_CH, 0);
    ledc_update_duty(LEDC_MODE, LEDC_B_CH);
}