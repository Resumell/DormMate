#include "main.h"
#include "app_http.h"
#include "app_control.h"
#include "app_oled.h"
#include "app_voice.h"
#include "app_sntp.h"
#include "app_nvs.h"
#include "app_predict.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_adc/adc_oneshot.h"

#define WIFI_SSID      "Resume"
#define WIFI_PASS      "101652Zc."
#define MAX_RETRY      5

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

/* ---------- WiFi 初始化 ---------- */
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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to AP %s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/* ---------- 传感器上报任务 ---------- */
static void sensor_task(void *pvParameters)
{
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));
    
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &chan_config));
    
    SensorData_t data;
    ActionData_t action;
    int loop_count = 0;
    static int dark_triggered = 0;
    
    static int recorded_today = 0;
    static char last_record_date[16] = {0};

    while (1) {
        loop_count++;
        memset(&data, 0, sizeof(data)); 
        memset(&action, 0, sizeof(ActionData_t));
        action.servo = -1;

        /* 读光敏 */
        int light_val = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &light_val);
        ESP_LOGI(TAG, "[第%d次] 光敏ADC值: %d", loop_count, light_val);

        /* 先给 data 填默认值 */
        data.light = light_val;
        data.temperature = 26.5;
        data.humidity = 60.0;
        data.current = 0.5;
        data.relay = 0;
        data.servo = -1;

        /* 本地光敏触发语音询问 */
        if (light_val < 500 && !dark_triggered) {
            voice_send_cmd(0x01);   // 如果平台上 light_in 消息号改了，这里同步改
            dark_triggered = 1;
            ESP_LOGI(TAG, "光线过暗，触发语音询问");
        } else if (light_val >= 500) {
            dark_triggered = 0;
        }

        /* 每日首次回寝记录 */
        char today_date[16];
        get_date_str(today_date, sizeof(today_date));
        if (strcmp(today_date, last_record_date) != 0) {
            recorded_today = 0;
            strcpy(last_record_date, today_date);
        }
        
        data.human = 1;  // TODO: 接入红外后改真实值
        
        if (data.human == 1 && !recorded_today) {
            char now_str[6];
            get_time_str(now_str, sizeof(now_str));
            nvs_save_return_time(now_str);
            recorded_today = 1;
            
            /* ===== home_in：红外检测回寝，直接开灯 + 语音播报（不经过AI）===== */
            ActionData_t home_action = {0};
            home_action.servo = -1;
            home_action.relay = 1;   // 直接开灯
            strncpy(home_action.oled_line1, "Home: Light ON", sizeof(home_action.oled_line1)-1);
            execute_action(&home_action);
            voice_send_cmd(0x09);   // 发 0x09 给语音模块，让它播"回家快乐"
            ESP_LOGI(TAG, ">>> 红外检测回寝，直接开灯 + 语音播报");
            /* ============================================================ */
            
            /* 上报首次回寝事件（只存数据库，不走AI决策） */
            SensorData_t event_data = {0};
            event_data.human = 1;
            event_data.temperature = data.temperature;
            event_data.humidity = data.humidity;
            strcpy(event_data.event, "human_first");
            strcpy(event_data.date, today_date);
            strcpy(event_data.first_seen_time, now_str);
            event_data.weekday = get_weekday();
            
            ActionData_t event_action = {0};
            event_action.servo = -1;
            http_report(&event_data, &event_action);
            
            ESP_LOGI(TAG, ">>> 今日首次回寝: %s", now_str);
        }

        /* 普通上报后端 */
        http_report(&data, &action);
        execute_action(&action);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}   // ← 补上这行！原来丢了
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
    oled_init();

    ESP_LOGI(TAG, "开始连接WiFi...");
    wifi_init_sta();
    
    /* ===== 场景三：SNTP 时间同步 ===== */
    sntp_init();
    /* ================================= */
    
    voice_init();
    xTaskCreate(voice_task, "voice_task", 4096, NULL, 5, NULL);
    
    /* ===== 场景三：启动预测任务 ===== */
    xTaskCreate(predict_task, "predict_task", 8192, NULL, 4, NULL);
    /* =============================== */

    xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
}