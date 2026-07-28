#include "config.h"
#if CONFIG_APP_CLIENT

#include "misc.h"
#include "blelib.h"
#include "repl.h"
#include "settings.h"
#include "ui.h"

#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "main";

static bool btn_single_click = false;
static bool btn_double_click = false;
static bool btn_lpress_start = false;
static bool btn_lpress_stop = false;

static void app_btn_single_click(){
    ESP_LOGI(TAG, "btn: single");
    btn_single_click = true;
}

static void app_btn_double_click(){
    ESP_LOGI(TAG, "btn: double");
    btn_double_click = true;
}

static void app_btn_lpress_start(){
    ESP_LOGI(TAG, "btn: long");
    btn_lpress_start = true;
}

static void app_btn_lpress_stop(){
    ESP_LOGI(TAG, "btn: release");
    btn_lpress_stop = true;
}

void app_init(){
    println(FIRMWARE_TYPE_STRING "_" FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION);

    misc_gpio_init(PIN_OUTPUT, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    misc_gpio_init(PIN_LED, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    misc_gpio_init(PIN_CMDLINE, GPIO_MODE_INPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);

    misc_vbat_init();
    misc_init_nvs();
    misc_vibration_init();

    blelib_init();

    settings_init();
    settings_load();

    ui_init_buttons(app_btn_single_click, app_btn_double_click, app_btn_lpress_start, app_btn_lpress_stop);

    if (gpio_get_level(PIN_CMDLINE)==0){
        repl_init();
        settings_addcmds();
        repl_begin();
    }

    ui_init();
}

void app_main(){
    app_init();
    ESP_LOGI(TAG, "end init");

    ui_showpage_launch();
    ui_showpage_main();

    /// @todo
    for (;;){
        misc_delay_ms(1000);
    }
}
#endif