/**
 * app_sntp.c — SNTP 网络时间同步模块
 * 功能：通过 NTP 获取准确 UTC 时间，转换为北京时间（UTC+8）
 * 依赖：WiFi 已连接，LWIP SNTP
 */

#include "app_sntp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <time.h>

static const char *TAG = "SNTP";

/** 北京时间偏移（秒） */
#define UTC8_OFFSET_SEC  (8 * 3600)

/** 时间同步完成标志 */
static bool time_synced = false;

/**
 * SNTP 同步完成回调
 */
static void _time_sync_cb(struct timeval *tv)
{
    time_synced = true;
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "时间同步完成: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

/**
 * 初始化 SNTP（连接 WiFi 后调用）
 * 服务器：阿里云 NTP（国内延迟低）+ pool.ntp.org 作为备选
 */
void sntp_init(void)
{
    ESP_LOGI(TAG, "开始SNTP时间同步...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_setservername(2, "time.windows.com");

    sntp_set_time_sync_notification_cb(_time_sync_cb);
    esp_sntp_init();

    // 设置时区为北京时间 UTC+8
    setenv("TZ", "CST-8", 1);
    tzset();

    // 等待同步（最多 15 秒）
    int retry = 0;
    while (!time_synced && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }

    if (time_synced) {
        ESP_LOGI(TAG, "SNTP 同步成功");
    } else {
        ESP_LOGW(TAG, "SNTP 同步超时，使用 RTC 默认时间");
    }
}

/**
 * 获取当前北京时间（HH:MM 格式）
 * @param buf 输出缓冲区（至少 6 字节）
 */
void sntp_get_time_str(char *buf, size_t len)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(buf, len, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

/**
 * 获取当前日期字符串（YYYY-MM-DD）
 * @param buf 输出缓冲区（至少 11 字节）
 */
void sntp_get_date_str(char *buf, size_t len)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(buf, len, "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

/**
 * 获取当前星期几（0=周日, 1=周一, ... 6=周六）
 */
int sntp_get_weekday(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_wday;
}

/**
 * 同步是否已完成
 */
bool sntp_is_synced(void)
{
    return time_synced;
}
