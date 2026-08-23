#pragma once

#include "config.h"
#include "blelib.h"

#if CONFIG_APP_CLIENT

//extern const uint8_t ui_font_wqy12_cn[];
//extern const uint8_t u8g2_font_5x7_tf[1612];

enum ui_btn_event {
    UI_BTN_NOEVENT,
    UI_BTN_SINGLE_CLICK,
    //UI_BTN_DOUBLE_CLICK,
    UI_BTN_LONGPRESS_START,
    UI_BTN_LONGPRESS_END
};

/// @brief 按钮回调函数指针类型
/// ### 参数
/// 1. `phbutton` 输入指针，实际类型为 `button_handle_t`
/// 2. `u32event` 整数，实际类型为 `enum ui_btn_event`
typedef void(*ui_btn_callback_t)(void *phbutton, void *u32event);

struct ui_alarmdev {
    uint16_t short_mac;
    int8_t rssi;
    uint8_t alarm_type;
    union {
        uint32_t recv_time_s;
        int day_sec;
    } time;
    union {
        uint32_t alarm_id;
        float vbat;
    } data;
};

struct ui_respdev {
    uint16_t short_mac;
    int8_t rssi;
    uint8_t __reserved;
};

extern uint8_t ui_self_mac_address[6];
extern char ui_self_mac_string[5];

void ui_add_alarmdev(const struct ui_alarmdev *dev);
void ui_clear_resp_list();
void ui_add_resp_dev(struct ui_respdev dev);
uint16_t ui_get_short_mac(const uint8_t mac[6]);

void ui_init_buttons(ui_btn_callback_t callback);
void ui_init();
void ui_showpage_launch();
void ui_showpage_home();
void ui_showpage_settings(uint8_t settings_index);
void ui_showpage_settingsunit(uint8_t settings_index, uint8_t button_index);
void ui_showpage_transmitting();

#endif // CONFIG_APP_CLIENT