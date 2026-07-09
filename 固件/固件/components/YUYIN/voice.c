#include "voice.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ============================================================
 * 静态变量
 * ============================================================ */
static const char* TAG = "VOICE";
static bool g_voice_ready = false;
static uint8_t g_rx_buffer[VOICE_BUFFER_SIZE];
static voice_data_t g_voice_data = {0};

/* ============================================================
 * 指令映射表
 * ============================================================ */
typedef struct {
    const char* keyword;
    voice_command_t cmd;
} cmd_map_t;

static const cmd_map_t g_cmd_map[] = {
    {"开灯",     VOICE_CMD_LIGHT_ON},
    {"打开灯",   VOICE_CMD_LIGHT_ON},
    {"关灯",     VOICE_CMD_LIGHT_OFF},
    {"关闭灯",   VOICE_CMD_LIGHT_OFF},
    {"开空调",   VOICE_CMD_AC_ON},
    {"关空调",   VOICE_CMD_AC_OFF},
    {"状态",     VOICE_CMD_STATUS},
    {"查询",     VOICE_CMD_STATUS},
};

#define CMD_MAP_SIZE (sizeof(g_cmd_map) / sizeof(g_cmd_map[0]))

/* ============================================================
 * 私有函数
 * ============================================================ */

/**
 * @brief 发送数据到语音模块
 */
static void voice_send_data(const uint8_t* data, int len)
{
    uart_write_bytes(VOICE_UART_NUM, (const char*)data, len);
}

/* ============================================================
 * 公有函数
 * ============================================================ */

esp_err_t voice_init(int rx_pin, int tx_pin)
{
    /* 配置 UART */
    uart_config_t uart_config = {
        .baud_rate = VOICE_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(VOICE_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 设置引脚 */
    ret = uart_set_pin(VOICE_UART_NUM, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART引脚设置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 安装驱动 */
    ret = uart_driver_install(VOICE_UART_NUM, VOICE_BUFFER_SIZE, VOICE_BUFFER_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART驱动安装失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 清空缓冲区 */
    uart_flush(VOICE_UART_NUM);

    g_voice_ready = true;
    ESP_LOGI(TAG, "语音模块就绪 (RX=GPIO%d, TX=GPIO%d, 波特率=%d)",
        rx_pin, tx_pin, VOICE_BAUD_RATE);

    return ESP_OK;
}

esp_err_t voice_init_default(void)
{
    return voice_init(VOICE_RX_PIN, VOICE_TX_PIN);
}

bool voice_get_command(voice_data_t* data)
{
    if (!g_voice_ready) return false;

    int len = uart_read_bytes(VOICE_UART_NUM, g_rx_buffer, sizeof(g_rx_buffer) - 1, 0);
    if (len <= 0) return false;

    g_rx_buffer[len] = '\0';
    ESP_LOGI(TAG, "收到语音: %s", (char*)g_rx_buffer);

    /* 解析指令 */
    voice_command_t cmd = voice_parse_command((char*)g_rx_buffer, len);

    /* 填充数据 */
    data->command = cmd;
    data->data_len = len;
    strncpy(data->raw_data, (char*)g_rx_buffer, sizeof(data->raw_data) - 1);
    data->is_new = true;

    *g_voice_data = *data;

    return true;
}

void voice_speak(const char* text)
{
    if (!g_voice_ready || !text) return;

    /* SU-03T 通常直接发送文本即可播报 */
    voice_send_data((const uint8_t*)text, strlen(text));
    ESP_LOGI(TAG, "播报: %s", text);
}

void voice_speak_format(const char* format, ...)
{
    if (!g_voice_ready || !format) return;

    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    voice_speak(buffer);
}

voice_command_t voice_parse_command(const char* raw_data, int len)
{
    if (!raw_data || len <= 0) return VOICE_CMD_NONE;

    /* 遍历指令映射表 */
    for (int i = 0; i < CMD_MAP_SIZE; i++) {
        if (strstr(raw_data, g_cmd_map[i].keyword)) {
            return g_cmd_map[i].cmd;
        }
    }

    return VOICE_CMD_UNKNOWN;
}

const char* voice_get_command_name(voice_command_t cmd)
{
    switch (cmd) {
        case VOICE_CMD_NONE:        return "NONE";
        case VOICE_CMD_LIGHT_ON:    return "开灯";
        case VOICE_CMD_LIGHT_OFF:   return "关灯";
        case VOICE_CMD_AC_ON:       return "开空调灯";
        case VOICE_CMD_AC_OFF:      return "关空调灯";
        case VOICE_CMD_STATUS:      return "状态查询";
        case VOICE_CMD_UNKNOWN:     return "未知指令";
        default:                    return "UNKNOWN";
    }
}

void voice_flush(void)
{
    uart_flush(VOICE_UART_NUM);
    memset(&g_voice_data, 0, sizeof(voice_data_t));
    ESP_LOGI(TAG, "语音缓冲区已清空");
}

bool voice_is_ready(void)
{
    return g_voice_ready;
}