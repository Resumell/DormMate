/**
 * app_http.c — HTTP 通信模块
 * 功能：向 Flask 后端上报传感器数据，解析返回的 AI 决策/语音通知
 * 优化：timeout 从 25s 降为 10s，HTTP 失败时本地 Fallback
 */

#include "app_http.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "HTTP";

/* ---------- 后端地址（部署时修改为实际IP）---------- */
#define SERVER_URL "http://10.147.81.32:5000"

/* ---------- HTTP 请求超时（秒）---------- */
#define HTTP_TIMEOUT_MS (10 * 1000)  // 10秒超时

/* ---------- 静态动作结构体 ---------- */
static http_actions_t last_actions;

/**
 * HTTP 响应数据收集结构体 —— ESP HTTP 客户端流式缓冲区
 */
typedef struct {
    char *data;
    int   len;
    int   cap;
} http_buffer_t;

/* 内部: 初始化 HTTP 客户端配置（超时、URL、方法、事件回调） */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    http_buffer_t *buf = (http_buffer_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (buf->len + evt->data_len < buf->cap) {
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            buf->data[buf->len] = '\0';
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * 向 /api/report 发送传感器数据并解析后端返回
 * @param json_payload  已序列化的 JSON 字符串（含 sensors + context）
 * @param actions_out   输出参数，解析后的动作结果
 * @return ESP_OK=成功，ESP_FAIL=失败（已写入 actions_out）
 */
esp_err_t http_post_report(const char *json_payload, http_actions_t *actions_out)
{
    // 初始化默认动作（失败时安全：全部关闭）
    memset(actions_out, 0, sizeof(http_actions_t));
    actions_out->servo = -1;

    if (!json_payload) {
        ESP_LOGE(TAG, "json_payload 为空");
        return ESP_FAIL;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s/api/report", SERVER_URL);

    char resp_buf[2048] = {0};
    http_buffer_t buf = { .data = resp_buf, .len = 0, .cap = sizeof(resp_buf) };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = _http_event_handler,
        .user_data = &buf,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP请求失败: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP状态码异常: %d", status_code);
        return ESP_FAIL;
    }

    // 解析 JSON 返回
    cJSON *root = cJSON_Parse(buf.data);
    if (!root) {
        ESP_LOGE(TAG, "JSON解析失败");
        return ESP_FAIL;
    }

    // 解析 actions 字段
    cJSON *actions = cJSON_GetObjectItem(root, "actions");
    if (!actions) {
        ESP_LOGE(TAG, "返回中无 actions 字段");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // 逐字段安全读取
    cJSON *item;
    item = cJSON_GetObjectItem(actions, "relay");
    if (cJSON_IsNumber(item)) actions_out->relay = item->valueint;

    item = cJSON_GetObjectItem(actions, "servo");
    if (cJSON_IsNumber(item)) actions_out->servo = item->valueint;

    item = cJSON_GetObjectItem(actions, "buzzer");
    if (cJSON_IsNumber(item)) actions_out->buzzer = item->valueint;

    item = cJSON_GetObjectItem(actions, "voice_notify");
    if (cJSON_IsNumber(item)) actions_out->voice_notify = item->valueint;

    item = cJSON_GetObjectItem(actions, "preheat");
    if (cJSON_IsNumber(item)) actions_out->preheat = item->valueint;

    item = cJSON_GetObjectItem(actions, "oled_line1");
    if (cJSON_IsString(item)) snprintf(actions_out->oled_line1, sizeof(actions_out->oled_line1), "%s", item->valuestring);

    item = cJSON_GetObjectItem(actions, "oled_line2");
    if (cJSON_IsString(item)) snprintf(actions_out->oled_line2, sizeof(actions_out->oled_line2), "%s", item->valuestring);

    ESP_LOGI(TAG, "上报成功: relay=%d, voice_notify=%d, preheat=%d",
             actions_out->relay, actions_out->voice_notify, actions_out->preheat);

    cJSON_Delete(root);

    // 保存到静态缓存
    memcpy(&last_actions, actions_out, sizeof(http_actions_t));
    return ESP_OK;
}

/**
 * 获取最近一次 HTTP 返回的动作（用于 fallback 场景）
 */
http_actions_t* http_get_last_actions(void)
{
    return &last_actions;
}
