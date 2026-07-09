/**
 * app_oled.h — OLED 显示模块头文件
 */

#ifndef APP_OLED_H
#define APP_OLED_H

void oled_init(void);
void oled_update(const char *line1, const char *line2);

#endif
