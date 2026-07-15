#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include "main.h"

void control_init(void);
void execute_action(ActionData_t *action);
int lamp_get_level(void);   /* 读取台灯当前状态 */
int ac_get_level(void);     /* 读取空调灯当前状态 */

/* 板载 WS2812 彩虹欢迎效果（红外检测到人体时调用） */
void board_led_rainbow_cycle(void);

#endif