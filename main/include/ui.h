#pragma once

#include "config.h"

#if CONFIG_APP_CLIENT

typedef void(*ui_btn_callback_t)(void);

void ui_init_buttons(   ui_btn_callback_t single_click,
                        ui_btn_callback_t double_click,
                        ui_btn_callback_t lpress_start,
                        ui_btn_callback_t lpress_stop   );
void ui_init();
void ui_showpage_launch();
void ui_showpage_main();
void ui_showpage_settings(uint8_t subpage_index);

#endif // CONFIG_APP_CLIENT