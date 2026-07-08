#ifndef APP_NVS_H
#define APP_NVS_H

#include <stdint.h>
#include "app_sntp.h"

void nvs_save_return_time(const char* time_str);
void nvs_get_history(char history[7][6]);
void nvs_set_pref(int8_t temp, uint8_t enable);
void nvs_get_pref(int8_t* temp, uint8_t* enable);
int nvs_get_last_record_date(char* buf, int len);

#endif