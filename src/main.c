#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "lib/linux_msg.h"

#define USING_MOUSE 0
#define USING_TOUCHSCREEN 1

#define PI 3.14159265358979323846
#define REFRESH_TIME 100 // 刷新周期 ms

#define DISPLAY_DISPLAY_COUNT 200 // 显示据点个数
#define Y_SCALE 1024              // Y轴缩放因子（实际值放大1024倍处理浮点）
#define CHART_WIDTH (LV_HOR_RES - 200)
#define CHART_HEIGHT (LV_VER_RES - 500)
#define CHART_BOTTOM_MARGIN 100
#define GRID_X_COUNT 5 // X轴网格线数量
#define GRID_Y_COUNT 4 // Y轴网格线数量
#define TOUCH_DEBOUNCE_MS 20

// 全局变量
static lv_obj_t * chart;
static lv_timer_t * update_timer;
static lv_chart_series_t * ref_signal_line;
static lv_chart_series_t * err_signal_line;
static lv_display_t * disp;
extern bool has_new_ref_signal;
extern bool has_new_err_signal;
extern int16_t ref_signal_array[200];
extern int16_t err_signal_array[200];

typedef struct
{
    int fd;
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    bool calibrated;
} TouchpadData;

static TouchpadData touchpad = {.fd = -1, .min_x = 0, .max_x = 0, .min_y = 0, .max_y = 0, .calibrated = false};

void get_sin_array(int16_t * array, size_t size, double frequency, double amplitude, double phase)
{
    for(size_t i = 0; i < size; i++) {
        double t = (double)i / (double)size;                                             // 归一化时间
        array[i] = (int16_t)(amplitude * sin(2 * PI * frequency * t + phase) * Y_SCALE); // 放大Y轴
    }
}

// 波形图更新函数
// LVGL的定时器回调函数必须遵循预定义的类型签名void (*lv_timer_cb_t)(lv_timer_t *timer)，无论函数内部是否使用参数
void update_chart(lv_timer_t * timer)
{
    (void)timer;
    float voltage;
    static int32_t converted_ref_values[DISPLAY_DISPLAY_COUNT];
    static int32_t converted_err_values[DISPLAY_DISPLAY_COUNT];

    // 转换传感器数据
    if(has_new_ref_signal == true) {
        for(int i = 0; i < DISPLAY_DISPLAY_COUNT; i++) {
            voltage                 = (int)ref_signal_array[i] * 10.0 / 32767.0f; // 32768 = 0x8000
            converted_ref_values[i] = (int32_t)(voltage / 10.0 * (Y_SCALE - 20));
        }
        has_new_ref_signal = false;

        lv_chart_set_series_values(chart, ref_signal_line, converted_ref_values, DISPLAY_DISPLAY_COUNT);
        lv_chart_refresh(chart);
    }

    if(has_new_err_signal == true) {
        for(int i = 0; i < DISPLAY_DISPLAY_COUNT; i++) {
            voltage = (int)err_signal_array[i + DISPLAY_DISPLAY_COUNT] * 10.0 / 32767.0f; // 32768 = 0x8000
            converted_err_values[i] = (int32_t)(voltage / 10.0 * (Y_SCALE - 20));
        }
        has_new_err_signal = false;

        lv_chart_set_series_values(chart, err_signal_line, converted_err_values, DISPLAY_DISPLAY_COUNT);
        lv_chart_refresh(chart);
    }
}

// 按钮事件处理
void btn_event_handler(lv_event_t * e)
{
    lv_obj_t * btn     = lv_event_get_target(e);
    const char * label = lv_label_get_text(lv_obj_get_child(btn, 0));

    if(strcmp(label, "Start excitation") == 0) {
        if(update_timer) {
            lv_timer_resume(update_timer);
        } else {
            update_timer = lv_timer_create(update_chart, REFRESH_TIME, NULL);
            if(update_timer == NULL) {
                printf("Error: Failed to create update timer\n");
                return;
            }
        }
        send_msg(CMD_START_EXCITATION, 0, 0);
    } else if(strcmp(label, "Stop excitation") == 0) {
        if(update_timer) {
            lv_timer_pause(update_timer);
        }
        send_msg(CMD_STOP_EXCITATION, 0, 0);
    } else if(strcmp(label, "Start control") == 0) {
        send_msg(CMD_START_CONTROL, 0, 0);
    } else if(strcmp(label, "Stop control") == 0) {
        send_msg(CMD_STOP_CONTROL, 0, 0);
    }
}

void create_axis_labels()
{
    // static lv_color_t color_red;
    // static lv_color_t color_blue;
    // static lv_color_t color_green;
    static lv_color_t color_black;

    // color_red   = lv_color_hex(0xFF0000); // 红色
    // color_blue  = lv_color_hex(0x0000FF); // 蓝色
    // color_green = lv_color_hex(0x00FF00); // 绿色
    color_black = lv_color_hex(0x000000); // 黑色
    // 获取图表位置和尺寸
    lv_area_t chart_area;
    lv_obj_get_coords(chart, &chart_area);
    printf("Chart area: x1=%d, y1=%d, x2=%d, y2=%d\n", chart_area.x1, chart_area.y1, chart_area.x2, chart_area.y2);

    // 获取图表画布区域（排除边距）
    lv_coord_t chart_width  = lv_area_get_width(&chart_area);
    lv_coord_t chart_height = lv_area_get_height(&chart_area);

    // 创建X轴标签
    const char * x_labels[] = {"0", "40", "80", "120", "160", "200"};
    for(int i = 0; i < 6; i++) {
        lv_obj_t * label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, x_labels[i]);
        // 计算X轴标签位置
        lv_coord_t x_pos = chart_area.x1 + (chart_width * i / 5);
        lv_coord_t y_pos = chart_area.y2 + 20;                        // 在图表下方20像素
        lv_obj_set_pos(label, x_pos - 10, y_pos);                     // 调整微调位置
        lv_obj_set_style_text_color(label, color_black, 0);           // 设置标签颜色
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0); // 设置字体
    }

    // 创建Y轴标签
    const char * y_labels[] = {"10", "5", "0", "-5", "-10"};
    for(int i = 0; i < 5; i++) {
        lv_obj_t * label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, y_labels[i]);
        // 计算Y轴标签位置（从顶部开始计算）
        lv_coord_t y_pos = chart_area.y1 + (chart_height * i / 4);
        lv_obj_set_pos(label, chart_area.x1 - 30, y_pos - 10);        // 在图表左侧30像素
        lv_obj_set_style_text_color(label, color_black, 0);           // 设置标签颜色
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0); // 设置字体
    }

    // 添加轴标题
    lv_obj_t * x_title = lv_label_create(lv_scr_act());
    lv_label_set_text(x_title, "Time (ms)");
    lv_obj_align_to(x_title, chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 40);
    lv_obj_set_style_text_color(x_title, color_black, 0);
    lv_obj_set_style_text_font(x_title, &lv_font_montserrat_24, 0);

    lv_obj_t * y_title = lv_label_create(lv_scr_act());
    lv_label_set_text(y_title, "Voltage (V)");
    lv_obj_align_to(y_title, chart, LV_ALIGN_OUT_LEFT_MID, -20, 0);
    lv_obj_set_style_text_color(y_title, color_black, 0);
    lv_obj_set_style_transform_angle(y_title, -900, 0); // 旋转90度
    lv_obj_set_style_text_font(y_title, &lv_font_montserrat_24, 0);
}

void create_chart(void)
{
    // 创建图表对象
    chart = lv_chart_create(lv_scr_act());
    lv_obj_set_size(chart, CHART_WIDTH, CHART_HEIGHT);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, -CHART_BOTTOM_MARGIN);

    // 设置图表类型为折线图,更新模式为CIRCULAR
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);

    // 设置网格线数量
    lv_chart_set_div_line_count(chart, GRID_Y_COUNT + 1, GRID_X_COUNT + 1);
    // 设置显示点数
    lv_chart_set_point_count(chart, DISPLAY_DISPLAY_COUNT);

    // 设置坐标轴轴范围
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -1.0 * Y_SCALE, 1.0 * Y_SCALE);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, DISPLAY_DISPLAY_COUNT);

    // 添加数据系列（红色波形）
    ref_signal_line = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, ref_signal_line, 0);
    // 添加数据系列（蓝色波形）
    err_signal_line = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, err_signal_line, 0);

    // -------------------------- 手动创建图例（无legend组件适配） --------------------------
    // 1. 创建图例容器（父组件为屏幕，统一管理条目）
    lv_obj_t * legend_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(legend_container, 300, 40); // 容器大小（适配2个条目）
    lv_obj_set_flex_flow(legend_container, LV_FLEX_FLOW_ROW); // 水平排列条目
    lv_obj_set_flex_align(legend_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // 均匀分布+垂直居中
    lv_obj_align_to(legend_container, chart, LV_ALIGN_TOP_MID, 0, -10); // 相对于图表顶部居中，上偏10px

    // 2. 可选：容器样式（白色半透明背景，圆角）
    lv_obj_set_style_bg_color(legend_container, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(legend_container, LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(legend_container, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(legend_container, 4, LV_STATE_DEFAULT);

    // 3. 创建第一个条目：参考信号（红色）
    // 3.1 颜色块（小矩形，模拟legend颜色标识）
    lv_obj_t * ref_color_block = lv_obj_create(legend_container);
    lv_obj_set_size(ref_color_block, 16, 16); // 颜色块大小（16x16px，醒目）
    lv_obj_set_style_bg_color(ref_color_block, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT); // 复用数据线颜色
    lv_obj_set_style_bg_opa(ref_color_block, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ref_color_block, 2, LV_STATE_DEFAULT); // 轻微圆角

    // 3.2 文本标签
    lv_obj_t * ref_label = lv_label_create(legend_container);
    lv_label_set_text(ref_label, "Reference Signal"); // 英文名称（中文："参考信号"）
    lv_obj_set_style_text_font(ref_label, &lv_font_montserrat_16, LV_STATE_DEFAULT); // 字体大小

    // 4. 创建第二个条目：误差信号（蓝色）
    // 4.1 颜色块
    lv_obj_t * err_color_block = lv_obj_create(legend_container);
    lv_obj_set_size(err_color_block, 16, 16);
    lv_obj_set_style_bg_color(err_color_block, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_DEFAULT); // 复用数据线颜色
    lv_obj_set_style_bg_opa(err_color_block, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(err_color_block, 2, LV_STATE_DEFAULT);

    // 4.2 文本标签
    lv_obj_t * err_label = lv_label_create(legend_container);
    lv_label_set_text(err_label, "Error Signal"); // 英文名称（中文："误差信号"）
    lv_obj_set_style_text_font(err_label, &lv_font_montserrat_16, LV_STATE_DEFAULT);
    // --------------------------------------------------------------------------------

    lv_obj_update_layout(chart);
    // 创建刻度标签
    create_axis_labels();
}

// 自动校准触摸屏范围
bool touchpad_auto_calibrate(void)
{
    struct input_absinfo abs_info;

    // 获取X轴范围
    if(ioctl(touchpad.fd, EVIOCGABS(ABS_X), &abs_info) < 0) {
        perror("Failed to get X range");
        return false;
    }
    touchpad.min_x = abs_info.minimum;
    touchpad.max_x = abs_info.maximum;

    // 获取Y轴范围
    if(ioctl(touchpad.fd, EVIOCGABS(ABS_Y), &abs_info) < 0) {
        perror("Failed to get Y range");
        return false;
    }
    touchpad.min_y = abs_info.minimum;
    touchpad.max_y = abs_info.maximum;

    touchpad.calibrated = true;
    return true;
}

// 初始化触摸屏
bool touchpad_init(void)
{
    touchpad.fd = open("/dev/input/touchscreen0", O_RDONLY | O_NONBLOCK);
    if(touchpad.fd < 0) {
        perror("Failed to open touchpad device");
        return false;
    }

    // 尝试自动获取触摸屏范围
    if(touchpad_auto_calibrate()) {
        printf("Touchpad auto-calibrated: X(%d-%d), Y(%d-%d)\n", touchpad.min_x, touchpad.max_x, touchpad.min_y,
               touchpad.max_y);
    } else {
        // 使用默认值（常见触摸屏范围）
        touchpad.min_x      = 0;
        touchpad.max_x      = 4095;
        touchpad.min_y      = 0;
        touchpad.max_y      = 4095;
        touchpad.calibrated = false;
        printf("Using default touchpad range: X(0-4095), Y(0-4095)\n");
    }

    return true;
}

// 输入设备回调函数
void indev_callback(lv_indev_t * indev, lv_indev_data_t * data)
{
#if USING_MOUSE
    static int16_t last_x        = 0;
    static int16_t last_y        = 0;
    static bool left_button_down = false;

    struct input_event in;
    static int fd = -1;

    if(fd < 0) {
        fd = open("/dev/input/mouse", O_RDONLY | O_NONBLOCK);
        if(fd < 0) {
            perror("Failed to open mouse device");
            data->point.x = last_x;
            data->point.y = last_y;
            data->state   = LV_INDEV_STATE_RELEASED;
            return;
        }
    }

    while(read(fd, &in, sizeof(struct input_event)) > 0) {
        if(in.type == EV_REL) {
            if(in.code == REL_X)
                last_x += in.value;
            else if(in.code == REL_Y)
                last_y += in.value;
        } else if(in.type == EV_KEY && in.code == BTN_LEFT) {
            left_button_down = (in.value != 0);
        }
    }

    // 更新LVGL输入数据
    data->point.x = last_x;
    data->point.y = last_y;
    data->state   = left_button_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
#elif USING_TOUCHSCREEN
    static int16_t last_x           = 0;
    static int16_t last_y           = 0;
    static bool touched             = false;
    static uint32_t last_touch_time = 0;

    struct input_event in;

    (void)indev;

    if(touchpad.fd < 0 && !touchpad_init()) {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state   = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }

    // 读取所有事件
    while(read(touchpad.fd, &in, sizeof(struct input_event)) > 0) {
        if(in.type == EV_ABS) {
            if(in.code == ABS_X) {
                // 使用校准数据映射X坐标
                if(touchpad.calibrated) {
                    last_x = ((in.value - touchpad.min_x) * LV_HOR_RES) / (touchpad.max_x - touchpad.min_x);
                } else {
                    // 默认映射（假设常见范围）
                    last_x = (in.value * LV_HOR_RES) / 4096;
                }

                // 边界检查
                last_x = LV_CLAMP(0, last_x, LV_HOR_RES - 1);
            } else if(in.code == ABS_Y) {
                // 使用校准数据映射Y坐标
                if(touchpad.calibrated) {
                    last_y = ((in.value - touchpad.min_y) * LV_VER_RES) / (touchpad.max_y - touchpad.min_y);
                } else {
                    // 默认映射（假设常见范围）
                    last_y = (in.value * LV_VER_RES) / 4096;
                }

                // 边界检查
                last_y = LV_CLAMP(0, last_y, LV_VER_RES - 1);
            }
        } else if(in.type == EV_KEY && in.code == BTN_TOUCH) {
            // 应用消抖逻辑
            bool raw_touched = (in.value != 0);
            if(raw_touched != touched && lv_tick_elaps(last_touch_time) > TOUCH_DEBOUNCE_MS) {
                touched         = raw_touched;
                last_touch_time = lv_tick_get();
            }
        }
    }

    // 更新LVGL数据
    data->point.x = last_x;
    data->point.y = last_y;
    data->state   = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
#endif
}

// 初始化输入设备
void input_evdev_init(void)
{
#if USING_MOUSE
    lv_indev_t * mouse;
    lv_obj_t cursor_obj;

    mouse = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
    lv_indev_set_display(mouse, disp);
    lv_indev_set_read_cb(mouse_indev, indev_callback);
    LV_IMAGE_DECLARE(mouse_cursor_icon);
    cursor_obj = lv_image_create(lv_screen_active());
    lv_image_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(mouse, cursor_obj);
#elif USING_TOUCHSCREEN
    lv_indev_t * touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/touchscreen0");
    lv_indev_set_display(touch, disp);
    lv_indev_set_read_cb(touch, indev_callback);

#endif
}

// 创建UI界面
void create_button_ui(void)
{
    // 创建按钮容器
    lv_obj_t * btn_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(btn_container, LV_HOR_RES - 200, 160);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(btn_container, LV_ALIGN_TOP_MID, 0, 10);

    // 开始激励按钮
    lv_obj_t * btn_start_excitation = lv_btn_create(btn_container);
    lv_obj_set_size(btn_start_excitation, 200, 120);
    lv_obj_set_style_bg_color(btn_start_excitation, lv_palette_lighten(LV_PALETTE_BLUE, 3), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_start_excitation, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_start_excitation, btn_event_handler, LV_EVENT_CLICKED, NULL);
    // 添加文本信息
    lv_obj_t * label_start_excitation = lv_label_create(btn_start_excitation);
    lv_label_set_text(label_start_excitation, "Start excitation");                           // 设置文本
    lv_obj_set_style_text_font(label_start_excitation, &lv_font_montserrat_22, 0);           // 设置字体
    lv_obj_set_style_text_color(label_start_excitation, lv_color_black(), LV_STATE_DEFAULT); // 文本颜色黑色
    lv_obj_center(label_start_excitation);                                                   // 文本居中

    // 结束激励按钮
    lv_obj_t * btn_stop_excitation = lv_btn_create(btn_container);
    lv_obj_set_size(btn_stop_excitation, 200, 120);
    lv_obj_set_style_bg_color(btn_stop_excitation, lv_palette_lighten(LV_PALETTE_BLUE, 3), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_stop_excitation, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_stop_excitation, btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t * label_stop_excitation = lv_label_create(btn_stop_excitation);
    lv_label_set_text(label_stop_excitation, "Stop excitation");
    lv_obj_set_style_text_font(label_stop_excitation, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(label_stop_excitation, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_center(label_stop_excitation);

    // 开始控制按钮
    lv_obj_t * btn_start_control = lv_btn_create(btn_container);
    lv_obj_set_size(btn_start_control, 200, 120);
    lv_obj_add_event_cb(btn_start_control, btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_start_control, lv_palette_lighten(LV_PALETTE_BLUE, 3), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_start_control, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_t * label_start_control = lv_label_create(btn_start_control);
    lv_label_set_text(label_start_control, "Start control");
    lv_obj_set_style_text_font(label_start_control, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(label_start_control, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_center(label_start_control);

    // 结束控制按钮
    lv_obj_t * btn_stop_control = lv_btn_create(btn_container);
    lv_obj_set_size(btn_stop_control, 200, 120);
    lv_obj_add_event_cb(btn_stop_control, btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_stop_control, lv_palette_lighten(LV_PALETTE_BLUE, 3), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_stop_control, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_t * label_stop_control = lv_label_create(btn_stop_control);
    lv_label_set_text(label_stop_control, "Stop control");
    lv_obj_set_style_text_font(label_stop_control, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(label_stop_control, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_center(label_stop_control);
}

// 主函数
int main(void)
{
    // 初始化LVGL
    lv_init();

    disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");

    // 创建UI界面
    create_button_ui();
    create_chart();
    input_evdev_init();

    update_timer = lv_timer_create(update_chart, REFRESH_TIME, NULL);
    lv_timer_enable(update_timer); // 启动定时器

    printf("UI created successfully.\n");
    printf("LV_HOR_RES=%d, LV_VER_RES =%d\n", LV_HOR_RES, LV_VER_RES);

    if(start_rpmsg() != EXIT_SUCCESS) {
        printf("start_rpmsg failed!\n");
        return 0;
    }

    // 主循环
    while(1) {
        lv_timer_handler();
        usleep(500);
    }

    return 0;
}