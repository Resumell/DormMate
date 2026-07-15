#include "app_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS";
#define NVS_NAMESPACE "dormmate"

void nvs_save_return_time(const char* time_str)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS打开失败");
        return;
    }
    
    char date_key[32];
    get_date_str(date_key, sizeof(date_key));
    
    nvs_set_str(handle, "last_return_time", time_str);
    nvs_set_str(handle, "last_record_date", date_key);
    
    char history[7][6];
    nvs_get_history(history);
    
    for (int i = 6; i > 0; i--) {
        strcpy(history[i], history[i-1]);
    }
    strcpy(history[0], time_str);
    
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist_%d", i);
        nvs_set_str(handle, key, history[i]);
    }
    
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "保存回寝时间: %s, 日期: %s", time_str, date_key);
}

void nvs_get_history(char history[7][6])
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        for (int i = 0; i < 7; i++) {
            strcpy(history[i], "");
        }
        return;
    }
    
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist_%d", i);
        size_t len = 6;
        esp_err_t ret = nvs_get_str(handle, key, history[i], &len);
        if (ret != ESP_OK) {
            strcpy(history[i], "");
        }
    }
    
    nvs_close(handle);
}

void nvs_set_pref(int8_t temp, uint8_t enable)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return;
    
    nvs_set_i8(handle, "pref_temp", temp);
    nvs_set_u8(handle, "pref_enable", enable);
    nvs_commit(handle);
    nvs_close(handle);
}

void nvs_get_pref(int8_t* temp, uint8_t* enable)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        *temp = 24;
        *enable = 1;
        return;
    }
    
    esp_err_t ret = nvs_get_i8(handle, "pref_temp", temp);
    if (ret != ESP_OK) *temp = 24;
    
    ret = nvs_get_u8(handle, "pref_enable", enable);
    if (ret != ESP_OK) *enable = 1;
    
    nvs_close(handle);
}

int nvs_get_last_record_date(char* buf, int len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        buf[0] = '\0';
        return 0;
    }
    
    size_t required_len = len;
    esp_err_t ret = nvs_get_str(handle, "last_record_date", buf, &required_len);
    nvs_close(handle);
    
    return (ret == ESP_OK) ? 1 : 0;
}

/* ===== 新增：WiFi 凭证读写 ===== */
void nvs_save_wifi_creds(const char* ssid, const char* pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS打开失败，无法保存WiFi凭证");
        return;
    }
    
    nvs_set_str(handle, "wifi_ssid", ssid);
    nvs_set_str(handle, "wifi_pass", pass);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi凭证已加密写入NVS");
}

int nvs_get_wifi_creds(char* ssid, int ssid_len, char* pass, int pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return 0;
    }
    
    size_t len = ssid_len;
    esp_err_t ret1 = nvs_get_str(handle, "wifi_ssid", ssid, &len);
    
    len = pass_len;
    esp_err_t ret2 = nvs_get_str(handle, "wifi_pass", pass, &len);
    
    nvs_close(handle);
    
    if (ret1 == ESP_OK && ret2 == ESP_OK) {
        ESP_LOGI(TAG, "从NVS读取WiFi凭证成功");
        return 1;
    }
    return 0;
}