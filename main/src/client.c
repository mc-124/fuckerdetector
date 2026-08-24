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
#include "freertos/FreeRTOS.h"

static const char *TAG = "main";

/*
    main:
        single -> transmit alarm
        single x2 -> transmit loud alarm
        long -> begin settings
    settings:
        single -> next
        long -> return
    settingsunit:
        single -> click
        long -> next button
*/

enum app_uipage {
    PAGE_HOME,
    PAGE_SETTINGS,
    PAGE_SETTINGSUNIT,
    PAGE_TRANSMITTING,
};

enum app_tick_stat {
    STAT_IDLE,

    // page main

    STAT_HOME_READY_TRANSMIT,
    STAT_HOME_WAIT_SECOND_BUTTON,
    STAT_HOME_SWITCH_TO_SETTINGS,

    STAT_TRANSMITTING_ALARM,
    STAT_SCANNING_RESPONSE,

    STAT_START_VIBRATION,
    STAT_START_RESPONSE,
    STAT_READY_RESPONSE,
    STAT_SENDING_RESPONSE,
    STAT_WAIT_VIBRATION_STOP,

    STAT_FOUND_RESPONSE,

    // page settings

    STAT_SETTINGS_SWITCH_NEXT_ITEM,
    STAT_SETTINGS_ENTRY_ITEM,

    // page settingsunit

    STAT_SETTINGSUNIT_CLICK_BUTTON,
    STAT_SETTINGSUNIT_SWITCH_NEXT_BUTTON,
};

#define APP_UI_UPDATE_DURATION 30000

// 按钮事件
// 会在 `app_handle_button_event` 中被自动重置
static enum ui_btn_event app_btn_event = UI_BTN_NOEVENT;
// 页索引
static enum app_uipage app_current_page = PAGE_HOME;
// 主循环节拍状态机标志
static enum app_tick_stat app_mainloop_stat = STAT_IDLE;
// 主循环倒计时
static uint32_t app_tick_countdown = 0;
// 空闲时自动更新UI倒计时
static uint32_t app_idle_update_ui_countdown = 0;
// 
static uint32_t app_ui_settings_index = 0;

static uint32_t app_ui_settingsunit_button_index = 0;

static const struct settings_config_desc *app_ui_settings_desc = NULL;

static struct ui_alarmdev app_thisdev = {0};

static struct blelib_adv_manfacturer_data app_adv_manfacturer_data = {
    .company_id = CONFIG_APP_BLE_COMPANY_ID,
    .type = 0,
    .data.adv_id = 0, // 为 0 时为无响应型广告
    .protocol_ver = CONFIG_APP_BLE_PROTOCOL_VER
};

#define APP_SET_ADV_TYPE(__type) do {app_adv_manfacturer_data.type = __type; } while (0)
#define APP_START_ADV() do {blelib_adv_start(&app_adv_manfacturer_data, 0); } while (0)

#define MS_TO_TICKS(__ms) ((uint32_t)(__ms/APP_MAINLOOP_DELAY))

#define COUNTDOWN_READY_ALARM (MS_TO_TICKS(500))
#define COUNTDOWN_TRANSMIT_ALARM (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_ALARM_DURATION))
#define COUNTDOWN_SCAN_RESP (MS_TO_TICKS(CONFIG_APP_CLIENT_SCAN_RESP_DURATION))
#define COUNTDOWN_TRANSMIT_RESP (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_RESP_DURATION))
#define COUNTDOWN_ALARM_ITVL (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_ALARM_INTERVAL))
#define COUNTDOWN_START_RESP (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_RESP_DURATION))
#define COUNTDOWN_RESP_WAIT (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_RESP_WAITTIME))

#define COUNTDOWN_IDLE_UPDATE_UI (MS_TO_TICKS(CONFIG_APP_CLIENT_IDLE_UPDATE_UI_INTERVAL))

static TaskHandle_t app_htask_vibrator = NULL;
bool app_vibrator_working = false;

/// @brief 震动控制任务
static void app_taskfunc_vibrator([[maybe_unused]] void*){
    uint32_t is_loud = 0;
    uint32_t vib_dur = 0;
    uint32_t vib_itv = 0;
    uint32_t vib_num = 0;
    uint32_t vib_pwr = 0;

    ESP_LOGD(TAG, "vibratir task running");

    const struct settings_config_desc *const desc_normal_num = &settings_config_list[4];
    const struct settings_config_desc *const desc_normal_dur = &settings_config_list[5];
    const struct settings_config_desc *const desc_normal_itv = &settings_config_list[6];
    const struct settings_config_desc *const desc_normal_pwr = &settings_config_list[7];
    
    const struct settings_config_desc *const desc_loud_num = &settings_config_list[8];
    const struct settings_config_desc *const desc_loud_dur = &settings_config_list[9];
    const struct settings_config_desc *const desc_loud_itv = &settings_config_list[10];
    const struct settings_config_desc *const desc_loud_pwr = &settings_config_list[11];

    for (;;){
        xTaskNotifyWait(0, 0, &is_loud, portMAX_DELAY);
        app_vibrator_working = true;

        if (is_loud){
            ESP_LOGD(TAG, "vibrator: loud");
            vib_num = settings_get_field_display_value(
                desc_loud_num,
                settings.vib_loud_num
            );
            vib_dur = settings_get_field_display_value(
                desc_loud_dur,
                settings.vib_loud_dur
            );
            vib_itv = settings_get_field_display_value(
                desc_loud_itv,
                settings.vib_loud_itv
            );
            vib_pwr = settings_get_field_display_value(
                desc_loud_pwr,
                settings.vib_loud_pwr
            );
        } else {
            ESP_LOGD(TAG, "vibrator: normal");
            vib_num = settings_get_field_display_value(
                desc_normal_num,
                settings.vib_normal_num
            );
            vib_dur = settings_get_field_display_value(
                desc_normal_dur,
                settings.vib_normal_dur
            );
            vib_itv = settings_get_field_display_value(
                desc_normal_itv,
                settings.vib_normal_itv
            );
            vib_pwr = settings_get_field_display_value(
                desc_normal_pwr,
                settings.vib_normal_pwr
            );
        }

        ESP_LOGD(
            TAG,
            "vibrator: d=%u i=%u n=%u p=%u",
            U32 vib_dur,
            U32 vib_itv,
            U32 vib_num,
            U32 vib_pwr
        );

        for (int i=0; i<vib_num; i++){
            misc_vibration_set(vib_pwr);
            misc_delay_ms(vib_dur);
            misc_vibration_set(0);
            misc_delay_ms(vib_itv);
        }

        ESP_LOGD(TAG, "vibrator: stop");
        app_vibrator_working = false;
    }
}

static void app_set_vibration(bool is_loud){
    assert(app_htask_vibrator);
    if (is_loud){
        ESP_LOGD(TAG, "set vibration: loud");
    } else {
        ESP_LOGD(TAG, "set vibration: normal");
    }
    uint32_t u32 = is_loud;
    xTaskNotify(app_htask_vibrator, u32, eSetValueWithOverwrite);
}

#if APP_RX_ALARM

/// @brief 检查 blelib_adv_manfacturer_data 有效性
static bool app_is_valid_mfdata(const struct blelib_adv_manfacturer_data *mfdata){
    assert(mfdata);

    if (
        mfdata->company_id != CONFIG_APP_BLE_COMPANY_ID 
        || mfdata->protocol_ver != CONFIG_APP_BLE_PROTOCOL_VER
        || (mfdata->type & 0x80 && mfdata->encoded_vbat)
        || (
            (
                mfdata->type != ADVTYPE_SERVER_ALARM
                || !IS_ENABLED(CONFIG_APP_CLIENT_RX_SERVERALARM)
                || !settings.enable_recv_server_alarm
            )
            && (
                mfdata->type != ADVTYPE_CLIENT_ALARM
                || !IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTALARM)
                || !settings.enable_recv_client_alarm
            )
            && (
                mfdata->type != ADVTYPE_CLIENT_LOUD
                || !IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTLOUD)
                || !settings.enable_recv_client_loud_alarm
            )
            && (
                !IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTRESP)
                || mfdata->type != ADVTYPE_CLIENT_RESPONSE
            )
        )
    ) return false;

    return true;
}

/// @brief BLE 扫描回调函数
static void app_scan_callback(const struct ble_gap_ext_disc_desc *d){
    bool scanning_resp = (
        IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTRESP)
        && app_mainloop_stat == STAT_SCANNING_RESPONSE
    );

    if (!(
            app_current_page == PAGE_HOME
            && app_mainloop_stat == STAT_IDLE
            && !scanning_resp
        )
        && !(
            app_current_page == PAGE_TRANSMITTING
            && app_mainloop_stat == STAT_SCANNING_RESPONSE
            && scanning_resp
        )
    ){;
        return;
    }

    if (d->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE){
        ESP_LOGD(TAG, "inv: bad data");
        return;
    }

    bool found_flags = false;
    bool found_name = false;
    bool found_mfdata = false;
    
    struct blelib_payload_field field;
    struct blelib_adv_manfacturer_data mfdata;
    const uint8_t *cur_ptr = NULL;

    while (
        blelib_iter_payload_fields(
            d->data,
            d->length_data, 
            &cur_ptr, 
            &field
        )
    ){
        if (field.type == BLE_HS_ADV_TYPE_FLAGS){
            if (found_flags) 
                return;
            if (*field.data != BLELIB_ADV_FLAGS)
                return;
            found_flags = true;
        } else if (field.type == BLE_HS_ADV_TYPE_COMP_NAME){
            if (found_name)
                return;
            found_name = true;
        } else if (field.type == BLE_HS_ADV_TYPE_MFG_DATA){
            if (found_mfdata)
                return;
            memcpy(&mfdata, field.data, sizeof(struct blelib_adv_manfacturer_data));
            if (!app_is_valid_mfdata(&mfdata))
                return;
            found_mfdata = true;
        }
    }
    if (!found_flags || !found_name || !found_mfdata){
        ESP_LOGD(TAG, "inv: loss field");
        return;
    }
    if (cur_ptr != d->data + d->length_data){
        ESP_LOGD(TAG, "inv: bad struct");
        return;
    }

    enum app_tick_stat new_stat = 0;

    switch (mfdata.type){
    case ADVTYPE_SERVER_ALARM:
        [[fallthrough]];
    case ADVTYPE_CLIENT_ALARM:
        [[fallthrough]];
    case ADVTYPE_CLIENT_LOUD:
        if (scanning_resp)
            break;
        ESP_LOGI(TAG, "found alarm: 0x%hhX", mfdata.type);
        new_stat = STAT_START_VIBRATION;
        goto write_mfdata;
    case ADVTYPE_CLIENT_RESPONSE:
        if (!scanning_resp)
            break;
        ESP_LOGI(TAG, "found response");
        new_stat = STAT_FOUND_RESPONSE;
        goto write_mfdata;
    default:
        assert(0);
    }
    return;

write_mfdata:
    ESP_LOGD(TAG, "valid adv");
    
    if (
        app_mainloop_stat != STAT_IDLE
        && app_mainloop_stat != STAT_SCANNING_RESPONSE
    ){
        ESP_LOGW(TAG, "mainloop stat changed");
        return;
    }

    memcpy(
        &app_adv_manfacturer_data,
        &mfdata,
        sizeof(struct blelib_adv_manfacturer_data)
    );

    memcpy(&app_thisdev.short_mac, d->addr.val, 6);
    app_thisdev.alarm_type = mfdata.type;
    if (mfdata.type & 0x80){ // client
        app_thisdev.data.alarm_id = mfdata.data.adv_id;
        app_thisdev.time.recv_time_s = get_seconds();
    } else {
        app_thisdev.data.vbat = blelib_decode_vbat(mfdata.encoded_vbat);
        app_thisdev.time.day_sec = mfdata.data.day_sec;
    }
    app_thisdev.rssi = d->rssi;

    app_mainloop_stat = new_stat;
}
#endif // CONFIG_APP_CLIENT_RX_SERVERALARM || CONFIG_APP_CLIENT_RX_CLIENTALARM

/// @brief 生成并设置广告 ID
/// 广告 ID 值域：(0x0, 0xffffffff]
static void app_generate_and_set_adv_id(){
    if (IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTRESP)){
        uint32_t n = esp_random();
        if (!n){
            n = 1;
        }
        ESP_LOGD(TAG, "adv_id = %X", n);
        app_adv_manfacturer_data.data.adv_id = n;
    } else {
        app_adv_manfacturer_data.data.adv_id = 0;
    }
}

/// @brief 按钮事件回调函数
/// - 在收到按钮事件后会把事件写入到 `app_btn_event`
static void app_btn_callback([[maybe_unused]] void *phbutton, void* u32event){
    if (app_btn_event != UI_BTN_NOEVENT){
        ESP_LOGW(TAG,
            "btn: unhandled event: %X. this event: %X",
            app_btn_event,
            (uint32_t)u32event
        );
        return;
    }
    switch ((enum ui_btn_event)u32event){
    case UI_BTN_SINGLE_CLICK:
        ESP_LOGD(TAG, "btn: single click");
        break;
    case UI_BTN_LONGPRESS_START:
        ESP_LOGD(TAG, "btn: longpress start");
        break;
    case UI_BTN_LONGPRESS_END:
        ESP_LOGD(TAG, "btn: longpress end");
        break;
    case UI_BTN_NOEVENT: // 不可能的值
        [[fallthrough]];
    default:
        ESP_LOGI(TAG, "btn: invalid event: %X", (uint32_t)u32event);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    app_btn_event = (enum ui_btn_event)u32event;
}

/// @brief 处理按钮事件，并转换为状态机状态，最后清空 app_btn_event
static void app_handle_button_event(){
    ESP_LOGD(TAG, "handle button event: p=%d e=%d", app_current_page, app_btn_event);
    switch (app_current_page){
    case PAGE_HOME:
        switch (app_btn_event){
        case UI_BTN_SINGLE_CLICK:
            ESP_LOGD(TAG, "transmit normal alarm");
            if (app_tick_countdown)
                break;
            app_mainloop_stat = STAT_HOME_READY_TRANSMIT;
            break;
        case UI_BTN_LONGPRESS_START:
            ESP_LOGD(TAG, "switch to settings");
            app_mainloop_stat = STAT_HOME_SWITCH_TO_SETTINGS;
            break;
        case UI_BTN_LONGPRESS_END:
            break;
        default:
            ESP_LOGE(TAG, "invalid button event (home): %d", app_btn_event);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        }
        break;
    case PAGE_SETTINGS:
        switch (app_btn_event){
        case UI_BTN_SINGLE_CLICK:
            ESP_LOGD(TAG, "switch next item");
            app_mainloop_stat = STAT_SETTINGS_SWITCH_NEXT_ITEM;
            break;
        case UI_BTN_LONGPRESS_START:
            ESP_LOGD(TAG, "entry item");
            app_mainloop_stat = STAT_SETTINGS_ENTRY_ITEM;
            break;
        case UI_BTN_LONGPRESS_END:
            break;
        default:
            ESP_LOGE(TAG, "invalid button event (settings): %d", app_btn_event);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        }
        break;
    case PAGE_SETTINGSUNIT:
        switch (app_btn_event){
        case UI_BTN_SINGLE_CLICK:
            ESP_LOGD(TAG, "click button");
            app_mainloop_stat = STAT_SETTINGSUNIT_CLICK_BUTTON;
            break;
        case UI_BTN_LONGPRESS_START:
            ESP_LOGD(TAG, "switch next button");
            app_mainloop_stat = STAT_SETTINGSUNIT_SWITCH_NEXT_BUTTON;
            break;
        case UI_BTN_LONGPRESS_END:
            break;
        default:
            ESP_LOGE(TAG, "invalid button event (settingsunit): %d", app_btn_event);
            ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
        }
        break;
    case PAGE_TRANSMITTING:
        break;
    default:
        ESP_LOGE(TAG, "invalid page index: %d", app_current_page);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    app_btn_event = UI_BTN_NOEVENT;
}

/// @brief 更新UI
static void app_update_ui(){
    app_idle_update_ui_countdown = COUNTDOWN_IDLE_UPDATE_UI;

    switch (app_current_page){
    case PAGE_HOME:
        ESP_LOGD(TAG, "update ui: PAGE_HOME");
        ui_showpage_home();
        break;
    case PAGE_SETTINGS:
        ESP_LOGD(TAG, "update ui: PAGE_SETTINGS");
        ui_showpage_settings(app_ui_settings_index);
        break;
    case PAGE_SETTINGSUNIT:
        ESP_LOGD(TAG, "update ui: PAGE_SETTINGSUNIT");
        ui_showpage_settingsunit(
            app_ui_settings_index, 
            app_ui_settingsunit_button_index
        );
        break;
    case PAGE_TRANSMITTING:
        ESP_LOGD(TAG, "update ui: PAGE_TRANSMITTING");
        ui_showpage_transmitting();
        break;
    default:
        ESP_LOGE(TAG, "invalid page: %d", app_current_page);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
}

/// @brief 切换页面
static void app_switch_page(enum app_uipage page){
    ESP_LOGI(TAG, "switch page: old=%d new=%d", app_current_page, page);
    switch (page){
    case PAGE_HOME:
        app_ui_settings_index = 0;
        app_ui_settingsunit_button_index = 0;
        break;
    case PAGE_SETTINGS:
        app_ui_settingsunit_button_index = 0;
        app_ui_settings_desc = NULL;
        break;
    case PAGE_SETTINGSUNIT:
        app_ui_settingsunit_button_index = 0;
        app_ui_settings_desc = &settings_config_list[app_ui_settings_index];
        break;
    case PAGE_TRANSMITTING:
        break;
    default:
        ESP_LOGE(TAG, "invalid page: %d", page);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    app_current_page = page;
    app_update_ui();
}

/// @brief 处理 settingsunit 页面的按钮按下
static void app_settingsunit_click_button(){
    assert(app_ui_settings_desc);

    uint8_t cur_value;

    if (!settings_human_rw(app_ui_settings_index, false, &cur_value)){
        ESP_LOGE(TAG, "invalid settings index: %d", app_ui_settings_index);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }

    if (settings_field_is_bool(app_ui_settings_desc)){
        if (app_ui_settingsunit_button_index == 0)
            cur_value = !cur_value;
        else
            goto button_return;
    } else
        switch (app_ui_settingsunit_button_index){
        case 0: // add
            if (cur_value < app_ui_settings_desc->max_value)
                cur_value++;
            else
                return;
            break;
        case 1: // sub
            if (cur_value > app_ui_settings_desc->min_value)
                cur_value--;
            else
                return;
            break;
        default: // return
            button_return:
            app_mainloop_stat = STAT_IDLE;
            app_switch_page(PAGE_SETTINGS);
            return;
        }

    settings_human_rw(app_ui_settings_index, true, &cur_value);
    app_update_ui();
}

/// @brief 主循环节拍状态机
static void app_main_tick(){
    if (app_idle_update_ui_countdown)
        app_idle_update_ui_countdown--;

    if (app_tick_countdown)
        app_tick_countdown--;

    switch (app_mainloop_stat) {
    case STAT_IDLE:
        if (app_idle_update_ui_countdown==0)
            app_update_ui();

        if (app_tick_countdown)
            // 发送冷却期
            break;

        if (app_btn_event != UI_BTN_NOEVENT)
            app_handle_button_event();
        
        break;

    // page main

    case STAT_HOME_READY_TRANSMIT:
        ESP_LOGD(TAG, "STAT_HOME_READY_TRANSMIT");
        led(1);
        app_mainloop_stat = STAT_HOME_WAIT_SECOND_BUTTON;
        app_tick_countdown = COUNTDOWN_READY_ALARM;
        app_btn_event = UI_BTN_NOEVENT;
        break;
    case STAT_HOME_WAIT_SECOND_BUTTON:
        if (app_btn_event == UI_BTN_SINGLE_CLICK){
            // double click
            ESP_LOGD(TAG, "transmit loud alarm");
            APP_SET_ADV_TYPE(ADVTYPE_CLIENT_LOUD);
            app_tick_countdown = 0;
            goto start_transmit_alarm;
        }
        
        if (app_btn_event != UI_BTN_NOEVENT){
            ESP_LOGW(TAG, "invalid button event: %d", app_btn_event);
            app_btn_event = UI_BTN_NOEVENT;
            break;
        }
        if (app_tick_countdown)
            break;
        
        ESP_LOGD(TAG, "transmit normal alarm");
        APP_SET_ADV_TYPE(ADVTYPE_CLIENT_ALARM);
        
    start_transmit_alarm:
        ui_clear_resp_list();
        app_btn_event = UI_BTN_NOEVENT;
        blelib_scan_stop();
        app_generate_and_set_adv_id();
        APP_START_ADV();
        app_switch_page(PAGE_TRANSMITTING);
        app_tick_countdown = COUNTDOWN_TRANSMIT_ALARM;
        app_mainloop_stat = STAT_TRANSMITTING_ALARM;
        break;

    case STAT_HOME_SWITCH_TO_SETTINGS:
        ESP_LOGD(TAG, "STAT_HOME_SWITCH_TO_SETTINGS");
        app_switch_page(PAGE_SETTINGS);
        blelib_scan_stop();
        app_mainloop_stat = STAT_IDLE;
        break;
    
    case STAT_TRANSMITTING_ALARM:
        if (app_tick_countdown)
            break;
        ESP_LOGD(TAG, "STAT_TRANSMITTING_ALARM");
        blelib_adv_stop();
        if (IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTRESP)){
            app_mainloop_stat = STAT_SCANNING_RESPONSE;
            app_tick_countdown = COUNTDOWN_SCAN_RESP;
        } else {
            goto transmit_alarm_end;
        }
        blelib_scan_start(0);
        break;
    case STAT_SCANNING_RESPONSE:
        if (!IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTRESP))
            goto invalid_state;
        if (app_tick_countdown)
            break;
        ESP_LOGD(TAG, "STAT_SCANNING_RESPONSE");
    transmit_alarm_end:
        app_mainloop_stat = STAT_IDLE;
        app_btn_event = UI_BTN_NOEVENT;
        app_tick_countdown = COUNTDOWN_ALARM_ITVL;
        led(0);
        app_switch_page(PAGE_HOME);
        break;

    case STAT_START_VIBRATION:
        ESP_LOGD(TAG, "STAT_START_VIBRATION");
        do {
            bool is_loud;

            ESP_LOGD(TAG, "adv type: %hhu", app_adv_manfacturer_data.type);

            if (app_adv_manfacturer_data.type == ADVTYPE_SERVER_ALARM){
                is_loud = settings.server_alarm_as_loud;
            } else if (app_adv_manfacturer_data.type == ADVTYPE_CLIENT_ALARM){
                is_loud = false;
            } else if (app_adv_manfacturer_data.type == ADVTYPE_CLIENT_LOUD){
                is_loud = true;
            } else {
                ESP_LOGE(
                    TAG,
                    "invalid type for vibration: %hhu",
                    app_adv_manfacturer_data.type
                );
                ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
                assert(0);
            }
            app_set_vibration(is_loud);
        } while (0);

        led(1);
        
        if (
            IS_ENABLED(CONFIG_APP_CLIENT_TX_CLIENTRESP)
            && app_adv_manfacturer_data.type & 0x80
        ){
            app_mainloop_stat = STAT_START_RESPONSE;
            blelib_scan_stop();
        } else {
            app_mainloop_stat = STAT_WAIT_VIBRATION_STOP;
        }
        ui_add_alarmdev(&app_thisdev);
        app_update_ui();
        break;
    case STAT_START_RESPONSE:
        if (!IS_ENABLED(CONFIG_APP_CLIENT_TX_CLIENTRESP))
            goto invalid_state;
        ESP_LOGD(TAG, "STAT_START_RESPONSE");
        app_tick_countdown = COUNTDOWN_RESP_WAIT;
        app_mainloop_stat = STAT_READY_RESPONSE;
        break;
    case STAT_READY_RESPONSE:
        if (!IS_ENABLED(CONFIG_APP_CLIENT_TX_CLIENTRESP))
            goto invalid_state;
        if (app_tick_countdown)
            break;
        ESP_LOGD(TAG, "STAT_READY_RESPONSE");
        app_tick_countdown = COUNTDOWN_START_RESP;
        app_mainloop_stat = STAT_SENDING_RESPONSE;
        app_adv_manfacturer_data.encoded_vbat = 0;
        app_adv_manfacturer_data.type = ADVTYPE_CLIENT_RESPONSE;
        blelib_adv_start(&app_adv_manfacturer_data, 0);
        break;
    case STAT_SENDING_RESPONSE:
        if (!IS_ENABLED(CONFIG_APP_CLIENT_TX_CLIENTRESP))
            goto invalid_state;
        if (app_tick_countdown)
            break;
        ESP_LOGD(TAG, "STAT_SENDING_RESPONSE");
        blelib_adv_stop();
        app_mainloop_stat = STAT_WAIT_VIBRATION_STOP;
        break;
    case STAT_WAIT_VIBRATION_STOP:
        if (app_vibrator_working)
            break;
        ESP_LOGD(TAG, "STAT_WAIT_VIBRATION_STOP");
        app_btn_event = UI_BTN_NOEVENT;
        app_mainloop_stat = STAT_IDLE;
        led(0);
        if (app_adv_manfacturer_data.type & 0x80)
            blelib_scan_start(0);
        break;
        
    case STAT_FOUND_RESPONSE:
        ESP_LOGD(TAG, "STAT_FOUND_RESPONSE");
        app_mainloop_stat = STAT_SCANNING_RESPONSE;
        ui_add_resp_dev(*((struct ui_respdev*)&app_thisdev));
        app_update_ui();
        break;

    // page settings

    case STAT_SETTINGS_SWITCH_NEXT_ITEM:
        ESP_LOGD(TAG, "STAT_SETTINGS_SWITCH_NEXT_ITEM");
        if (app_ui_settings_index == 12)
            app_ui_settings_index = 0;
        else
            app_ui_settings_index++;
        app_mainloop_stat = STAT_IDLE;
        app_update_ui();
        break;
    case STAT_SETTINGS_ENTRY_ITEM:
        ESP_LOGD(TAG, "STAT_SETTINGS_ENTRY_ITEM");
        if (app_ui_settings_index == 12){
            app_switch_page(PAGE_HOME);
            blelib_scan_start(0);
            settings_store();
        } else {
            app_switch_page(PAGE_SETTINGSUNIT);
        }
        app_mainloop_stat = STAT_IDLE;
        break;

    // page settingsunit

    case STAT_SETTINGSUNIT_CLICK_BUTTON:
        ESP_LOGD(TAG, "STAT_SETTINGSUNIT_CLICK_BUTTON");
        app_settingsunit_click_button();
        app_mainloop_stat = STAT_IDLE;
        break;
    case STAT_SETTINGSUNIT_SWITCH_NEXT_BUTTON:
        ESP_LOGD(TAG, "STAT_SETTINGSUNIT_SWITCH_NEXT_BUTTON");
        if (settings_field_is_bool(app_ui_settings_desc)){
            app_ui_settingsunit_button_index = !app_ui_settingsunit_button_index;
        } else {
            if (app_ui_settingsunit_button_index == 2)
                app_ui_settingsunit_button_index = 0;
            else
                app_ui_settingsunit_button_index++;
        }
        app_mainloop_stat = STAT_IDLE;
        app_update_ui();
        break;

    default:
    invalid_state:
        ESP_LOGE(TAG, "invalid mainloop stat: %d", app_mainloop_stat);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
}

/// @brief 初始化
static void app_init(){
    ESP_LOGI(
        TAG,
        FIRMWARE_TYPE_STRING "_" FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION
    );

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

    UBaseType_t rc = xTaskCreate(
        app_taskfunc_vibrator,
        "vibrator",
        CONFIG_APP_CLIENT_VIBRATIOR_STACK_SIZE,
        NULL,
        1,
        &app_htask_vibrator
    );

    if (rc != pdTRUE){
        ESP_LOGE(TAG, "create task failed (%d): vibrator", rc);
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    blelib_scan_set_callback(app_scan_callback);
    ui_init_buttons(app_btn_callback);
    
    ui_init();
}

void app_main(){
    app_init();
    ESP_LOGD(TAG, "end of init");

    led(1);

    // 开机画面
    ui_showpage_launch();

    led(0);

    ui_showpage_home();

    TickType_t tick_count = APP_MAINLOOP_DELAY
        ? xTaskGetTickCount()
        : 0
    ;

    blelib_scan_start(0);

    for(;;){
        if(APP_MAINLOOP_DELAY)
            vTaskDelayUntil(&tick_count, pdMS_TO_TICKS(APP_MAINLOOP_DELAY));

        app_main_tick();
    }
}

#endif // CONFIG_APP_CLIENT