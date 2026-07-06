#include "app_http.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"

#define BACKEND_IP "10.147.81.32" 
#define BACKEND_URL "http://" BACKEND_IP ":5000/api/report"

static const char *TAG = "HTTP";

void http_report(SensorData_t *data, ActionData_t *action)
{
    /* 1. 把传感器数据打包成 JSON */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", "dormmate_01");
    
    cJSON *sensors = cJSON_CreateObject();
    cJSON_AddNumberToObject(sensors, "light", data->light);
    cJSON_AddNumberToObject(sensors, "human", data->human);
    cJSON_AddNumberToObject(sensors, "temperature", data->temperature);
    cJSON_AddNumberToObject(sensors, "humidity", data->humidity);
    cJSON_AddNumberToObject(sensors, "current", data->current);
    cJSON_AddItemToObject(root, "sensors", sensors);
    
    cJSON *status = cJSON_CreateObject();
    cJSON_AddNumberToObject(status, "relay", data->relay);
    cJSON_AddNumberToObject(status, "servo", data->servo);
    cJSON_AddItemToObject(root, "status", status);
    
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "上报: %s", post_data);

    /* 2. 配置 HTTP 客户端（像填快递单） */
    esp_http_client_config_t config = {
        .url = BACKEND_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    /* 3. 发送（像把快递丢进邮筒） */
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "上报成功，HTTP状态码: %d", status_code);
        
        /* 4. 读后端返回的指令 */
        int content_len = esp_http_client_get_content_length(client);
        if (content_len > 0) {
            char *resp_buf = malloc(content_len + 1);
            if (resp_buf) {
                int read_len = esp_http_client_read(client, resp_buf, content_len);
                resp_buf[read_len] = '\0';
                
                cJSON *resp = cJSON_Parse(resp_buf);
                if (resp) {
                    cJSON *actions = cJSON_GetObjectItem(resp, "actions");
                    if (actions) {
                        cJSON *relay = cJSON_GetObjectItem(actions, "relay");
                        cJSON *servo = cJSON_GetObjectItem(actions, "servo");
                        cJSON *tts = cJSON_GetObjectItem(actions, "tts");
                        
                        if (relay) action->relay = relay->valueint;
                        if (servo) action->servo = servo->valueint;
                        if (tts && cJSON_IsString(tts)) {
                            strncpy(action->tts, tts->valuestring, sizeof(action->tts)-1);
                        }
                        ESP_LOGI(TAG, "收到指令: relay=%d, servo=%d", 
                                 action->relay, action->servo);
                    }
                    cJSON_Delete(resp);
                }
                free(resp_buf);
            }
        }
    } else {
        ESP_LOGE(TAG, "上报失败: %s", esp_err_to_name(err));
    }
    
    /* 5. 清理 */
    esp_http_client_cleanup(client);
    free(post_data);
}