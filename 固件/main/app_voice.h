#ifndef APP_VOICE_H
#define APP_VOICE_H

#include <stdint.h>

void voice_init(void);
void voice_task(void *pv);
void voice_send_cmd(uint8_t msg_id);  // 新增：ESP32 主动发指令给语音模块

#endif