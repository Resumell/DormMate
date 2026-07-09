/**
 * app_predict.h — 回寝预测/预处理模块头文件
 */

#ifndef APP_PREDICT_H
#define APP_PREDICT_H

#include <stdbool.h>

int predict_should_preheat(void);
void predict_set_triggered(void);
void predict_reset_daily(void);

#endif
