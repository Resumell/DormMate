#include "app_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "SNTP";
static volatile int sntp_synced = 0;

static bool http_sync_time(void)
{
    char *buf = malloc(256);
    if (!buf) return false;

    esp_http_client_config_t config = {
        .url = "http://10.147.81.32:5000/api/time",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open失败: %s", esp_err_to_name(err));
        free(buf);
        esp_http_client_cleanup(client);
        return false;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len <= 0) {
        ESP_LOGE(TAG, "HTTP headers异常: %d", content_len);
        free(buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int total_read = 0;
    int read_len = 0;
    while (total_read < content_len && total_read < 255) {
        read_len = esp_http_client_read(client, buf + total_read, 255 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read == 0) {
        free(buf);
        ESP_LOGE(TAG, "HTTP body为空");
        return false;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON解析失败: %s", buf);
        free(buf);
        return false;
    }
    free(buf);

    /* ===== 优先使用后端返回的 epoch（Unix 时间戳，不受时区影响）===== */
    cJSON *j_epoch = cJSON_GetObjectItem(root, "epoch");
    if (j_epoch && cJSON_IsNumber(j_epoch)) {
        time_t t = (time_t)j_epoch->valuedouble;
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        cJSON_Delete(root);
        ESP_LOGI(TAG, "HTTP对时成功（epoch=%ld）", (long)t);
        return true;
    }

    /* fallback：使用年月日时分秒（需要 TZ 设置正确） */
    cJSON *j_year = cJSON_GetObjectItem(root, "year");
    cJSON *j_month = cJSON_GetObjectItem(root, "month");
    cJSON *j_day = cJSON_GetObjectItem(root, "day");
    cJSON *j_hour = cJSON_GetObjectItem(root, "hour");
    cJSON *j_min = cJSON_GetObjectItem(root, "minute");
    cJSON *j_sec = cJSON_GetObjectItem(root, "second");

    if (!j_year || !j_month || !j_day || !j_hour || !j_min || !j_sec) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "JSON字段缺失");
        return false;
    }

    setenv("TZ", "CST-8", 1);
    tzset();

    struct tm timeinfo = {0};
    timeinfo.tm_year = j_year->valueint - 1900;
    timeinfo.tm_mon  = j_month->valueint - 1;
    timeinfo.tm_mday = j_day->valueint;
    timeinfo.tm_hour = j_hour->valueint;
    timeinfo.tm_min  = j_min->valueint;
    timeinfo.tm_sec  = j_sec->valueint;

    time_t t = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    cJSON_Delete(root);
    return true;
}

/* 将 UTC epoch 转换为北京时间（手动 +8 小时，不依赖 localtime_r 的时区设置） */
static void epoch_to_beijing(time_t epoch, struct tm* out)
{
    epoch += 8 * 3600;  /* 北京时间 = UTC + 8 小时 */
    gmtime_r(&epoch, out);
}

void app_sntp_init(void)
{
    ESP_LOGI(TAG, "初始化时间同步...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "203.107.6.88");
    esp_sntp_setservername(1, "120.25.115.20");
    esp_sntp_init();

    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < 10) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        sntp_synced = 1;
        ESP_LOGI(TAG, "SNTP时间同步成功");
        goto set_tz;
    }

    ESP_LOGW(TAG, "SNTP失败，尝试HTTP对时...");
    if (http_sync_time()) {
        sntp_synced = 1;
        ESP_LOGI(TAG, "HTTP对时成功");
        goto set_tz;
    }

    ESP_LOGW(TAG, "HTTP对时也失败，使用本地时间（时间可能不准）");
    return;

set_tz:
    setenv("TZ", "CST-8", 1);
    tzset();

    time_t now;
    struct tm timeinfo;
    time(&now);
    epoch_to_beijing(now, &timeinfo);
    ESP_LOGI(TAG, "当前时间: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

int app_sntp_is_synced(void)
{
    return sntp_synced;
}

time_t get_epoch_time(void)
{
    time_t now;
    time(&now);
    return now;
}

void get_time_str(char* buf, int len)
{
    time_t now;
    time(&now);
    struct tm timeinfo;
    epoch_to_beijing(now, &timeinfo);
    strftime(buf, len, "%H:%M", &timeinfo);
}

void get_date_str(char* buf, int len)
{
    time_t now;
    time(&now);
    struct tm timeinfo;
    epoch_to_beijing(now, &timeinfo);
    strftime(buf, len, "%Y-%m-%d", &timeinfo);
}

int get_weekday(void)
{
    time_t now;
    time(&now);
    struct tm timeinfo;
    epoch_to_beijing(now, &timeinfo);
    return timeinfo.tm_wday;
}