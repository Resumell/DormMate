/**
 * app_predict.c — 回寝预测/预处理模块
 * 功能：基于历史数据和用户设定，判断是否触发环境预处理
 * 优化：本地预判断（NVS中的历史数据 + 当前时间），减少无效AI调用
 */

#include "app_predict.h"
#include "app_nvs.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>

static const char *TAG = "Predict";

/* ---------- 预处理提前窗口（分钟）---------- */
#define PREHEAT_WINDOW_MIN 15  // 距回寝时间 ≤15分钟触发

/** 今日预处理是否已触发（内存标记，复位清除） */
static bool preheat_triggered_today = false;

/**
 * 获取当前时间字符串 HH:MM
 * @param buf 输出缓冲区（至少 6 字节）
 */
static void _get_now_time_str(char *buf, size_t len)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    snprintf(buf, len, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

/**
 * 计算两个 HH:MM 时间相差的分钟数
 * @param t1  时间1（如 "20:45"）
 * @param t2  时间2（如 "21:00"）
 * @return t2 - t1 的分钟差（正数=t2在未来, 负数=t2在过去）
 */
static int _time_diff_minutes(const char *t1, const char *t2)
{
    int h1, m1, h2, m2;
    if (sscanf(t1, "%d:%d", &h1, &m1) != 2) return 9999;
    if (sscanf(t2, "%d:%d", &h2, &m2) != 2) return 9999;
    return (h2 - h1) * 60 + (m2 - m1);
}

/**
 * 判断当前是否应触发预处理
 * 规则：
 *   1. 用户手动设定了回寝时间，当前时间距设定时间 ≤15分钟 → 触发
 *   2. 否则不触发
 * 说明：仅做本地预判断，实际决策仍由后端 AI 完成
 * @return 1=应该预处理, 0=不应该
 */
int predict_should_preheat(void)
{
    // 防重复：今日已触发过
    if (preheat_triggered_today) {
        ESP_LOGD(TAG, "今日已触发预处理，跳过");
        return 0;
    }

    // 从 NVS 读取偏好设置
    char custom_time[16] = {0};
    int preheat_enable = 0;
    nvs_load_preferences(NULL, &preheat_enable, custom_time, sizeof(custom_time));

    // 预处理未开启
    if (!preheat_enable) {
        ESP_LOGD(TAG, "预处理开关已关闭");
        return 0;
    }

    // 无自定义回寝时间
    if (custom_time[0] == '\0') {
        ESP_LOGD(TAG, "未设置自定义回寝时间");
        return 0;
    }

    char now_str[8];
    _get_now_time_str(now_str, sizeof(now_str));

    int diff = _time_diff_minutes(now_str, custom_time);
    ESP_LOGI(TAG, "时间差: %d min (now=%s, target=%s)", diff, now_str, custom_time);

    // 距目标时间 0~15 分钟 → 触发
    if (diff >= 0 && diff <= PREHEAT_WINDOW_MIN) {
        preheat_triggered_today = true;
        return 1;
    }

    return 0;
}

/**
 * 设置预处理已触发状态（供后端确认后调用）
 */
void predict_set_triggered(void)
{
    preheat_triggered_today = true;
    ESP_LOGI(TAG, "预处理已标记为触发");
}

/**
 * 重置预处理状态（跨天时调用）
 */
void predict_reset_daily(void)
{
    preheat_triggered_today = false;
    ESP_LOGI(TAG, "每日预处理标记已重置");
}
