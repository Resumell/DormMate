#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h>

/* 传感器数据结构（板子 -> 后端） */
typedef struct {
    int light;          // 光敏 ADC 值
    int human;          // 人体感应 0/1
    float temperature;  // 温度
    float humidity;     // 湿度
    float current;      // 电流
    int relay;          // 当前继电器状态
    int servo;          // 舵机角度（-1=不动）
    
    /* ===== 场景三新增 ===== */
    char current_time[6];   // "HH:MM"
    char history[7][6];     // 最近7天回寝时间
    int predict_query;      // 1=预测查询, 0=普通上报
    
    char event[32];         // "human_first" 或空
    char date[16];          // "YYYY-MM-DD"
    char first_seen_time[6]; // "HH:MM"
    int weekday;
    /* ====================== */
} SensorData_t;

/* AI 指令结构（后端 -> 板子） */
typedef struct {
    int relay;          // 0=关灯, 1=开灯
    int servo;          // 0-180, -1=不动
    int buzzer;         // 0=安静, 1=响
    char tts[64];       // 语音文本
    char oled_line1[32];
    char oled_line2[32];
    
    /* ===== 场景三新增 ===== */
    int preheat;            // 0=不预热, 1=预热
    int target_temp;        // 目标温度
    char predict_return_time[6]; // 预测回寝时间 "HH:MM"
    char reason[64];        // 决策原因
    /* ====================== */
} ActionData_t;

#endif