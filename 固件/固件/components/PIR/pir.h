#ifndef PIR_H
#define PIR_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化PIR人体感应传感器 (中断模式)
 * @param gpio_pin GPIO引脚号
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t pir_init(int gpio_pin);

/**
 * @brief 获取触发总次数 (检测到人的次数)
 * @return 触发次数
 */
int pir_get_count(void);

/**
 * @brief 获取当前是否有人
 * @return 1=有人, 0=无人
 */
int pir_get_state(void);

/**
 * @brief 获取距离首次触发的秒数
 * @return -1=从未触发, >=0=秒数
 */
int64_t pir_get_first_sec(void);

/**
 * @brief 重置计数器和时间戳
 */
void pir_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PIR_H */
