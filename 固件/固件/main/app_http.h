/**
 * app_http.h — HTTP 通信模块头文件
 */

#ifndef APP_HTTP_H
#define APP_HTTP_H

#include "esp_err.h"

/* ---------- 后端返回的动作结构体 ---------- */
typedef struct {
    int relay;              // 继电器（0/1）
    int servo;              // 舵机角度（-1=不操作）
    int buzzer;             // 蜂鸣器（0/1）
    int voice_notify;       // 语音通知编号（见 voice_module.md）
    int preheat;            // 预处理标记（0/1）
    char oled_line1[32];    // OLED 第一行
    char oled_line2[32];    // OLED 第二行
} http_actions_t;

esp_err_t http_post_report(const char *json_payload, http_actions_t *actions_out);
http_actions_t* http_get_last_actions(void);

#endif
