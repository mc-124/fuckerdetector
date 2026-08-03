#include "config.h"
#if CONFIG_APP_CLIENT

#include "misc.h"
#include "blelib.h"
#include "repl.h"
#include "settings.h"
#include "ui.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "main";

/*
    main:
        single -> transmit alarm
        double -> transmit violance alarm
        long -> begin settings
    settings:
        single -> next
        double -> entry
        long -> return
    settingsunit:
        single -> click
        double -> next
        long -> return
*/

enum {
    PAGE_MAIN,
    PAGE_SETTINGS,
    PAGE_SETTINGSUNIT,
    PAGE_TRANSMITTING,
};

#define APP_UI_UPDATE_COUNTDOWN_MAX 10*30

enum {
    DO_NONE,
    DO_TRANSMIT_ALARM,
    DO_TRANSMIT_VIOLANCE_ALARM,
};

static uint8_t app_op = 0;
static uint8_t app_cur_page = 0;
static uint8_t app_settings_index = 0;
static uint32_t app_ui_update_countdown = 0;

const static struct settings_config_desc *app_settings_cur_desc = NULL;
static uint8_t app_settings_cur_value = 0;
static uint8_t app_settingsunit_button = 0;
static struct blelib_adv_manfacturer_data app_manfacturer_data = {
    .company_id = CONFIG_APP_BLE_COMPANY_ID,
    .type = 0,
    .encoded_vbat = 0,
    .data.adv_id = 0,
    .protocol_ver = CONFIG_APP_BLE_PROTOCOL_VER
};
static uint32_t app_adv_id = 0;
static TaskHandle_t app_htask_ble = NULL;
static bool app_task_ble_working = false;

#define APP_UPDATE_UI() do {app_ui_update_countdown = 0; } while(0)

static void app_btn_single_click(){
    ESP_LOGI(TAG, "btn: single");
    switch (app_cur_page){
    case PAGE_MAIN:
        app_op = DO_TRANSMIT_ALARM;
        break;
    case PAGE_SETTINGS:
        if (app_settings_index++==SETTINGS_SET_NUM){ // 允许索引等于 SETTINGS_SET_NUM
            app_settings_index = 0;
        }
        APP_UPDATE_UI();
        break;
    case PAGE_SETTINGSUNIT:
        assert(app_settings_cur_desc);
        if (settings_field_is_bool(app_settings_cur_desc)){
            app_settings_cur_value = !app_settings_cur_value;
            APP_UPDATE_UI();
        } else if (
            app_settings_index==0
            &&app_settings_cur_value<app_settings_cur_desc->max_value
        ){
            // add
            app_settings_cur_value++;
            APP_UPDATE_UI();
        } else if (
            app_settings_index==1
            &&app_settings_cur_value>app_settings_cur_desc->min_value
        ){
            // sub
            app_settings_cur_value--;
            APP_UPDATE_UI();
        } else {
            ESP_LOGE(TAG, 
                "unknown error. val=%hhu index=%hhu btn=%hhu", 
                app_settings_cur_value, 
                app_settings_index, 
                app_settingsunit_button
            );
            ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);
        }
        break;
    case PAGE_TRANSMITTING:
        break;
    default:
        ESP_LOGE(TAG, "invalid page: %hhu", app_cur_page);
        break;
    }
}

static void app_btn_double_click(){
    ESP_LOGI(TAG, "btn: double");
    switch (app_cur_page){
    case PAGE_MAIN:
        app_op = DO_TRANSMIT_VIOLANCE_ALARM;
        break;
    case PAGE_SETTINGS:
        app_cur_page = PAGE_SETTINGSUNIT;
        if (app_settings_index>=SETTINGS_SET_NUM){
            app_cur_page = PAGE_MAIN;
            return;
        }
        app_settings_cur_desc = &settings_config_list[app_settings_index];
        app_settings_cur_value = 0;
        break;
    case PAGE_SETTINGSUNIT:
        assert(app_settings_cur_desc);
        if (settings_field_is_bool(app_settings_cur_desc)){
            return;
        } else if (app_settingsunit_button==0){
            app_settingsunit_button = 1;
        } else if (app_settingsunit_button==1){
            app_settingsunit_button = 0;
        } else {
            ESP_LOGE(TAG, "invalid settingsunit button: %hhu", app_settingsunit_button);
            app_settingsunit_button = 0;
        }
        APP_UPDATE_UI();
        break;
    case PAGE_TRANSMITTING:
        break;
    default:
        ESP_LOGE(TAG, "invalid page: %hhu", app_cur_page);
        break;
    }
}

static void app_btn_lpress_start(){
    ESP_LOGI(TAG, "btn: long");
    switch (app_cur_page){
    case PAGE_MAIN:
        app_cur_page = PAGE_SETTINGS;
        app_settings_index = 0;
        APP_UPDATE_UI();
        break;
    case PAGE_SETTINGS:
        app_cur_page = PAGE_MAIN;
        app_settings_index = 0;
        APP_UPDATE_UI();
        break;
    case PAGE_SETTINGSUNIT:
        app_cur_page = PAGE_SETTINGS;
        if (!settings_human_rw(app_settings_index, true, &app_settings_cur_value)){
            ESP_LOGE(TAG, "invalid settings index: %hhu", app_settings_cur_value);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        }
        app_settings_cur_desc = NULL;
        app_settings_cur_value = 0;
        APP_UPDATE_UI();
        break;
    case PAGE_TRANSMITTING:
        break;
    default:
        ESP_LOGE(TAG, "invalid page: %hhu", app_cur_page);
        break;
    }
}

static void app_btn_lpress_stop(){
    ESP_LOGI(TAG, "btn: released");
}

static void app_task_ble(void*){
    for (;;){
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        app_task_ble_working = true;

        blelib_adv_start(
            &app_manfacturer_data,
            0
        );

        misc_delay_ms(CONFIG_APP_CLIENT_ADV_DURATION);
        ESP_LOGI(TAG, "advertising timeout");

        blelib_adv_stop();

        app_task_ble_working = false;
    }
}

static void app_transmit_alarm(bool is_violance){
    if (app_task_ble_working){
        ESP_LOGE(TAG, "repeat transmit alarm");
        return;
    }
    app_manfacturer_data.type = is_violance
        ? ADVTYPE_CLIENT_VIOLANCE
        : ADVTYPE_CLIENT_ALARM;
    app_adv_id = esp_random();
    app_manfacturer_data.data.adv_id = app_adv_id;
    app_task_ble_working = true;
    xTaskNotify(app_htask_ble, 0, eNoAction);
    ui_clear_resp_list();
    app_cur_page = PAGE_TRANSMITTING;
    APP_UPDATE_UI();
}

static void app_init(){
    println(FIRMWARE_TYPE_STRING "_" FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION);

    misc_gpio_init(PIN_OUTPUT, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    misc_gpio_init(PIN_LED, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    misc_gpio_init(PIN_CMDLINE, GPIO_MODE_INPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);

    misc_vbat_init();
    misc_init_nvs();
    misc_vibration_init();

    blelib_init();
    blelib_adv_init();

    settings_init();
    settings_load();

    if (gpio_get_level(PIN_CMDLINE)==0){
        repl_init();
        settings_addcmds();
        repl_begin();
    }

    int ret = xTaskCreate(
        app_task_ble,
        "app_task_ble",
        APP_CLIENT_BLETASK_STACKSIZE,
        NULL,
        2,
        &app_htask_ble
    );

    if (ret){
        ESP_LOGE(TAG, "create task app_task_ble failed: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    
    ui_init_buttons(
        app_btn_single_click,
        app_btn_double_click, 
        app_btn_lpress_start, 
        app_btn_lpress_stop
    );
    ui_init();
}

static void app_update_ui(){
    ESP_LOGI(TAG, "update ui: %hhd", app_cur_page);
    switch (app_cur_page){
    case PAGE_MAIN:
        ui_showpage_main();
        break;
    case PAGE_SETTINGS:
        ui_showpage_settings(app_settings_index);
        break;
    case PAGE_SETTINGSUNIT:
        ui_showpage_settingsunit(app_settingsunit_button, app_settings_index);
        break;
    case PAGE_TRANSMITTING:
        ui_showpage_transmitting();
        break;
    default:
        ESP_LOGE(TAG, "invalid page: %hhu", app_cur_page);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        break;
    }
}

void app_main(){
    app_init();
    ESP_LOGI(TAG, "end init");

    ui_showpage_launch();
    ui_showpage_main();

    uint32_t advertising_countdown = 0;

    for (;;){
        misc_delay_ms(100);
        if (app_ui_update_countdown==0){
            app_ui_update_countdown = APP_UI_UPDATE_COUNTDOWN_MAX;
            app_update_ui();
        } else {
            app_ui_update_countdown--;
        }

        switch (app_op){
        case DO_NONE:
            break;
        case DO_TRANSMIT_ALARM:
            app_op = DO_NONE;
            ESP_LOGI(TAG, "transmit alarm");
            break;
        case DO_TRANSMIT_VIOLANCE_ALARM:
            app_op = DO_NONE;
            ESP_LOGI(TAG, "transmit violance alarm");
            break;
        default:
            ESP_LOGE(TAG, "invalid operator: %hhu", app_op);
            break;
        }
    }
}
#endif