/**
 * app_control.h — 设备控制模块头文件
 */

#ifndef APP_CONTROL_H
#define APP_CONTROL_H

void control_init(void);
void control_execute(int relay, int servo, int buzzer);
int control_get_relay_state(void);

#endif
