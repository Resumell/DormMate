#include "app_http.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_log.h"

#define BACKEND_IP "10.147.81.32" 
#define BACKEND_URL "http://" BACKEND_IP ":5000/api/report"

static const char *TAG = "HTTP";

void http_report(SensorData_t *data, ActionData_t *action)
{
    /* 1. 打包 JSON（此部分不变） */
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

    /* 2. 配置 HTTP 客户端（超时改为15秒，等AI响应） */
    esp_http_client_config_t config = {
        .url = BACKEND_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    /* ========== 改成手动 open → write → fetch_headers → read ========== */
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

    /* 6. 处理返回的 JSON（和原来一样） */
    if (total_read > 0) {
        resp_buf[total_read] = '\0';
        ESP_LOGI(TAG, "后端返回: %s", resp_buf);
        
        cJSON *resp = cJSON_Parse(resp_buf);
        if (resp) {
cJSON *actions = cJSON_GetObjectItem(resp, "actions");
if (actions) {
    	 cJSON *relay = cJSON_GetObjectItem(actions, "relay");
   	 cJSON *servo = cJSON_GetObjectItem(actions, "servo");
   	 cJSON *buzzer = cJSON_GetObjectItem(actions, "buzzer");  // 新增
   	 cJSON *tts = cJSON_GetObjectItem(actions, "tts");
    	 cJSON *oled1 = cJSON_GetObjectItem(actions, "oled_line1");  // 新增
  	 cJSON *oled2 = cJSON_GetObjectItem(actions, "oled_line2");  // 新增
    
   	  if (relay) action->relay = relay->valueint;
  	  if (servo) action->servo = servo->valueint;
  	  if (buzzer) action->buzzer = buzzer->valueint;  // 新增
   	  if (tts && cJSON_IsString(tts)) {
      	    strncpy(action->tts, tts->valuestring, sizeof(action->tts)-1);
    	   }
  	    // 新增：解析 OLED 两行文字
   	    if (oled1 && cJSON_IsString(oled1)) {
    	       strncpy(action->oled_line1, oled1->valuestring, sizeof(action->oled_line1)-1);
   	   }
 	     if (oled2 && cJSON_IsString(oled2)) {
     	     strncpy(action->oled_line2, oled2->valuestring, sizeof(action->oled_line2)-1);
  	   }
    
	     ESP_LOGI(TAG, "解析指令: relay=%d, tts=%s", action->relay, action->tts);
 	     ESP_LOGI(TAG, "OLED: [%s] / [%s]", action->oled_line1, action->oled_line2);  // 新增日志
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