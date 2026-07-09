#ifndef VOICE_H
#define VOICE_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 引脚定义
 * ============================================================ */
#define VOICE_RX_PIN        11      /* GPIO11 - ESP32 RX ← 语音模块 TX */
#define VOICE_TX_PIN        12      /* GPIO12 - ESP32 TX → 语音模块 RX */

/* ============================================================
 * UART 配置
 * ============================================================ */
#define VOICE_UART_NUM      UART_NUM_1
#define VOICE_BAUD_RATE     9600
#define VOICE_BUFFER_SIZE   256

/* ============================================================
 * 语音指令定义
 * ============================================================ */
typedef enum {
    VOICE_CMD_NONE = 0,         /* 无指令 */
    VOICE_CMD_LIGHT_ON,         /* 开灯 */
    VOICE_CMD_LIGHT_OFF,        /* 关灯 */
    VOICE_CMD_AC_ON,            /* 开空调灯 */
    VOICE_CMD_AC_OFF,           /* 关空调灯 */
    VOICE_CMD_STATUS,           /* 查询状态 */
    VOICE_CMD_UNKNOWN,          /* 未知指令 */
} voice_command_t;

/* ============================================================
 * 数据结构
 * ============================================================ */
typedef struct {
    voice_command_t command;    /* 指令类型 */
    char raw_data[64];          /* 原始数据 */
    int data_len;               /* 数据长度 */
    bool is_new;                /* 是否有新指令 */
} voice_data_t;

/* ============================================================
 * 函数声明
 * ============================================================ */

/**
 * @brief 初始化语音模块 UART
 * @param rx_pin RX引脚 (ESP32接收)
 * @param tx_pin TX引脚 (ESP32发送)
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t voice_init(int rx_pin, int tx_pin);

/**
 * @brief 初始化语音模块 (使用默认引脚)
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t voice_init_default(void);

/**
 * @brief 获取语音指令 (非阻塞)
 * @param data 输出指令数据
 * @return true=有新指令, false=无
 */
bool voice_get_command(voice_data_t* data);

/**
 * @brief 语音播报 (TTS)
 * @param text 播报文本
 */
void voice_speak(const char* text);

/**
 * @brief 语音播报格式化
 * @param format 格式化字符串
 * @param ... 可变参数
 */
void voice_speak_format(const char* format, ...);

/**
 * @brief 解析语音指令
 * @param raw_data 原始UART数据
 * @param len 数据长度
 * @return 解析后的指令类型
 */
voice_command_t voice_parse_command(const char* raw_data, int len);

/**
 * @brief 获取指令名称
 * @param cmd 指令类型
 * @return 指令名称字符串
 */
const char* voice_get_command_name(voice_command_t cmd);

/**
 * @brief 清空接收缓冲区
 */
void voice_flush(void);

/**
 * @brief 检查语音模块是否就绪
 * @return true=就绪, false=未就绪
 */
bool voice_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_H */