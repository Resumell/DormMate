/**
 * app_sntp.h — SNTP 时间同步模块头文件
 */

#ifndef APP_SNTP_H
#define APP_SNTP_H

#include <stdbool.h>
#include <stddef.h>

void sntp_init(void);
void sntp_get_time_str(char *buf, size_t len);
void sntp_get_date_str(char *buf, size_t len);
int  sntp_get_weekday(void);
bool sntp_is_synced(void);

#endif
