/**
 * app_control.c — 设备控制模块（继电器/LED/舵机/蜂鸣器控制）
 * 功能：初始化控制GPIO，提供执行设备动作的统一接口
 */

#include "app_control.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "Control";

/* ---------- 引脚定义（与 DormMate_main 实际硬件一致）---------- */
#define RELAY_PIN    GPIO_NUM_18   // 继电器（灯光/空开）
#define BUZZER_PIN   GPIO_NUM_19   // 蜂鸣器
#define SERVO_PIN    GPIO_NUM_21   // 舵机（-1=未使用）

/**
 * 初始化控制GPIO
 * - 将继电器、蜂鸣器设为输出模式，初始低电平（关闭）
 */
void control_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN) | (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // 初始状态：全部关闭
    gpio_set_level(RELAY_PIN, 0);
    gpio_set_level(BUZZER_PIN, 0);

    ESP_LOGI(TAG, "控制GPIO初始化完成：继电器(GPIO%d), 蜂鸣器(GPIO%d)", RELAY_PIN, BUZZER_PIN);
}

/**
 * 执行设备控制动作
 * @param relay   继电器电平（0/1=关/开）
 * @param servo   舵机角度（-1=不操作）
 * @param buzzer  蜂鸣器电平（0/1=关/开）
 */
void control_execute(int relay, int servo, int buzzer)
{
    if (relay == 0 || relay == 1) {
        gpio_set_level(RELAY_PIN, relay);
        ESP_LOGI(TAG, "继电器 -> %s", relay ? "ON" : "OFF");
    }
    if (buzzer == 0 || buzzer == 1) {
        gpio_set_level(BUZZER_PIN, buzzer);
        ESP_LOGI(TAG, "蜂鸣器 -> %s", buzzer ? "ON" : "OFF");
    }
    // 舵机暂不使用
    if (servo >= 0) {
        ESP_LOGW(TAG, "舵机未连接(servo=%d)，跳过", servo);
    }
}

/**
 * 获取当前继电器状态
 * @return 0=关闭, 1=开启
 */
int control_get_relay_state(void)
{
    return gpio_get_level(RELAY_PIN);
}
