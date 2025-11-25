#pragma once

#define PI 3.14159265358979323846
#define REFRESH_TIME 100 // 刷新周期 ms
#define CHART_WIDTH (LV_HOR_RES - 200)
#define CHART_HEIGHT (LV_VER_RES - 350)
#define Y_SCALE 1024 // Y轴缩放因子（实际值放大1024倍处理浮点）

const int16_t DISPLAY_DISPLAY_COUNT = 200; // 显示点个数
const int16_t CHART_BOTTOM_MARGIN   = 100;
const int16_t GRID_X_COUNT          = 5; // X轴网格线数量
const int16_t GRID_Y_COUNT          = 4; // Y轴网格线数量
const uint16_t TOUCH_DEBOUNCE_MS    = 20;

lv_obj_t * ref_label;
lv_obj_t * err_label;