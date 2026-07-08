#include "app_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "SNTP";
static volatile int sntp_synced = 0;

void sntp_init(void)
{
    ESP_LOGI(TAG, "初始化SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "cn.pool.ntp.org");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    
    int retry = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }
    
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        sntp_synced = 1;
        ESP_LOGI(TAG, "SNTP时间同步成功");
        
        setenv("TZ", "CST-8", 1);
        tzset();
        
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "当前时间: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        ESP_LOGW(TAG, "SNTP同步失败，使用本地时间");
    }
}

int sntp_is_synced(void)
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
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, len, "%H:%M", &timeinfo);
}

void get_date_str(char* buf, int len)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(buf, len, "%Y-%m-%d", &timeinfo);
}

int get_weekday(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_wday;
}