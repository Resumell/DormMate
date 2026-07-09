/**
 * app_voice.h — 语音播报模块头文件
 *
 * voice_notify 编号规范：
 *   0  = 不播报
 *   1  = 询问是否开灯
 *   4  = 询问是否开空调
 *   9  = 欢迎回家
 *   10 = 预处理通知（0x0A）
 *   11 = 设置已修改（0x0B）
 */

#ifndef APP_VOICE_H
#define APP_VOICE_H

#include <stdbool.h>

void voice_init(void);
void voice_task(void *pvParameters);

void voice_enqueue(int notify_id, const char *tts_text, bool priority);

/* 便捷函数 */
void voice_notify_preheat(void);
void voice_notify_setting_changed(void);
void voice_notify_welcome(void);
void voice_notify_ask_light(void);
void voice_notify_ask_ac(void);

#endif
