#pragma once

#include "config.h"
#include "blelib.h"

#if CONFIG_APP_CLIENT

typedef void(*ui_btn_callback_t)(void);

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

extern uint8_t ui_self_mac_address[6];
extern char ui_self_mac_string[5];

void ui_add_alarmdev(const struct ui_alarmdev *dev);
uint16_t ui_get_short_mac(uint8_t mac[6]);

void ui_init_buttons(   ui_btn_callback_t single_click,
                        ui_btn_callback_t double_click,
                        ui_btn_callback_t lpress_start,
                        ui_btn_callback_t lpress_stop   );
void ui_init();
void ui_showpage_launch();
void ui_showpage_main();
void ui_showpage_settings(uint8_t button_index);
void ui_showpage_settingsunit(uint8_t button_index, uint8_t settings_index);

#endif // CONFIG_APP_CLIENT