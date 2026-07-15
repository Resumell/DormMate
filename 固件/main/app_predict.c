#include "app_predict.h"
#include "app_http.h"
#include "app_sntp.h"
#include "app_nvs.h"
#include "app_control.h"
#include "app_voice.h"
#include "esp_log.h"
#include <string.h>

extern volatile float g_temperature;
extern volatile float g_humidity;
extern volatile int   g_light;

static const char *TAG = "PREDICT";

void predict_task(void *pv)
{
    ESP_LOGI(TAG, "预测任务启动，每1分钟检查一次");
    
    int sntp_wait = 0;
    while (!app_sntp_is_synced() && sntp_wait < 8){
        ESP_LOGW(TAG, "等待SNTP同步... (%d/8)", sntp_wait);
        vTaskDelay(pdMS_TO_TICKS(1000));
        sntp_wait++;
    }
    
    if (!app_sntp_is_synced()) {
        ESP_LOGW(TAG, "SNTP同步失败，使用本地时间继续");
    } else {
        ESP_LOGI(TAG, "SNTP已同步，预测任务开始");
    }
    
    static int preheat_done_today_infrared = 0;
    static uint32_t last_preheat_tick = 0;
    static char preheat_date[16] = {0};
    
    while (1) {
        char today_date[16];
        get_date_str(today_date, sizeof(today_date));
        
        if (strcmp(today_date, preheat_date) != 0) {
            preheat_done_today_infrared = 0;
            last_preheat_tick = 0;
            strcpy(preheat_date, today_date);
            ESP_LOGI(TAG, "日期已更新: %s，预处理标记重置", today_date);
        }
        
        SensorData_t data = {0};
        ActionData_t action = {0};
        action.servo = -1;
        action.relay = -1;
        action.ac_relay = -1;
        
        char now_str[6];
        get_time_str(now_str, sizeof(now_str));
        
        char history[7][6];
        nvs_get_history(history);
        
        data.temperature = g_temperature;
        data.humidity    = g_humidity;
        data.light       = g_light;
        ESP_LOGI(TAG, "预测任务使用全局传感器: 光敏=%d, 温度=%.1f, 湿度=%.1f",
                 data.light, data.temperature, data.humidity);
        
        data.human = 1;
        data.current = 0;
        data.relay = 0;
        data.servo = -1;
        strcpy(data.current_time, now_str);
        data.predict_query = 1;
        memcpy(data.history, history, sizeof(history));
        
        http_report(&data, &action);
        
        if (action.preheat == 1) {
            int is_infrared = (strstr(action.reason, "红外历史平均") != NULL);
            int should_execute = 0;
            
            if (is_infrared) {
                if (!preheat_done_today_infrared) {
                    should_execute = 1;
                    preheat_done_today_infrared = 1;
                    ESP_LOGI(TAG, "红外预处理触发，今日首次");
                } else {
                    ESP_LOGI(TAG, "红外预处理今日已执行过，跳过");
                }
            } else {
                if ((xTaskGetTickCount() - last_preheat_tick) > pdMS_TO_TICKS(10000)) {
                    should_execute = 1;
                    ESP_LOGI(TAG, "自定义预处理触发，距离上次%lu秒", 
                             (unsigned long)((xTaskGetTickCount() - last_preheat_tick) / 1000));
                } else {
                    ESP_LOGI(TAG, "自定义预处理10秒内已触发，跳过重复");
                }
            }
            
            if (should_execute) {
                action.ac_relay = 1;
                ESP_LOGI(TAG, ">>> 执行环境预处理！目标温度: %d", action.target_temp);
                execute_action(&action);
                last_preheat_tick = xTaskGetTickCount();
                ESP_LOGI(TAG, ">>> 预处理已执行并语音播报");
            } else {
                action.voice_notify = 0;
            }
        } else {
            ESP_LOGI(TAG, "当前无需预处理 (preheat=%d)", action.preheat);
        }
        
        if (action.voice_notify > 0) {
            voice_send_cmd((uint8_t)action.voice_notify);
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}