/**
 * app_nvs.c — NVS 非易失性存储模块
 * 功能：持久化存储回寝历史、偏好设置、预处理标记
 * 优化：添加清晰的中文注释，所有函数声明用途和参数
 */

#include "app_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS";
static const char *NVS_NAMESPACE = "dormmate";

/* ---------- 存储 key 名称 ---------- */
#define KEY_RETURN_HISTORY  "ret_hist"      // 回寝历史（JSON 数组）
#define KEY_PREF_TEMP       "pref_temp"     // 用户偏好温度
#define KEY_PREHEAT_ENABLE  "preht_en"      // 预处理开关
#define KEY_CUSTOM_TIME     "cust_time"     // 自定义回寝时间
#define KEY_PREHT_DONE      "pht_done"      // 今日预处理是否已完成
#define KEY_PREHT_DATE      "pht_date"      // 预处理完成日期（防跨天）

/**
 * 初始化 NVS 分区
 * @return ESP_OK=成功
 */
esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS分区需擦除，执行格式化...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_LOGI(TAG, "NVS初始化完成");
    return err;
}

/**
 * 保存回寝历史记录到NVS（JSON字符串）
 * @param json_str  JSON 格式的回寝历史数组
 * @param len       数据长度
 */
esp_err_t nvs_save_return_history(const char *json_str, size_t len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, KEY_RETURN_HISTORY, json_str, len);
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "回寝历史已保存 (%d bytes)", len);
    }
    nvs_close(handle);
    return err;
}

/**
 * 读取回寝历史记录
 * @param buf       输出缓冲区
 * @param max_len   缓冲区最大长度
 * @return 实际读取的字节数，失败返回-1
 */
int nvs_load_return_history(char *buf, size_t max_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return -1;

    size_t size = max_len;
    err = nvs_get_blob(handle, KEY_RETURN_HISTORY, buf, &size);
    nvs_close(handle);
    return (err == ESP_OK) ? (int)size : -1;
}

/**
 * 保存用户偏好设置（温度、开关、自定义回寝时间）到NVS
 */
esp_err_t nvs_save_preferences(int pref_temp, int preheat_enable, const char *custom_time)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_i32(handle, KEY_PREF_TEMP, pref_temp);
    nvs_set_i8(handle, KEY_PREHEAT_ENABLE, (int8_t)preheat_enable);
    if (custom_time) {
        nvs_set_str(handle, KEY_CUSTOM_TIME, custom_time);
    }
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "偏好设置已保存: temp=%d, enable=%d, time=%s", pref_temp, preheat_enable, custom_time ? custom_time : "NULL");
    return ESP_OK;
}

/**
 * 读取偏好设置
 * @param pref_temp_out   [输出] 目标温度
 * @param preheat_en_out  [输出] 预处理开关
 * @param custom_time_out [输出] 自定义回寝时间缓冲区
 * @param time_buf_len    缓冲区长度
 */
esp_err_t nvs_load_preferences(int *pref_temp_out, int *preheat_en_out,
                                char *custom_time_out, size_t time_buf_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    int32_t temp = 24;
    int8_t enable = 1;
    char time_buf[16] = {0};
    size_t len = sizeof(time_buf);

    nvs_get_i32(handle, KEY_PREF_TEMP, &temp);
    nvs_get_i8(handle, KEY_PREHEAT_ENABLE, &enable);
    nvs_get_str(handle, KEY_CUSTOM_TIME, time_buf, &len);

    nvs_close(handle);

    if (pref_temp_out) *pref_temp_out = (int)temp;
    if (preheat_en_out) *preheat_en_out = (int)enable;
    if (custom_time_out && time_buf_len > 0) {
        strncpy(custom_time_out, time_buf, time_buf_len - 1);
        custom_time_out[time_buf_len - 1] = '\0';
    }
    return ESP_OK;
}

/**
 * 检查今日预处理是否已完成（防止一天内重复触发）
 * @param today_date  当前日期字符串（如 "2026-07-09"）
 * @return 1=今日已完成, 0=未完成
 */
int nvs_is_preheat_done(const char *today_date)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return 0;

    char saved_date[16] = {0};
    size_t len = sizeof(saved_date);
    int8_t done = 0;

    nvs_get_str(handle, KEY_PREHT_DATE, saved_date, &len);
    nvs_get_i8(handle, KEY_PREHT_DONE, &done);
    nvs_close(handle);

    // 如果日期不同，重置标记
    if (today_date && strcmp(saved_date, today_date) != 0) return 0;

    return (int)done;
}

/**
 * 标记今日预处理已完成
 * @param today_date 当前日期字符串
 */
esp_err_t nvs_mark_preheat_done(const char *today_date)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_str(handle, KEY_PREHT_DATE, today_date);
    nvs_set_i8(handle, KEY_PREHT_DONE, 1);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "预处理标记已设置: %s", today_date);
    return ESP_OK;
}
