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
        double -> transmit loud alarm
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

enum app_uipage {
    PAGE_HOME,
    PAGE_SETTINGS,
    PAGE_SETTINGSUNIT,
    PAGE_TRANSMITTING,
};

enum app_tick_stat {
    STAT_IDLE,

    // page main

    STAT_HOME_START_TRANSMIT_NORMAL_ALARM, // -> STAT_TRANSMITTING_ALARM
    STAT_HOME_START_TRANSMIT_LOUD_ALARM, // -> STAT_TRANSMITTING_ALARM
    STAT_HOME_SWITCH_TO_SETTINGS,

    STAT_TRANSMITTING_ALARM,
    STAT_SCANNING_RESPONSE,

    STAT_START_TRANSMIT_RESPONSE,
    STAT_TRANSMITTING_RESPONSE,

    STAT_STARTV_SERVER_ALARM,
    STAT_STARTV_CLIENT_ALARM,
    STAT_STARTV_CLIENT_LOUD,
    STAT_VIBRATING,

    STAT_START_RESPONSE,
    STAT_SENDING_RESPONSE,

    // page settings

    STAT_SETTINGS_SWITCH_NEXT_ITEM,
    STAT_SETTINGS_ENTRY_ITEM,
    STAT_SETTINGS_RETURN,

    // page settingsunit

    STAT_SETTINGSUNIT_CLICK_BUTTON,
    STAT_SETTINGSUNIT_SWITCH_NEXT_BUTTON,
    STAT_SETTINGSUNIT_RETURN,
};

#define APP_UI_UPDATE_DURATION 30000

// 按钮事件
static enum ui_btn_event app_btn_event = 0;
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

/// @todo
static SemaphoreHandle_t app_lock = 0;

static struct blelib_adv_manfacturer_data app_adv_manfacturer_data = {
    .company_id = CONFIG_APP_BLE_COMPANY_ID,
    .type = 0,
    .data.adv_id = 0, // 为 0 时为无响应型广告
    .protocol_ver = CONFIG_APP_BLE_PROTOCOL_VER
};

#define APP_SET_ADV_TYPE(__type) do {app_adv_manfacturer_data.type = __type; } while (0)
#define APP_START_ADV() do {blelib_adv_start(&app_adv_manfacturer_data, 0); } while (0)

#define MS_TO_TICKS(__ms) ((uint32_t)(__ms/APP_MAINLOOP_DELAY))

#define COUNTDOWN_TRANSMIT_ALARM (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_ALARM_DURATION))
#define COUNTDOWN_SCAN_RESP (MS_TO_TICKS(CONFIG_APP_CLIENT_SCAN_RESP_DURATION))
#define COUNTDOWN_TRANSMIT_RESP (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_RESP_DURATION))
#define COUNTDOWN_ALARM_ITVL (MS_TO_TICKS(CONFIG_APP_CLIENT_TRANSMIT_ALARM_INTERVAL))

#define COUNTDOWN_IDLE_UPDATE_UI (MS_TO_TICKS(CONFIG_APP_CLIENT_IDLE_UPDATE_UI_INTERVAL))

#if CONFIG_APP_CLIENT_RX_SERVERALARM || CONFIG_APP_CLIENT_RX_CLIENTALARM

/// @brief 检查 blelib_adv_manfacturer_data 有效性
static bool app_is_valid_mfdata(const struct blelib_adv_manfacturer_data *mfdata){
    assert(mfdata);

    if (
        mfdata->company_id != CONFIG_APP_BLE_COMPANY_ID 
        || mfdata->protocol_ver != CONFIG_APP_BLE_PROTOCOL_VER
        || (mfdata->type & 0x80 && mfdata->encoded_vbat)
        || (
            mfdata->type != ADVTYPE_SERVER_ALARM
            && mfdata->type != ADVTYPE_CLIENT_ALARM
            && mfdata->type != ADVTYPE_CLIENT_LOUD
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
    
    if (
        app_current_page != PAGE_HOME
        || (
            app_mainloop_stat != STAT_IDLE
            && !scanning_resp
        )
    ){
        return;
    }

    if (d->data_status != BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE){
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
        return;
    }

    switch (mfdata.type){ /// @todo
    case ADVTYPE_SERVER_ALARM:
        if (scanning_resp)
            break;
        ESP_LOGI(TAG ,"found server alarm");
        app_mainloop_stat = STAT_STARTV_SERVER_ALARM;
        break;
    case ADVTYPE_CLIENT_ALARM:
        if (scanning_resp)
            break;
        ESP_LOGI(TAG, "found client alarm");
        app_mainloop_stat = STAT_STARTV_CLIENT_ALARM;
        break;
    case ADVTYPE_CLIENT_LOUD:
        if (scanning_resp)
            break;
        ESP_LOGI(TAG, "found client loud alarm");
        app_mainloop_stat = STAT_STARTV_CLIENT_LOUD;
        break;
    case ADVTYPE_CLIENT_RESPONSE:
        if (!scanning_resp)
            break;
        ESP_LOGI(TAG, "found client response");
        app_mainloop_stat = STAT_START_RESPONSE;
        break;
    default:
        assert(0);
    }
}
#endif // CONFIG_APP_CLIENT_RX_SERVERALARM || CONFIG_APP_CLIENT_RX_CLIENTALARM

/// @brief 生成并设置广告 ID
/// 广告 ID 值域：(0x0, 0xffffffff]
static void app_generate_and_set_adv_id(){
#if CONFIG_APP_CLIENT_TX_CLIENTRESP
    uint32_t n = esp_random();
    if (!n){
        n = 1;
    }
    ESP_LOGI(TAG, "adv_id = %X", n);
    app_adv_manfacturer_data.data.adv_id = n;
#else
    app_adv_manfacturer_data.data.adv_id = 0;
#endif
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
        ESP_LOGI(TAG, "btn: single click");
        break;
    case UI_BTN_DOUBLE_CLICK:
        ESP_LOGI(TAG, "btn: double click");
        break;
    case UI_BTN_LONGPRESS_START:
        ESP_LOGI(TAG, "btn: longpress start");
        break;
    case UI_BTN_LONGPRESS_END:
        ESP_LOGI(TAG, "btn: longpress end");
        break;
    case UI_BTN_NOEVENT: // 不可能的值
        [[fallthrough]];
    default:
        ESP_LOGI(TAG, "btn: invalid event: %X", (uint32_t)u32event);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    app_btn_event = (enum ui_btn_event)u32event;
}

/// @brief 处理按钮事件，并转换为状态机状态
static void app_main_handle_button_event(){
    ESP_LOGI(TAG, "handle button event: p=%d e=%d", app_current_page, app_btn_event);
    switch (app_current_page){
    case PAGE_HOME:
        switch (app_btn_event){
        case UI_BTN_SINGLE_CLICK:
            ESP_LOGI(TAG, "transmit normal alarm");
            if (app_tick_countdown){
                break;
            }
            app_mainloop_stat = STAT_HOME_START_TRANSMIT_NORMAL_ALARM;
            break;
        case UI_BTN_DOUBLE_CLICK:
            ESP_LOGI(TAG, "transmit loud alarm");
            if (app_tick_countdown){
                break;
            }
            app_mainloop_stat = STAT_HOME_START_TRANSMIT_LOUD_ALARM;
            break;
        case UI_BTN_LONGPRESS_START:
            ESP_LOGI(TAG, "switch to settings");
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
            ESP_LOGI(TAG, "switch next item");
            app_mainloop_stat = STAT_SETTINGS_SWITCH_NEXT_ITEM;
            break;
        case UI_BTN_DOUBLE_CLICK:
            ESP_LOGI(TAG, "entry item");
            app_mainloop_stat = STAT_SETTINGS_ENTRY_ITEM;
            break;
        case UI_BTN_LONGPRESS_START:
            ESP_LOGI(TAG, "return to page home");
            app_mainloop_stat = STAT_SETTINGS_RETURN;
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
            ESP_LOGI(TAG, "click button");
            app_mainloop_stat = STAT_SETTINGSUNIT_CLICK_BUTTON;
            break;
        case UI_BTN_DOUBLE_CLICK:
            ESP_LOGI(TAG, "switch next button");
            app_mainloop_stat = STAT_SETTINGSUNIT_SWITCH_NEXT_BUTTON;
            break;
        case UI_BTN_LONGPRESS_START:
            ESP_LOGI(TAG, "return to page settings");
            app_mainloop_stat = STAT_SETTINGSUNIT_RETURN;
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
    ESP_LOGI(TAG, "update ui. page=%d", app_current_page);
    app_idle_update_ui_countdown = COUNTDOWN_IDLE_UPDATE_UI;

    switch (app_current_page){
    case PAGE_HOME:
        ui_showpage_home();
        break;
    case PAGE_SETTINGS:
        ui_showpage_settings(app_ui_settings_index);
        break;
    case PAGE_SETTINGSUNIT:
        ui_showpage_settingsunit(
            app_ui_settingsunit_button_index, 
            app_ui_settings_index
        );
        break;
    case PAGE_TRANSMITTING:
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
        break;
    case PAGE_SETTINGSUNIT:
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

/// @brief 主循环节拍状态机
static void app_main_tick(){
    if (app_idle_update_ui_countdown){
        app_idle_update_ui_countdown--;
    }
    if (app_tick_countdown){
        app_tick_countdown--;
    }

    switch (app_mainloop_stat) {
    case STAT_IDLE:
        if (app_idle_update_ui_countdown==0){
            app_update_ui();
        }
        if (app_tick_countdown){
            // 发送冷却期
            break;
        }
        if (app_btn_event != UI_BTN_NOEVENT){
            app_main_handle_button_event();
        }
        break;
    case STAT_HOME_START_TRANSMIT_NORMAL_ALARM:
        blelib_scan_stop();
        APP_SET_ADV_TYPE(ADVTYPE_CLIENT_ALARM);
        app_generate_and_set_adv_id();
        APP_START_ADV();
        app_switch_page(PAGE_TRANSMITTING);
        app_tick_countdown = COUNTDOWN_TRANSMIT_ALARM;
        app_mainloop_stat = STAT_TRANSMITTING_ALARM;
        break;
    case STAT_HOME_START_TRANSMIT_LOUD_ALARM:
        blelib_scan_stop();
        APP_SET_ADV_TYPE(ADVTYPE_CLIENT_LOUD);
        app_generate_and_set_adv_id();
        APP_START_ADV();
        app_switch_page(PAGE_TRANSMITTING);
        app_tick_countdown = COUNTDOWN_TRANSMIT_ALARM;
        app_mainloop_stat = STAT_TRANSMITTING_ALARM;
        break;
    case STAT_HOME_SWITCH_TO_SETTINGS:
        app_switch_page(PAGE_SETTINGS);
        app_mainloop_stat = STAT_IDLE;
        break;
    
    case STAT_TRANSMITTING_ALARM:
        if (app_tick_countdown){
            break;
        }
        blelib_adv_stop();
        blelib_scan_start(0);
        app_mainloop_stat = STAT_SCANNING_RESPONSE;
        app_tick_countdown = COUNTDOWN_SCAN_RESP;
        break;
    case STAT_SCANNING_RESPONSE:
        if (IS_ENABLED(CONFIG_APP_CLIENT_RX_CLIENTRESP) && app_tick_countdown){
            break;
        }
        app_mainloop_stat = STAT_IDLE;
        app_tick_countdown = COUNTDOWN_ALARM_ITVL;
        app_switch_page(PAGE_HOME);
        break;
        
    case STAT_START_TRANSMIT_RESPONSE:
        app_tick_countdown = COUNTDOWN_TRANSMIT_RESP;
        app_mainloop_stat = STAT_TRANSMITTING_RESPONSE;
        break;
    case STAT_TRANSMITTING_RESPONSE:
        if (IS_ENABLED(CONFIG_APP_CLIENT_TX_CLIENTRESP) && app_tick_countdown){
            break;
        }
        app_tick_countdown = 0;
        app_mainloop_stat = STAT_IDLE;
        break;

    case STAT_SETTINGS_SWITCH_NEXT_ITEM:
        break;
    case STAT_SETTINGS_ENTRY_ITEM:
        break;
    case STAT_SETTINGS_RETURN:
        break;

    case STAT_SETTINGSUNIT_CLICK_BUTTON:
        break;
    case STAT_SETTINGSUNIT_SWITCH_NEXT_BUTTON:
        break;
    case STAT_SETTINGSUNIT_RETURN:
        break;

    default:
        ESP_LOGE(TAG, "invalid mainloop stat: %d", app_mainloop_stat);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
}

/// @brief 初始化
static void app_init(){
    ESP_LOGI(TAG, FIRMWARE_TYPE_STRING "_" FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION);

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

    blelib_scan_set_callback(app_scan_callback);
    
    ui_init();
}

void app_main(){
    app_init();
    ESP_LOGI(TAG, "end of init");

    // 开机画面
    ui_showpage_launch();
    ui_showpage_home();

#if APP_MAINLOOP_DELAY
    TickType_t tick_count = xTaskGetTickCount();
#endif // APP_MAINLOOP_DELAY

    for(;;){
#if APP_MAINLOOP_DELAY
        vTaskDelayUntil(&tick_count, pdMS_TO_TICKS(APP_MAINLOOP_DELAY));
#endif // APP_MAINLOOP_DELAY
        app_main_tick();
    }
}

#endif // CONFIG_APP_CLIENT