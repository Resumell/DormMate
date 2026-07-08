#include "app_predict.h"
#include "app_http.h"
#include "app_sntp.h"
#include "app_nvs.h"
#include "app_control.h"
#include "app_voice.h"      // ← 新增：发语音播报
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PREDICT";

void predict_task(void *pv)
{
    ESP_LOGI(TAG, "预测任务启动，每5分钟检查一次");
    
    while (!sntp_is_synced()) {
        ESP_LOGW(TAG, "等待SNTP同步...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    while (1) {
        SensorData_t data = {0};
        ActionData_t action = {0};
        action.servo = -1;
        
        char now_str[6];
        get_time_str(now_str, sizeof(now_str));
        
        char history[7][6];
        nvs_get_history(history);
        
        /* TODO: 接入真实传感器后替换 */
        data.light = 0;
        data.human = 1;
        data.temperature = 26.5;
        data.humidity = 60.0;
        data.current = 0;
        data.relay = 0;
        data.servo = -1;
        strcpy(data.current_time, now_str);
        data.predict_query = 1;
        memcpy(data.history, history, sizeof(history));
        
        http_report(&data, &action);
        
        if (action.preheat == 1) {
            action.relay = 1;           // ← 新增：预热=开空调，继电器吸合
            ESP_LOGI(TAG, ">>> 触发预热！目标温度: %d", action.target_temp);
            execute_action(&action);    // 执行继电器+OLED
            voice_send_cmd(0x07);       // ← 新增：播报"已提前十五分钟环境预处理"
        }
        
        vTaskDelay(pdMS_TO_TICKS(300000));  /* 5分钟 */
    }
}