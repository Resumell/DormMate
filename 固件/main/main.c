#include "main.h"
#include "app_http.h"
#include "app_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_adc/adc_oneshot.h"

/* WiFi 配置（你之前的热点） */
#define WIFI_SSID      "Resume"
#define WIFI_PASS      "101652Zc."
#define MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static const char* TAG = "MAIN";
static int s_retry_num = 0;

/* ---------- WiFi 事件处理（从你之前的代码复制） ---------- */
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

/* ---------- 传感器上报任务（每 10 秒一次） ---------- */
static void sensor_task(void *pvParameters)
{
    /* ADC 初始化（读光敏，GPIO4 = ADC1_CH3） */
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {.unit_id = ADC_UNIT_1};
    adc_oneshot_new_unit(&init_cfg, &adc1_handle);
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &chan_cfg);

    SensorData_t data;
    ActionData_t action;
    int loop_count = 0;

    while (1) {
        loop_count++;
        
        /* 每次循环先清空指令结构体，防止垃圾数据 */
        memset(&action, 0, sizeof(ActionData_t));
        action.servo = -1;   // 默认不动

        /* 读光敏 */
        int light_val = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &light_val);
        ESP_LOGI(TAG, "[第%d次] 光敏ADC值: %d", loop_count, light_val);

        /* 填充数据（光敏是真的，其他是假的，等A交代码） */
        data.light = light_val;
        data.human = 1;           // 假数据
        data.temperature = 26.5;  // 假数据
        data.humidity = 60.0;     // 假数据
        data.current = 0.5;       // 假数据
        data.relay = 0;
        data.servo = -1;

        /* 上报后端 + 收 AI 指令 */
        http_report(&data, &action);

        /* 执行指令（继电器等） */
        execute_action(&action);

        vTaskDelay(pdMS_TO_TICKS(10000));  // 10秒一次
    }
}

/* ---------- 主入口 ---------- */
void app_main(void)
{
    /* NVS 初始化（ESP32 的小硬盘） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 初始化继电器和 RGB 灯 */
    control_init();

    /* 连 WiFi */
    ESP_LOGI(TAG, "开始连接WiFi...");
    wifi_init_sta();

    /* 启动传感器上报任务 */
    xTaskCreate(sensor_task, "sensor_task", 8192, NULL, 5, NULL);
}