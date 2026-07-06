#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h>

/* 传感器数据结构（板子 → 后端） */
typedef struct {
    int light;          // 光敏 ADC 值
    int human;          // 人体感应 0/1（现在用假数据）
    float temperature;  // 温度（假数据）
    float humidity;     // 湿度（假数据）
    float current;      // 电流（假数据）
    int relay;          // 当前继电器状态
    int servo;          // 舵机角度（-1=不动）
} SensorData_t;

/* AI 指令结构（后端 → 板子） */
typedef struct {
    int relay;          // 0=关灯, 1=开灯
    int servo;          // 0-180, -1=不动
    int buzzer;         // 0=安静, 1=响
    char tts[64];       // 语音文本
    char oled_line1[32];
    char oled_line2[32];
} ActionData_t;

#endif