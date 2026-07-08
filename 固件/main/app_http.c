#include "app_http.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"

#define BACKEND_IP "10.147.81.32" 
#define BACKEND_URL "http://" BACKEND_IP ":5000/api/report"

static const char *TAG = "HTTP";

void http_report(SensorData_t *data, ActionData_t *action)
{
    /* 1. 打包 JSON */
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
    
    /* ===== 新增：context 字段 ===== */
    cJSON *context = cJSON_CreateObject();
    if (data->predict_query) {
        cJSON_AddStringToObject(context, "query_type", "predict");
        cJSON_AddStringToObject(context, "current_time", data->current_time);
        cJSON *hist_arr = cJSON_CreateArray();
        for (int i = 0; i < 7; i++) {
            if (strlen(data->history[i]) > 0) {
                cJSON_AddItemToArray(hist_arr, cJSON_CreateString(data->history[i]));
            }
        }
        cJSON_AddItemToObject(context, "history", hist_arr);
    } else if (strlen(data->event) > 0) {
        cJSON_AddStringToObject(context, "event", data->event);
        cJSON_AddStringToObject(context, "date", data->date);
        cJSON_AddStringToObject(context, "first_seen_time", data->first_seen_time);
        cJSON_AddNumberToObject(context, "weekday", data->weekday);
    }
    cJSON_AddItemToObject(root, "context", context);
    /* ============================ */
    
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "上报: %s", post_data);

    /* 2. 配置 HTTP 客户端 */
    esp_http_client_config_t config = {
        .url = BACKEND_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    /* 3. 打开连接，写入 POST 数据 */
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开连接失败");
        goto cleanup;
    }
    esp_http_client_write(client, post_data, strlen(post_data));

    /* 4. 读取响应头，获取 body 长度 */
    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Content-Length: %d", content_length);

    /* 5. 读取响应 body */
    char *resp_buf = malloc(512);
    if (!resp_buf) {
        ESP_LOGE(TAG, "内存分配失败");
        esp_http_client_close(client);
        goto cleanup;
    }
    memset(resp_buf, 0, 512);

    int total_read = 0;
    int read_len = 0;
    while (total_read < content_length && total_read < 511) {
        read_len = esp_http_client_read(client, resp_buf + total_read, 511 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    ESP_LOGI(TAG, "实际读取字节数: %d", total_read);

    /* 6. 处理返回的 JSON */
    if (total_read > 0) {
        resp_buf[total_read] = '\0';
        ESP_LOGI(TAG, "后端返回: %s", resp_buf);
        
        cJSON *resp = cJSON_Parse(resp_buf);
        if (resp) {
            cJSON *actions = cJSON_GetObjectItem(resp, "actions");
            if (actions) {
                cJSON *relay = cJSON_GetObjectItem(actions, "relay");
                cJSON *servo = cJSON_GetObjectItem(actions, "servo");
                cJSON *buzzer = cJSON_GetObjectItem(actions, "buzzer");
                cJSON *tts = cJSON_GetObjectItem(actions, "tts");
                cJSON *oled1 = cJSON_GetObjectItem(actions, "oled_line1");
                cJSON *oled2 = cJSON_GetObjectItem(actions, "oled_line2");
                
                /* ===== 新增：解析预热相关字段 ===== */
                cJSON *preheat = cJSON_GetObjectItem(actions, "preheat");
                cJSON *target_temp = cJSON_GetObjectItem(actions, "target_temp");
                cJSON *pred_time = cJSON_GetObjectItem(actions, "predict_return_time");
                cJSON *reason = cJSON_GetObjectItem(actions, "reason");
                /* ================================== */
                
                if (relay) action->relay = relay->valueint;
                if (servo) action->servo = servo->valueint;
                if (buzzer) action->buzzer = buzzer->valueint;
                if (tts && cJSON_IsString(tts)) {
                    strncpy(action->tts, tts->valuestring, sizeof(action->tts)-1);
                }
                if (oled1 && cJSON_IsString(oled1)) {
                    strncpy(action->oled_line1, oled1->valuestring, sizeof(action->oled_line1)-1);
                }
                if (oled2 && cJSON_IsString(oled2)) {
                    strncpy(action->oled_line2, oled2->valuestring, sizeof(action->oled_line2)-1);
                }
                
                /* ===== 新增：赋值预热字段 ===== */
                if (preheat) action->preheat = preheat->valueint;
                if (target_temp) action->target_temp = target_temp->valueint;
                if (pred_time && cJSON_IsString(pred_time)) {
                    strncpy(action->predict_return_time, pred_time->valuestring, sizeof(action->predict_return_time)-1);
                }
                if (reason && cJSON_IsString(reason)) {
                    strncpy(action->reason, reason->valuestring, sizeof(action->reason)-1);
                }
                /* ============================= */
                
                ESP_LOGI(TAG, "解析指令: relay=%d, preheat=%d, tts=%s", 
                         action->relay, action->preheat, action->tts);
                ESP_LOGI(TAG, "OLED: [%s] / [%s]", action->oled_line1, action->oled_line2);
            }
            cJSON_Delete(resp);
        } else {
            ESP_LOGE(TAG, "JSON解析失败");
        }
    } else {
        ESP_LOGW(TAG, "后端未返回数据");
    }

    free(resp_buf);
    esp_http_client_close(client);

cleanup:
    esp_http_client_cleanup(client);
    free(post_data);
}