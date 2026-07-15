#include "main.h"
#include "app_http.h"
#include "app_control.h"
#include "oled.h"
#include "esp_adc/adc_oneshot.h"
#include "app_voice.h"
#include "app_sntp.h"
#include "app_nvs.h"
#include "app_predict.h"
#include "dht11.h"
#include "light.h"
#include "hc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define WIFI_SSID      "Resume"
#define WIFI_PASS      "101652Zc."   /* 首次烧录默认值，之后从NVS读取，不再使用明文 */
#define MAX_RETRY      5

/* ========== 全局传感器缓存 ========== */
volatile float g_temperature = 26.5;
volatile float g_humidity   = 60.0;
volatile int   g_light      = 0;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static const char* TAG = "MAIN";
static int s_retry_num = 0;

/* ---------- WiFi 事件处理 ---------- */
static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ---------- WiFi 初始化（从NVS读取，首次回退默认值） ---------- */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    /* ===== 从 NVS 读取 WiFi 凭证，首次运行回退到默认值并写入NVS ===== */
    char wifi_ssid[32] = {0};
    char wifi_pass[64] = {0};
    
    if (!nvs_get_wifi_creds(wifi_ssid, sizeof(wifi_ssid), wifi_pass, sizeof(wifi_pass))) {
        strncpy(wifi_ssid, WIFI_SSID, sizeof(wifi_ssid) - 1);
        strncpy(wifi_pass, WIFI_PASS, sizeof(wifi_pass) - 1);
        nvs_save_wifi_creds(wifi_ssid, wifi_pass);
        ESP_LOGI(TAG, "首次运行，WiFi凭证已加密写入NVS，后续不再使用明文宏");
    }

    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    strncpy((char*)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, wifi_pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to AP %s", wifi_ssid);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", wifi_ssid);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/* ---------- 传感器上报任务 ---------- */
static void sensor_task(void *pvParameters)
{
    int sntp_wait = 0;
    while (!app_sntp_is_synced() && sntp_wait < 15) {
        ESP_LOGW(TAG, "等待SNTP时间同步... (%d/15)", sntp_wait);
        vTaskDelay(pdMS_TO_TICKS(1000));
        sntp_wait++;
    }
    if (!app_sntp_is_synced()) {
        ESP_LOGW(TAG, "SNTP同步失败，传感器任务继续但时间可能不准确");
    } else {
        ESP_LOGI(TAG, "SNTP已同步，传感器任务开始");
    }

    ESP_ERROR_CHECK(light_init(ADC_CHANNEL_0));
    ESP_ERROR_CHECK(dht11_init(6));
    ESP_ERROR_CHECK(hc_init(7));

    SensorData_t data;
    ActionData_t action;
    int loop_count = 0;
    static int dark_triggered = 0;
    static int hot_triggered = 0;
    static int recorded_today = 0;
    static char last_record_date[16] = {0};
    static int last_human_raw = 0;
    static uint32_t last_rainbow_tick = 0;  /* 彩虹灯冷却计时 */

    nvs_get_last_record_date(last_record_date, sizeof(last_record_date));
    char today_date[16];
    get_date_str(today_date, sizeof(today_date));
    if (strcmp(today_date, last_record_date) == 0) {
        recorded_today = 1;
        ESP_LOGI(TAG, "NVS恢复：今日(%s)已记录回寝", today_date);
    } else {
        recorded_today = 0;
        ESP_LOGI(TAG, "NVS恢复：今日(%s)未记录回寝", today_date);
    }

    while (1) {
        loop_count++;

        /* ===== 1. 传感器读取（每 200ms） ===== */
        int light_val = 0;
        light_get_adc(&light_val);
        data.light = light_val;
        g_light = light_val;

        dht11_data_t dht;
        esp_err_t dht_ret = dht11_read(&dht);
        if (dht_ret == ESP_OK) {
            data.temperature = dht.temperature;
            data.humidity = dht.humidity;
            g_temperature = dht.temperature;
            g_humidity = dht.humidity;
        } else {
            data.temperature = g_temperature;
            data.humidity = g_humidity;
        }

        int human_raw = hc_is_motion() ? 1 : 0;
        data.human = human_raw;

        /* ===== 2. 红外逻辑（每 200ms，极灵敏） ===== */
        if (human_raw == 1 && last_human_raw == 0) {
            /* 上升沿：必闪 */
            board_led_rainbow_cycle();
            last_rainbow_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, ">>> 红外上升沿，彩虹欢迎");

            /* 18:00 之后且今日首次：语音 + 入库 */
            char now_str[6];
            get_time_str(now_str, sizeof(now_str));
            int current_hour = 0;
            sscanf(now_str, "%d:", &current_hour);

            if (current_hour >= 18 && !recorded_today) {
                nvs_save_return_time(now_str);
                recorded_today = 1;

                SensorData_t ai_data = {0};
                ai_data.light = light_val;
                ai_data.human = 1;
                ai_data.temperature = data.temperature;
                ai_data.humidity = data.humidity;
                ai_data.current = 0.5;
                ai_data.relay = 0;
                ai_data.ac_relay = 0;
                ai_data.servo = -1;
                strcpy(ai_data.event, "human_return");

                ActionData_t ai_action = {0};
                ai_action.servo = -1;
                ai_action.relay = -1;
                ai_action.ac_relay = -1;
                http_report(&ai_data, &ai_action);
                execute_action(&ai_action);

                voice_send_cmd(0x09);
                ESP_LOGI(TAG, ">>> 晚间首次回寝: %s，语音'回家快乐哦'", now_str);

                SensorData_t event_data = {0};
                event_data.human = 1;
                event_data.temperature = data.temperature;
                event_data.humidity = data.humidity;
                strcpy(event_data.event, "human_first");
                strcpy(event_data.source, "infrared");
                strcpy(event_data.date, today_date);
                strcpy(event_data.first_seen_time, now_str);
                event_data.weekday = get_weekday();

                ActionData_t event_action = {0};
                event_action.servo = -1;
                event_action.relay = -1;
                event_action.ac_relay = -1;
                http_report(&event_data, &event_action);
            } else if (current_hour < 18) {
                ESP_LOGI(TAG, "当前 %s（白天），红外只闪灯，不记录", now_str);
            } else {
                ESP_LOGI(TAG, "当前 %s，晚间但今日已记录，只闪灯", now_str);
            }
        } else if (human_raw == 1 && (xTaskGetTickCount() - last_rainbow_tick) > pdMS_TO_TICKS(2000)) {
            /* 持续有人：每 2 秒再闪一次（解决封锁期"没反应"） */
            board_led_rainbow_cycle();
            last_rainbow_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, ">>> 红外持续有人，彩虹欢迎");
        }
        last_human_raw = human_raw;

        /* ===== 3. 慢任务（每 20 次 ≈ 4 秒） ===== */
        if (loop_count % 5 == 0) {
            memset(&data, 0, sizeof(data));
            memset(&action, 0, sizeof(ActionData_t));
            action.servo = -1;
            action.relay = -1;
            action.ac_relay = -1;

            data.light = light_val;
            data.temperature = g_temperature;
            data.humidity = g_humidity;
            data.human = human_raw;
            data.current = 0.5;
            data.relay = lamp_get_level();
            data.ac_relay = ac_get_level();
            data.servo = -1;

            /* OLED 常驻状态 */
            {
                char time_now[6];
                get_time_str(time_now, sizeof(time_now));
                char oled_buf[4][32];
                snprintf(oled_buf[0], sizeof(oled_buf[0]), "T:%.0fC H:%.0f%%", data.temperature, data.humidity);
                snprintf(oled_buf[1], sizeof(oled_buf[1]), "L:%d IR:%s", light_val, human_raw ? "ON" : "OFF");
                snprintf(oled_buf[2], sizeof(oled_buf[2]), "Lamp:%s AC:%s",
                         data.relay ? "ON" : "OFF", data.ac_relay ? "ON" : "OFF");
                snprintf(oled_buf[3], sizeof(oled_buf[3]), "%s", time_now);

                oled_clear();
                oled_show_string(0, oled_buf[0]);
                oled_show_string(1, oled_buf[1]);
                oled_show_string(2, oled_buf[2]);
                oled_show_string(3, oled_buf[3]);
                oled_refresh();
            }

            /* 跨天重置 */
            get_date_str(today_date, sizeof(today_date));
            if (strcmp(today_date, last_record_date) != 0) {
                recorded_today = 0;
                strcpy(last_record_date, today_date);
                ESP_LOGI(TAG, "日期已更新: %s，等待今日首次回寝", today_date);
            }

            /* HTTP 普通上报 + AI 决策 */
            http_report(&data, &action);
            execute_action(&action);

            /* 预处理语音 */
            if (action.preheat == 1) {
                voice_send_cmd(0x0A);
                ESP_LOGI(TAG, ">>> 预处理触发，语音播报 0x0A");
            }

            /* AI 环境询问语音 */
            if (action.voice_notify == 1 && !dark_triggered) {
                voice_send_cmd(0x01);
                dark_triggered = 1;
                ESP_LOGI(TAG, "AI决策：光线过暗，语音询问是否开灯");
            } else if (action.voice_notify == 4 && !hot_triggered) {
                voice_send_cmd(0x04);
                hot_triggered = 1;
                ESP_LOGI(TAG, "AI决策：温度过高，语音询问是否开空调");
            } else if (action.voice_notify == 11) {
                voice_send_cmd(0x0B);
                ESP_LOGI(TAG, "偏好修改：语音播报已修改预处理时间");
            }

            /* 环境恢复后重置询问标志 */
            if (light_val < 2000) dark_triggered = 0;
            if (data.temperature < 26.0) hot_triggered = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(200));  /* 核心：从 4000ms 改为 200ms */
    }
}

/* ---------- 主入口 ---------- */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    control_init();
    oled_init(4, 5, 0x3C);

    ESP_LOGI(TAG, "开始连接WiFi...");
    wifi_init_sta();
    
    app_sntp_init();
    
    voice_init();
    xTaskCreate(voice_task, "voice_task", 4096, NULL, 5, NULL);
    
    xTaskCreate(predict_task, "predict_task", 8192, NULL, 4, NULL);

    xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
}