/**
 * app_nvs.h — NVS 非易失性存储模块头文件
 */

#ifndef APP_NVS_H
#define APP_NVS_H

#include "esp_err.h"
#include <stddef.h>

esp_err_t nvs_init(void);

esp_err_t nvs_save_return_history(const char *json_str, size_t len);
int nvs_load_return_history(char *buf, size_t max_len);

esp_err_t nvs_save_preferences(int pref_temp, int preheat_enable, const char *custom_time);
esp_err_t nvs_load_preferences(int *pref_temp_out, int *preheat_en_out,
                                char *custom_time_out, size_t time_buf_len);

int nvs_is_preheat_done(const char *today_date);
esp_err_t nvs_mark_preheat_done(const char *today_date);

#endif
