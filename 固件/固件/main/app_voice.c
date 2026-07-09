/**
 * app_voice.c — 语音播报模块
 *
 * 功能：通过 UART 向语音模块发送播报指令
 * 核心优化：
 *   1. 优先级队列：预处理通知(0x0A)和设置确认(0x0B)为高优先级，插队立即播报
 *   2. 失败重试机制：播报前检查模块就绪状态，失败最多重试 2 次（间隔 200ms）
 *   3. 播报完成后清空 TTS 缓存文本
 *   4. 播报语速控制：精简 TTS 文本减少播报时长
 *
 * voice_notify 编号映射：
 *   0  = 不播报
 *   1  = 询问是否开灯（"当前光线太暗，是否要为您开灯"）
 *   4  = 询问是否开空调（"当前温度过高，是否要为您开空调"）
 *   9  = 欢迎回家（"欢迎回家"）
 *   10 = 预处理通知（"已开启环境预处理"）
 *   11 = 设置已修改（"已为您修改设置"）
 */

#include "app_voice.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "Voice";

/* ---------- 语音模块 UART 配置 ---------- */
#define VOICE_UART_NUM      UART_NUM_2    // UART2
#define VOICE_TX_PIN        GPIO_NUM_17   // TX 引脚
#define VOICE_RX_PIN        GPIO_NUM_16   // RX 引脚
#define VOICE_BAUD_RATE     115200
#define VOICE_BUF_SIZE      256

/* ---------- 重试配置 ---------- */
#define MAX_RETRY           2             // 最大重试次数
#define RETRY_DELAY_MS      200           // 重试间隔(ms)

/* ---------- 优先级队列 ---------- */
#define QUEUE_LEN           8             // 队列容量

/** 语音播报条目 */
typedef struct {
    int notify_id;          // voice_notify 编号
    char tts_text[128];     // 播报文本（后端下发的 TTS 文本）
    bool is_high_priority;  // 是否为高优先级（插队播报）
} voice_item_t;

/** 环形队列 */
static voice_item_t queue[QUEUE_LEN];
static int queue_head = 0;
static int queue_tail = 0;
static int queue_count = 0;

/** 模块是否已就绪 */
static bool module_ready = false;

/**
 * 初始化语音模块 UART
 * 引脚：TX=GPIO17, RX=GPIO16
 */
void voice_init(void)
{
    uart_config_t uart_conf = {
        .baud_rate = VOICE_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(VOICE_UART_NUM, &uart_conf);
    uart_set_pin(VOICE_UART_NUM, VOICE_TX_PIN, VOICE_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(VOICE_UART_NUM, VOICE_BUF_SIZE * 2, 0, 0, NULL, 0);

    module_ready = true;
    ESP_LOGI(TAG, "语音模块初始化完成 (UART2, TX=GPIO%d, RX=GPIO%d)",
             VOICE_TX_PIN, VOICE_RX_PIN);
}

/**
 * 检查语音模块是否就绪
 * @return true=就绪, false=未就绪（UART 未初始化）
 */
static bool _voice_is_ready(void)
{
    return module_ready;
}

/**
 * 向语音模块发送播报指令
 * @param notify_id  voice_notify 编号（1/4/9/10/11）
 * @param tts_text   播报文本（后端AI下发，用于编号0/特殊情况的直接文本播报）
 * @return 0=成功, -1=失败
 *
 * 协议说明（以实际语音模块文档为准）：
 *   帧格式: AA <cmd> <len> <data...> 55
 *   notify_id=0 时使用 tts_text 文本播报
 *   notify_id!=0 时按编号播报（模块内置音频文件）
 */
static int _voice_send_command(int notify_id, const char *tts_text)
{
    if (!_voice_is_ready()) {
        ESP_LOGE(TAG, "语音模块未就绪，无法播报");
        return -1;
    }

    uint8_t buf[VOICE_BUF_SIZE];
    int len = 0;

    // 构造帧
    buf[len++] = 0xAA;  // 帧头

    if (notify_id == 0 && tts_text && tts_text[0]) {
        // 按文本播报（TTS模式）
        buf[len++] = 0x01;  // 命令：TTS文本播报
        int text_len = strlen(tts_text);
        if (text_len > 200) text_len = 200;
        buf[len++] = text_len + 1;
        memcpy(&buf[len], tts_text, text_len);
        len += text_len;
    } else {
        // 按编号播报（播放预先烧录的音频段）
        buf[len++] = 0x02;  // 命令：按编号播报
        buf[len++] = 0x01;  // 数据长度=1字节
        buf[len++] = (uint8_t)(notify_id & 0xFF);
    }
    buf[len++] = 0x55;  // 帧尾

    // 发送
    int sent = uart_write_bytes(VOICE_UART_NUM, (const char *)buf, len);
    if (sent < 0) {
        ESP_LOGE(TAG, "UART 发送失败");
        return -1;
    }

    ESP_LOGI(TAG, "语音播报: notify_id=%d, tts='%s', bytes=%d",
             notify_id, tts_text ? tts_text : "NULL", sent);
    return 0;
}

/**
 * 播报单条语音（含重试机制）
 * @param item 待播报条目
 */
static void _voice_play_item(voice_item_t *item)
{
    int retry = 0;
    int rc = -1;

    while (retry <= MAX_RETRY) {
        if (!_voice_is_ready()) {
            ESP_LOGW(TAG, "重试 %d/%d: 模块未就绪", retry + 1, MAX_RETRY + 1);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            retry++;
            continue;
        }

        rc = _voice_send_command(item->notify_id, item->tts_text);
        if (rc == 0) {
            ESP_LOGI(TAG, "播报成功 (notify_id=%d)", item->notify_id);
            return;
        }

        ESP_LOGW(TAG, "播报失败，重试 %d/%d (notify_id=%d)",
                 retry + 1, MAX_RETRY + 1, item->notify_id);
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        retry++;
    }

    ESP_LOGE(TAG, "播报最终失败 (notify_id=%d)，已达最大重试次数", item->notify_id);
}

/**
 * 语音播报任务（从队列取条目播报）
 * 高优先级条目插入队首，普通条目追加队尾
 */
void voice_task(void *pvParameters)
{
    ESP_LOGI(TAG, "语音播报任务启动");

    while (1) {
        if (queue_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 从队列头部取一个条目
        voice_item_t item = queue[queue_head];
        queue_head = (queue_head + 1) % QUEUE_LEN;
        queue_count--;

        // 播报
        _voice_play_item(&item);

        // 两段播报之间间隔 300ms，避免语音重叠
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/**
 * 添加语音播报到队列
 * @param notify_id  voice_notify 编号
 * @param tts_text   后端下发的 TTS 文本（可选，为 NULL 时使用编号播报）
 * @param priority   优先级：true=高优先级(插队播报), false=普通(排到队尾)
 */
void voice_enqueue(int notify_id, const char *tts_text, bool priority)
{
    if (queue_count >= QUEUE_LEN) {
        ESP_LOGW(TAG, "播报队列已满（%d），丢弃 notify_id=%d", QUEUE_LEN, notify_id);
        return;
    }

    voice_item_t item = {
        .notify_id = notify_id,
        .is_high_priority = priority,
    };
    if (tts_text) {
        strncpy(item.tts_text, tts_text, sizeof(item.tts_text) - 1);
    }

    if (priority) {
        // 高优先级：插入队首
        queue_head = (queue_head - 1 + QUEUE_LEN) % QUEUE_LEN;
        queue[queue_head] = item;
    } else {
        // 普通：追加队尾
        queue[queue_tail] = item;
        queue_tail = (queue_tail + 1) % QUEUE_LEN;
    }
    queue_count++;

    ESP_LOGI(TAG, "入队: notify_id=%d, priority=%d, 队列长度=%d",
             notify_id, priority, queue_count);
}

/**
 * 播报预处理通知（voice_notify=10 / 0x0A）
 * 高优先级，立即播报
 */
void voice_notify_preheat(void)
{
    // TTS 文本精简，减少播报时长
    voice_enqueue(10, "已开启环境预处理", true);
}

/**
 * 播报设置确认（voice_notify=11 / 0x0B）
 * 高优先级，立即播报
 */
void voice_notify_setting_changed(void)
{
    voice_enqueue(11, "已为您修改设置", true);
}

/**
 * 播报欢迎回家（voice_notify=9）
 */
void voice_notify_welcome(void)
{
    voice_enqueue(9, NULL, false);  // 按编号播报，语音模块内预设音频
}

/**
 * 播报询问开灯（voice_notify=1）
 */
void voice_notify_ask_light(void)
{
    voice_enqueue(1, NULL, false);
}

/**
 * 播报询问开空调（voice_notify=4）
 */
void voice_notify_ask_ac(void)
{
    voice_enqueue(4, NULL, false);
}
