/*
 * DormMate 主程序
 * 功能：智能宿舍管家主流程 — 单次执行模式
 * 流程：传感器采集 → 上报后端/AI决策 → 执行动作/语音播报 → 深度休眠
 * 
 * 优化点：
 * - 单流程执行模式：上电后执行一次完整流程后休眠，Token 按需消耗
 * - 语音播报优先级队列：预处理(0x0A)和设置确认(0x0B)高优先级立即播报
 * - HTTP 超时 10 秒，失败后本地 fallback
 */

#include "main.h"
#include "app_voice.h"
#include "app_http.h"
#include "app_control.h"
#include "app_nvs.h"
#include "app_oled.h"
#include "app_predict.h"
#include "app_sntp.h"

static const char *TAG = "DormMate";

/* 全局传感器数据（最新一次采集） */
sensor_data_t g_sensors = {0};

/* 后端 URL 和设备 ID（从 NVS 读取） */
char g_server_url[128] = "http://192.168.4.1:5000";
char g_device_id[32] = "dormmate_01";

/* 今日预处理是否已触发（防重复） */
static int preheat_done_today = 0;

/* ====== 主入口 ====== */
void app_main(void)
{
    /* 初始化 NVS（持久化存储：WiFi 密码、偏好设置等） */
    nvs_init();

    /* 从 NVS 读取 WiFi 配置和设备 ID */
    nvs_load_config(g_server_url, g_device_id, sizeof(g_server_url), sizeof(g_device_id));

    /* 初始化 Wi-Fi 并连接 */
    wifi_init_sta();

    /* 同步网络时间（用于预处理时间判断） */
    sntp_init();

    /* 初始化 I2C（OLED）、UART（语音模块）、GPIO（传感器+执行器） */
    oled_init();
    voice_init();
    light_init();
    pir_init();
    dht11_init();
    zhixinger_init();   /* 继电器初始化 */
    zhixingyi_init();   /* 舵机初始化 */

    ESP_LOGI(TAG, "DormMate 启动完成，开始单次执行流程...");

    /* ====== 单次执行主流程 ====== */

    /* 1. 采集传感器数据 */
    read_sensors();
    ESP_LOGI(TAG, "传感器: T=%.1fC H=%.1f%% L=%d PIR=%d",
             g_sensors.temperature, g_sensors.humidity,
             g_sensors.light_raw, g_sensors.human);

    /* 2. 设置 OLED 显示实时数据 */
    char oled_buf1[32], oled_buf2[32];
    snprintf(oled_buf1, sizeof(oled_buf1), "T:%.0fC H:%.0f%%", g_sensors.temperature, g_sensors.humidity);
    snprintf(oled_buf2, sizeof(oled_buf2), "L:%d %s", g_sensors.light_raw,
             g_sensors.human ? "Occupied" : "No One");
    oled_display(oled_buf1, oled_buf2);

    /* 3. 环境异常上报（光线太暗/温度过高 → AI 语音询问） */
    actions_t actions = {0};
    int ret = send_report_to_server("normal", "normal", &actions);
    if (ret == 0) {
        /* 成功后执行 AI 返回的指令 */
        execute_actions(&actions);
    }

    /* 4. 预处理检查（时间触发） */
    if (!preheat_done_today) {
        ret = check_predict_condition();
        if (ret == 0) {
            /* 触发预处理 → 获取 AI 预测结果 */
            ret = send_report_to_server("predict", "predict", &actions);
            if (ret == 0) {
                execute_actions(&actions);
                if (actions.preheat) {
                    preheat_done_today = 1;
                }
            }
        }
    }

    /* 5. 处理语音播报队列（优先级播报） */
    voice_process_queue();

    /* 6. 流程完成 → 深度休眠（等待外部唤醒：PIR 触发或定时器） */
    ESP_LOGI(TAG, "单次流程完成，进入深度休眠...");
    enter_deep_sleep();
}

/* ====== 传感器采集任务 ====== */
void sensor_task(void *pvParameters)
{
    while (1) {
        read_sensors();
        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_DELAY));
    }
}

/* ====== 读取所有传感器数据 ====== */
void read_sensors(void)
{
    /* 读取 DHT11 温湿度 */
    dht11_read(&g_sensors.temperature, &g_sensors.humidity);

    /* 读取光敏传感器 ADC */
    g_sensors.light_raw = light_read_raw();

    /* 读取红外人体传感器 */
    g_sensors.human = pir_read();
}

/* ====== 执行后端返回的动作指令 ====== */
void execute_actions(actions_t *actions)
{
    if (!actions) return;

    /* 执行继电器（灯光/空调） */
    if (actions->relay != g_sensors.relay) {
        control_relay(actions->relay);
        g_sensors.relay = actions->relay;
        ESP_LOGI(TAG, "继电器 %s", actions->relay ? "打开" : "关闭");
    }

    /* 执行蜂鸣器 */
    if (actions->buzzer) {
        control_buzzer(1);
        vTaskDelay(pdMS_TO_TICKS(300));
        control_buzzer(0);
    }

    /* 更新 OLED 显示 */
    if (strlen(actions->oled_line1) > 0 || strlen(actions->oled_line2) > 0) {
        oled_display(actions->oled_line1, actions->oled_line2);
    }

    /* 语音播报（优先级播报） */
    if (actions->voice_notify > 0) {
        int priority = (actions->voice_notify == 10 || actions->voice_notify == 11) ? 1 : 0;
        voice_notify_priority(actions->voice_notify, priority);
    }
}

/* ====== 控制继电器 ====== */
void control_relay(int state)
{
    zhixinger_set(RELAY_PIN, state);
}

/* ====== 控制舵机 ====== */
void control_servo(int angle)
{
    if (angle < 0) return;  /* -1 表示不操作，跳过 */
    zhixingyi_set(angle);
}

/* ====== 控制蜂鸣器 ====== */
void control_buzzer(int state)
{
    gpio_set_level(BUZZER_PIN, state);
}

/* ====== 深度休眠 ====== */
void enter_deep_sleep(void)
{
    /* 短暂延迟确保所有操作完成 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 设置唤醒源：红外传感器 GPIO 上升沿唤醒 */
    esp_sleep_enable_ext0_wakeup(PIR_PIN, 1);

    /* 进入深度休眠 */
    esp_deep_sleep_start();
}
