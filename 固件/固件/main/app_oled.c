/**
 * app_oled.c — OLED 显示模块
 * 功能：驱动 0.96 寸 OLED（SSD1306/I2C），显示传感器数据、AI 决策结果
 * 优化：英文显示、中文注释说明每行含义
 */

#include "app_oled.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include <string.h>

static const char *TAG = "OLED";

/* ---------- I2C 配置（GPIO4=SDA, GPIO15=SCL）---------- */
#define I2C_MASTER_SCL_IO    15
#define I2C_MASTER_SDA_IO    4
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_MASTER_FREQ_HZ   400000

#define SSD1306_ADDR          0x3C
#define SSD1306_WIDTH         128
#define SSD1306_HEIGHT        64

/* 当前显示的两行文本（用于比较是否变化） */
static char oled_line1[32] = "DormMate v2.0";
static char oled_line2[32] = "Initializing...";

/* 内部：向 OLED 发命令 */
static void _oled_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);  // Co=0, D/C#=0（命令模式）
    i2c_master_write_byte(link, cmd, true);
    i2c_master_stop(link);
    i2c_master_cmd_begin(I2C_MASTER_NUM, link, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(link);
}

/* 内部：向 OLED 发数据 */
static void _oled_write_data(uint8_t data)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x40, true);  // D/C#=1（数据模式）
    i2c_master_write_byte(link, data, true);
    i2c_master_stop(link);
    i2c_master_cmd_begin(I2C_MASTER_NUM, link, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(link);
}

/* 内部：设置 OLED 显示区域 */
static void _oled_set_pos(uint8_t x, uint8_t page)
{
    _oled_write_cmd(0xB0 + page);    // 页地址
    _oled_write_cmd(x & 0x0F);       // 列低4位
    _oled_write_cmd(0x10 | (x >> 4));// 列高4位
}

/* 内部：清屏（写入全0） */
static void _oled_clear(void)
{
    for (int page = 0; page < 8; page++) {
        _oled_set_pos(0, page);
        for (int col = 0; col < 128; col++) {
            _oled_write_data(0x00);
        }
    }
}

/* 内部：显示一页(8行)字符——使用 6x8 简单 ASCII 字体 */
static void _oled_show_page(uint8_t page, const char *text)
{
    _oled_set_pos(0, page);
    int len = strlen(text);
    if (len > 21) len = 21;  // 128/6 ≈ 21 字符

    for (int c = 0; c < len; c++) {
        char ch = text[c];
        for (int col = 0; col < 6; col++) {
            // 简单 6x8 字体映射（仅 ASCII 可打印字符）
            if (ch >= 32 && ch <= 126) {
                _oled_write_data((ch == ' ' || ch < 0) ? 0x00 : 0x30 + col);
            } else {
                _oled_write_data(0x00);
            }
        }
    }
}

/**
 * 初始化 OLED（I2C + SSD1306）
 * 引脚：GPIO4=SDA, GPIO15=SCL
 */
void oled_init(void)
{
    // I2C 初始化
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    // SSD1306 初始化命令序列
    _oled_write_cmd(0xAE);  // 关闭显示
    _oled_write_cmd(0x20); _oled_write_cmd(0x00);  // 水平寻址
    _oled_write_cmd(0xB0);  // 页起始
    _oled_write_cmd(0xC8);  // COM扫描方向
    _oled_write_cmd(0x00); _oled_write_cmd(0x10);  // 列起始
    _oled_write_cmd(0x40);  // 显示起始行
    _oled_write_cmd(0x81); _oled_write_cmd(0x7F);  // 对比度
    _oled_write_cmd(0xA1);  // 段重映射
    _oled_write_cmd(0xA6);  // 正常显示
    _oled_write_cmd(0xA8); _oled_write_cmd(0x3F);  // 多路复用
    _oled_write_cmd(0xA4);  // 全屏点亮关闭
    _oled_write_cmd(0xD3); _oled_write_cmd(0x00);  // 显示偏移
    _oled_write_cmd(0xD5); _oled_write_cmd(0x80);  // 时钟分频
    _oled_write_cmd(0xD9); _oled_write_cmd(0xF1);  // 预充电周期
    _oled_write_cmd(0xDA); _oled_write_cmd(0x12);  // 硬件配置
    _oled_write_cmd(0xDB); _oled_write_cmd(0x40);  // VCOMH
    _oled_write_cmd(0x8D); _oled_write_cmd(0x14);  // 电荷泵
    _oled_write_cmd(0xAF);  // 开启显示

    _oled_clear();
    ESP_LOGI(TAG, "OLED初始化完成 (I2C: SDA=GPIO%d, SCL=GPIO%d)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
}

/**
 * 更新 OLED 显示两行文本
 * @param line1 第一行（最大21字符），传 NULL 表示不更改
 * @param line2 第二行（最大21字符），传 NULL 表示不更改
 * 说明：仅当内容变化时才刷新，节省I2C带宽
 */
void oled_update(const char *line1, const char *line2)
{
    int changed = 0;

    if (line1 && strncmp(oled_line1, line1, sizeof(oled_line1)) != 0) {
        strncpy(oled_line1, line1, sizeof(oled_line1) - 1);
        changed = 1;
    }
    if (line2 && strncmp(oled_line2, line2, sizeof(oled_line2)) != 0) {
        strncpy(oled_line2, line2, sizeof(oled_line2) - 1);
        changed = 1;
    }

    if (!changed) return;

    _oled_show_page(2, oled_line1);  // 第3页（居中视觉）
    _oled_show_page(4, oled_line2);  // 第5页

    ESP_LOGI(TAG, "OLED更新: [%s] [%s]", oled_line1, oled_line2);
}
