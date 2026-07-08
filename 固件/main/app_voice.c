#include "app_voice.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "app_control.h"
#include "string.h"

#define VOICE_UART      UART_NUM_1
#define VOICE_TX_PIN    7
#define VOICE_RX_PIN    6
#define VOICE_BUF_SIZE  256

static const char *TAG = "VOICE";

void voice_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(VOICE_UART, VOICE_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(VOICE_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(VOICE_UART, VOICE_TX_PIN, VOICE_RX_PIN, 
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "语音模块 UART1 初始化完成，TX=GPIO%d, RX=GPIO%d", 
             VOICE_TX_PIN, VOICE_RX_PIN);
}

void voice_send_cmd(uint8_t msg_id)
{
    uint8_t frame[5] = {0xAA, 0x55, msg_id, 0x55, 0xAA};
    uart_write_bytes(VOICE_UART, (const char*)frame, 5);
    ESP_LOGI(TAG, "发送语音触发帧: AA 55 %02X 55 AA", msg_id);
}

static void voice_parse_frame(uint8_t *data, int len)
{
    if (len < 5) return;
    
    for (int i = 0; i <= len - 5; i++) {
        if (data[i] == 0xAA && data[i+1] == 0x55 && 
            data[i+3] == 0x55 && data[i+4] == 0xAA) {
            
            uint8_t cmd = data[i+2];
            ESP_LOGI(TAG, "收到语音命令: 0x%02X", cmd);
            
            ActionData_t action;
            memset(&action, 0, sizeof(action));
            action.servo = -1;
            
            switch (cmd) {
                case 0x08:  /* "我回来了" - home_init，直接开灯，不经过AI */
                    action.relay = 1;
                    strncpy(action.oled_line1, "Welcome Home", sizeof(action.oled_line1)-1);
                    ESP_LOGI(TAG, "执行: 回家模式-直接开灯");
                    execute_action(&action);
                    break;
                    
                case 0x02:  /* "开灯" */
                    action.relay = 1;
                    strncpy(action.oled_line1, "Voice: Light ON", sizeof(action.oled_line1)-1);
                    ESP_LOGI(TAG, "执行: 开灯");
                    execute_action(&action);
                    break;
                    
                case 0x03:  /* "关灯" */
                    action.relay = 0;
                    strncpy(action.oled_line1, "Voice: Light OFF", sizeof(action.oled_line1)-1);
                    ESP_LOGI(TAG, "执行: 关灯");
                    execute_action(&action);
                    break;
                    
                case 0x05:  /* "开空调" */
                    action.relay = 1;
                    strncpy(action.oled_line1, "Voice: AC ON", sizeof(action.oled_line1)-1);
                    ESP_LOGI(TAG, "执行: 开空调");
                    execute_action(&action);
                    break;
                    
                case 0x06:  /* "关空调" */
                    action.relay = 0;
                    strncpy(action.oled_line1, "Voice: AC OFF", sizeof(action.oled_line1)-1);
                    ESP_LOGI(TAG, "执行: 关空调");
                    execute_action(&action);
                    break;
                    
                default:
                    ESP_LOGW(TAG, "未知命令: 0x%02X", cmd);
                    break;
            }
            return;
        }
    }
}

void voice_task(void *pv)
{
    uint8_t buf[VOICE_BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(VOICE_UART, buf, VOICE_BUF_SIZE, 
                                  pdMS_TO_TICKS(100));
        if (len > 0) {
            ESP_LOGI(TAG, "UART 收到 %d 字节", len);
            voice_parse_frame(buf, len);
        }
    }
}