#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h>

typedef struct {
    int light;
    int human;
    float temperature;
    float humidity;
    float current;
    int relay;          /* 台灯状态 */
    int ac_relay;       /* 空调灯状态 */
    int servo;
    char current_time[6];
    char history[7][6];
    int predict_query;
    char event[32];
    char date[16];
    char first_seen_time[6];
    int weekday;
    char source[16];
} SensorData_t;

typedef struct {
    int relay;          /* 台灯: -1=不操作, 0=关, 1=开 */
    int ac_relay;       /* 空调灯: -1=不操作, 0=关, 1=开 */
    int servo;
    int buzzer;
    char tts[64];
    char oled_line1[32];
    char oled_line2[32];
    int preheat;
    int target_temp;
    char predict_return_time[6];
    char reason[64];
    int voice_notify;
    int manual;
} ActionData_t;

#endif