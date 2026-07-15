#include "app_oled.h"
#include "esp_log.h"

extern esp_err_t oled_init(int sda_pin, int scl_pin, uint8_t i2c_addr);
extern void oled_clear(void);
extern void oled_show_string(uint8_t line, const char *str);
extern esp_err_t oled_refresh(void);

static const char* TAG = "OLED";

void oled_init(void)
{
    esp_err_t ret = oled_init(4, 5, 0x3C);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED 初始化失败");
    } else {
        ESP_LOGI(TAG, "OLED 初始化完成");
    }
}

void oled_show(const char* line1, const char* line2)
{
    oled_clear();
    if (line1 && strlen(line1) > 0) {
        oled_show_string(0, line1);
    }
    if (line2 && strlen(line2) > 0) {
        oled_show_string(2, line2);
    }
    oled_refresh();
    ESP_LOGI(TAG, "OLED显示: [%s] / [%s]", line1 ? line1 : "", line2 ? line2 : "");
}