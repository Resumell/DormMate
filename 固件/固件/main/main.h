/*
 * DormMate 主程序头文件
 * 功能：智能宿舍管家 ESP32 固件 - 全局宏定义、任务声明、外部变量声明
 */

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

/* ====== 引脚映射（以 DormMate_main 实际接线为准）====== */

/* 传感器引脚 */
#define DHT11_PIN           4      /* DHT11 温湿度传感器数据引脚 */
#define LIGHT_ADC_CHANNEL   ADC1_CHANNEL_0  /* 光敏传感器 ADC 通道 GPIO36 */
#define PIR_PIN             5      /* 红外人体传感器引脚（高电平=有人） */

/* 输出引脚 */
#define RELAY_PIN           12     /* 继电器控制引脚（灯光/空调） */
#define BUZZER_PIN          13     /* 蜂鸣器 */
#define OLED_SDA            21     /* OLED I2C SDA */
#define OLED_SCL            22     /* OLED I2C SCL */

/* 语音模块 UART */
#define VOICE_UART_NUM      UART_NUM_1  /* 语音模块使用的 UART 端口 */
#define VOICE_TXD           17          /* ESP32 TX → 语音模块 RX */
#define VOICE_RXD           16          /* ESP32 RX → 语音模块 TX */

/* ====== 系统常量 ====== */
#define WIFI_SSID           "CONFIG_WIFI_SSID"  /* 从 NVS 读取 */
#define WIFI_PASS           "CONFIG_WIFI_PASS"
#define SERVER_BASE_URL     "CONFIG_SERVER_URL"  /* 后端地址，如 http://192.168.1.100:5000 */

/* 任务延迟时间 (ms) */
#define SENSOR_TASK_DELAY    5000    /* 传感器采集间隔 */
#define REPORT_TASK_DELAY    10000   /* 数据上报间隔 */
#define PREDICT_TASK_DELAY   60000   /* 预回寝预测检测间隔（1分钟） */
#define HTTP_TIMEOUT_MS      10000   /* HTTP 请求超时（10秒） */

/* 传感器阈值 */
#define LIGHT_DARK_THRESHOLD  3500   /* 光线 <3500=亮，>3500=暗 */
#define TEMP_HIGH_THRESHOLD   30     /* 温度过高阈值 */
#define TEMP_LOW_THRESHOLD    18     /* 温度过低阈值 */

/* ====== 数据结构 ====== */

/* 传感器数据 */
typedef struct {
    float temperature;  /* 温度 ℃ */
    float humidity;     /* 湿度 % */
    int   light_raw;    /* 光线 ADC 原始值 (0-4095) */
    int   human;        /* 红外检测：1=有人，0=无人 */
    int   relay;        /* 当前继电器状态：1=开，0=关 */
} sensor_data_t;

/* 后端返回的动作指令 */
typedef struct {
    int relay;              /* 继电器：1=开，0=关 */
    int servo;              /* 舵机角度（-1=不操作） */
    int buzzer;             /* 蜂鸣器：1=响，0=不响 */
    int preheat;            /* 预处理标记：1=触发预处理 */
    int voice_notify;       /* 语音播报编号（0=不播报，1/4/9/10/11） */
    char tts[256];          /* TTS 播报文本（保留字段） */
    char oled_line1[32];    /* OLED 第1行 */
    char oled_line2[32];    /* OLED 第2行 */
    char reason[128];       /* 决策原因 */
    char predict_time[8];   /* 预测回寝时间 */
    int target_temp;        /* 目标温度 */
    int sleep_after;        /* 执行后是否休眠（单流程模式） */
} actions_t;

/* 语音播报队列项 */
typedef struct {
    int notify_id;      /* voice_notify 编号 */
    int priority;       /* 优先级：0=普通，1=高（预处理/设置确认） */
} voice_queue_item_t;

/* ====== 全局变量声明 ====== */
extern sensor_data_t g_sensors;
extern char g_server_url[128];
extern char g_device_id[32];

/* ====== 函数声明 ====== */

/* main.c */
void app_main(void);

/* 传感器任务 */
void sensor_task(void *pvParameters);
void read_sensors(void);

/* 数据上报任务 */
void report_task(void *pvParameters);
int  send_report_to_server(const char *event, const char *query_type, actions_t *out_actions);

/* 预回寝预测任务 */
void predict_task(void *pvParameters);
int  check_predict_condition(void);

/* 动作执行 */
void execute_actions(actions_t *actions);
void control_relay(int state);
void control_servo(int angle);
void control_buzzer(int state);

/* 休眠模式（单流程执行完成后进入） */
void enter_deep_sleep(void);

/* ====== 外部模块声明 ====== */
void wifi_init_sta(void);
void nvs_init(void);

/* DHT11 */
void dht11_init(void);
int  dht11_read(float *temperature, float *humidity);

/* 光线 */
void light_init(void);
int  light_read_raw(void);

/* 红外 */
void pir_init(void);
int  pir_read(void);

/* OLED */
void oled_init(void);
void oled_display(const char *line1, const char *line2);

/* 语音模块 */
void voice_init(void);
void voice_notify(int notify_id);
void voice_notify_priority(int notify_id, int priority);  /* 优先级播报 */
int  voice_is_ready(void);
void voice_process_queue(void);  /* 处理播报队列 */

/* 执行器 */
void zhixinger_init(void);  /* 继电器等执行器初始化 */
void zhixingyi_init(void);  /* 舵机等执行器初始化 */
void zhixinger_set(int pin, int state);
void zhixingyi_set(int angle);

/* HTTP 客户端 */
void http_init(void);
int  http_post_json(const char *url, const char *json_body, char *response, int response_len);
int  http_get_json(const char *url, char *response, int response_len);

/* SNTP 时间同步 */
void sntp_init(void);
void sntp_get_time(char *time_str, int len);  /* 获取 HH:MM 格式时间 */

#endif /* MAIN_H */
