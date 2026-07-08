#ifndef APP_SNTP_H
#define APP_SNTP_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

void sntp_init(void);
int sntp_is_synced(void);
time_t get_epoch_time(void);
void get_time_str(char* buf, int len);
void get_date_str(char* buf, int len);
int get_weekday(void);

#endif