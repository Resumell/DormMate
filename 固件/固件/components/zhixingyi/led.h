#ifndef RELAY_LIGHT_H
#define RELAY_LIGHT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 引脚定义
 * ============================================================ */
#define RELAY_LIGHT_PIN     10      /* GPIO10 - 台灯继电器 */

/* ============================================================
 * 状态定义
 * ============================================================ */
#define RELAY_ON            1       /* 继电器吸合 (灯亮) */
#define RELAY_OFF           0       /* 继电器释放 (灯灭) */

/* ============================================================
 * 函数声明
 * ============================================================ */

/**
 * @brief 初始化台灯继电器
 * @return ESP_OK 成功, 其他失败
 */
esp_err_t relay_light_init(void);

/**
 * @brief 开灯
 */
void relay_light_on(void);

/**
 * @brief 关灯
 */
void relay_light_off(void);

/**
 * @brief 切换灯状态
 */
void relay_light_toggle(void);

/**
 * @brief 获取灯状态
 * @return RELAY_ON 或 RELAY_OFF
 */
int relay_light_get(void);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_LIGHT_H */
