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

#define APP_UI_UPDATE_DURATION 30000

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
static bool app_scan_resp_dev = false;
static TaskHandle_t app_htask_main = NULL;

#define APP_UPDATE() do {\
    app_ui_update_countdown = 0;\
    xTaskNotify(app_htask_main, 0, eNoAction);\
} while(0)

static void app_btn_single_click(){
    ESP_LOGI(TAG, "btn: single");
    switch (app_cur_page){
    case PAGE_MAIN:
        app_op = DO_TRANSMIT_ALARM;
        APP_UPDATE();
        break;
    case PAGE_SETTINGS:
        if (app_settings_index++==SETTINGS_SET_NUM){ // 允许索引等于 SETTINGS_SET_NUM
            app_settings_index = 0;
        }
        APP_UPDATE();
        break;
    case PAGE_SETTINGSUNIT:
        assert(app_settings_cur_desc);
        if (settings_field_is_bool(app_settings_cur_desc)){
            app_settings_cur_value = !app_settings_cur_value;
            APP_UPDATE();
            break;
        } else if (
            app_settings_index==0
            &&app_settings_cur_value<app_settings_cur_desc->max_value
        ){
            // add
            app_settings_cur_value++;
            APP_UPDATE();
            break;
        } else if (
            app_settings_index==1
            &&app_settings_cur_value>app_settings_cur_desc->min_value
        ){
            // sub
            app_settings_cur_value--;
            APP_UPDATE();
            break;
        }
        ESP_LOGE(TAG, 
            "unknown error. val=%hhu index=%hhu btn=%hhu", 
            app_settings_cur_value, 
            app_settings_index, 
            app_settingsunit_button
        );
        ESP_ERROR_CHECK(ESP_ERR_INVALID_SIZE);
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
        APP_UPDATE();
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
        APP_UPDATE();
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
        APP_UPDATE();
        break;
    case PAGE_SETTINGS:
        app_cur_page = PAGE_MAIN;
        app_settings_index = 0;
        APP_UPDATE();
        break;
    case PAGE_SETTINGSUNIT:
        app_cur_page = PAGE_SETTINGS;
        if (!settings_human_rw(app_settings_index, true, &app_settings_cur_value)){
            ESP_LOGE(TAG, "invalid settings index: %hhu", app_settings_cur_value);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        }
        app_settings_cur_desc = NULL;
        app_settings_cur_value = 0;
        APP_UPDATE();
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

#if CONFIG_APP_CLIENT_TX_CLIENTALARM
static void app_task_ble(void*){
    [[maybe_unused]] uint32_t resp_adv_id = 0;
    for (;;){
        xTaskNotifyWait(0, 0, &resp_adv_id, portMAX_DELAY);
        app_task_ble_working = true;
        app_cur_page = PAGE_TRANSMITTING;
        APP_UPDATE();

        blelib_scan_stop();
        misc_delay_ms(100);
        blelib_adv_start(&app_manfacturer_data, 0);

        misc_delay_ms(CONFIG_APP_CLIENT_ADV_DURATION);
        ESP_LOGI(TAG, "advertising timeout");

        blelib_adv_stop();
        misc_delay_ms(100);
        blelib_scan_start(0);
        
#if CONFIG_APP_CLIENT_RX_CLIENTRESP
        ESP_LOGI(TAG, "start recv resp");
        app_scan_resp_dev = true;
        misc_delay_ms(CONFIG_APP_CLIENT_RESP_SCAN_DURATION);
        app_scan_resp_dev = false;
        ESP_LOGI(TAG, "end recv resp");

#endif // CONFIG_APP_CLIENT_RX_CLIENTRESP
        app_adv_id = 0;
        app_task_ble_working = false;
        app_cur_page = PAGE_MAIN;
        APP_UPDATE();
    }
}

static void app_transmit_alarm(bool is_violance){
    while (app_task_ble_working) {
        misc_delay_ms(500);
    }
    app_task_ble_working = true;
    app_manfacturer_data.type = is_violance
        ? ADVTYPE_CLIENT_VIOLANCE
        : ADVTYPE_CLIENT_ALARM;
    app_adv_id = esp_random();
    if (!app_adv_id){
        app_adv_id += 1;
    }
    app_manfacturer_data.data.adv_id = app_adv_id;
    xTaskNotify(app_htask_ble, 0, eNoAction);
    ui_clear_resp_list();
    app_cur_page = PAGE_TRANSMITTING;
    APP_UPDATE();
}
#if CONFIG_APP_CLIENT_TX_CLIENTALARM
static void app_transmit_resp(uint32_t adv_id){
    while (app_task_ble_working) {
        misc_delay_ms(500);
    }
    app_task_ble_working = true;
    xTaskNotify(app_htask_ble, adv_id, eNoAction);
}
#endif
#else
#   define app_transmit_alarm(__bIsViolance) do {} while(0)
#endif // CONFIG_APP_CLIENT_TX_CLIENTALARM

#if CONFIG_APP_CLIENT_RX_SERVERALARM

static bool app_is_valid_mfgdata(const struct blelib_adv_manfacturer_data *data){
    if (
        data->protocol_ver != CONFIG_APP_BLE_PROTOCOL_VER
        || data->company_id != CONFIG_APP_BLE_COMPANY_ID
    ){
        return false;
    }
    switch (data->type){
    case ADVTYPE_CLIENT_ALARM:
        [[fallthrough]];
    case ADVTYPE_CLIENT_VIOLANCE:
        [[fallthrough]];
    case ADVTYPE_CLIENT_RESPONSE:
        if (
            data->encoded_vbat 
            || data->data.adv_id == 0
            || app_scan_resp_dev!=(data->type==ADVTYPE_CLIENT_RESPONSE)
            || (data->type==ADVTYPE_CLIENT_RESPONSE&&data->data.adv_id!=app_adv_id)
        ){
            return false;
        }
        break;
    case ADVTYPE_SERVER_ALARM:
        if (
            !data->encoded_vbat // 电池电压不可能低到 2.2V
            || !(0<=data->data.day_sec && data->data.day_sec < 86400)
        ){
            return false;
        }
        break;    
    default:
        return false;
    }
    return true;
}

static void app_scan_callback(struct ble_gap_ext_disc_desc *desc){
    if (desc->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE
        || desc->tx_power != 127
        || desc->prim_phy != BLE_HCI_LE_PHY_CODED
        || desc->sec_phy != BLE_HCI_LE_PHY_CODED
        || desc->length_data > 32
    ) return;
    const uint8_t *cur_p = desc->data;
    struct blelib_payload_field field;
    uint8_t found_fields = 0;
    uint8_t type;
    uint32_t adv_id;

    while (
        blelib_iter_payload_fields(cur_p, 
            desc->length_data,
            &cur_p, 
            &field
        )
    ){
        if (
            field.type == BLE_HS_ADV_TYPE_FLAGS 
            && *field.data == BLELIB_ADV_FLAGS 
            && !(found_fields & 0x1)
        ){
            found_fields ^= 0x1;
            continue;
        } else if (
            field.type == BLE_HS_ADV_TYPE_COMP_NAME
            && !(found_fields & 0x2)
        ) {
            found_fields ^= 0x2;
            continue;
        } else if (
            field.type == BLE_HS_ADV_TYPE_MFG_DATA 
            && !(found_fields & 0x4)
            && field.len == sizeof(struct blelib_adv_manfacturer_data)
            && app_is_valid_mfgdata((const struct blelib_adv_manfacturer_data*)field.data)
        ){
            found_fields ^= 0x4;
            type = ((const struct blelib_adv_manfacturer_data*)field.data)->type;
            adv_id = ((const struct blelib_adv_manfacturer_data*)field.data)->data.adv_id;
            continue;
        }
        return;
    }

    if (cur_p+1 != desc->data+desc->length_data){
        return;
    }

    uint16_t short_mac = ui_get_short_mac(desc->addr.val);

    if (app_scan_resp_dev){
        ESP_LOGI(TAG, "found resp dev: %04hX", short_mac);
        struct ui_respdev dev = {
            .short_mac = short_mac,
            .rssi = desc->rssi
        };
        ui_add_resp_dev(dev);
    } else {
        ESP_LOGI(TAG, "found alarm dev: %04hX", short_mac);
        struct ui_alarmdev dev = {
            .short_mac = short_mac,
            .rssi = desc->rssi,
            .alarm_type = type,
            .time.recv_time_s = get_seconds(),
            .data.alarm_id = adv_id
        };
        ui_add_alarmdev(&dev);
    }
}
#endif // CONFIG_APP_CLIENT_RX_SERVERALARM

static void app_init(){
    println(FIRMWARE_TYPE_STRING "_" FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION);

    app_htask_main = xTaskGetCurrentTaskHandle();

    misc_gpio_init(
        PIN_OUTPUT, 
        GPIO_MODE_OUTPUT, 
        GPIO_FLOATING, 
        GPIO_INTR_DISABLE
    );
    misc_gpio_init(
        PIN_LED, 
        GPIO_MODE_OUTPUT, 
        GPIO_FLOATING, 
        GPIO_INTR_DISABLE
    );
    misc_gpio_init(
        PIN_CMDLINE, 
        GPIO_MODE_INPUT, 
        GPIO_FLOATING, 
        GPIO_INTR_DISABLE
    );

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

#if CONFIG_APP_CLIENT_TX_CLIENTALARM
    if (
        !xTaskCreate(
            app_task_ble,
            "app_task_ble",
            APP_CLIENT_BLETASK_STACKSIZE,
            NULL,
            2,
            &app_htask_ble
        )
    ){
        ESP_LOGE(TAG, "create task app_task_ble failed");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
#endif // CONFIG_APP_CLIENT_TX_CLIENTALARM

    blelib_scan_set_callback(app_scan_callback);
    
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

    for (;;){
        bool update_op = xTaskNotifyWait(
            0,
            0,
            NULL,
            pdMS_TO_TICKS(APP_UI_UPDATE_DURATION)
        );

        app_update_ui();

        if (!update_op){
            goto no_update;
        }

        bool is_violance = false;

        switch (app_op){
        case DO_NONE:
            break;
        case DO_TRANSMIT_ALARM:
            [[fallthrough]];
        case DO_TRANSMIT_VIOLANCE_ALARM:
            is_violance = (app_op == DO_TRANSMIT_VIOLANCE_ALARM);
            app_op = DO_NONE;
            ESP_LOGI(TAG, "transmit alarm. is_violance=%hhu", is_violance);
            app_transmit_alarm(is_violance);
            break;
        default:
            ESP_LOGE(TAG, "invalid operator: %hhu", app_op);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
            break;
        }

        no_update:

#if CONFIG_APP_CLIENT_TX_CLIENTALARM
        if (app_task_ble_working){
            ESP_LOGI(TAG, "waiting adv complete");
        }
        while (app_task_ble_working) misc_delay_ms(500);
#endif // CONFIG_APP_CLIENT_TX_CLIENTALARM

        misc_delay_ms(100);
    }
}
#endif // CONFIG_APP_CLIENT