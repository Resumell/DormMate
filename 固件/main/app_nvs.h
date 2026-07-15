#ifndef APP_NVS_H
#define APP_NVS_H

#include <stdint.h>
#include "app_sntp.h"

void nvs_save_return_time(const char* time_str);
void nvs_get_history(char history[7][6]);
void nvs_set_pref(int8_t temp, uint8_t enable);
void nvs_get_pref(int8_t* temp, uint8_t* enable);
int nvs_get_last_record_date(char* buf, int len);

/* ===== 新增：WiFi 凭证加密存储 ===== */
void nvs_save_wifi_creds(const char* ssid, const char* pass);
int nvs_get_wifi_creds(char* ssid, int ssid_len, char* pass, int pass_len);

#endif