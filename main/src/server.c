#include "config.h"
#if CONFIG_APP_SERVER

#include "misc.h"
#include "timelib.h"
#include "blelib.h"
#include "repl.h"
#include "raderctl.h"
#include "settings.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

static const char* TAG = "main";


void app_init(){
    ESP_LOGI(TAG, FIRMWARE_TYPE_STRING "_" FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION);

    misc_gpio_init(PIN_FUNCT, GPIO_MODE_INPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);    misc_gpio_init(PIN_OUTPUT, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);

    misc_gpio_init(PIN_LED, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    led(0);

    misc_gpio_init(PIN_CMDLINE, GPIO_MODE_INPUT, GPIO_PULLUP_ONLY, GPIO_INTR_DISABLE);

    misc_vbat_init();

    timelib_init();

    settings_init();
    settings_load();
    
    if (gpio_get_level(PIN_CMDLINE)==0){
        repl_init();
        raderctl_addcmds();
        settings_addcmds();
        repl_begin();
    }
}

/// @brief 等待雷达就绪
static void app_wait_rader_ready(){
    esp_sleep_enable_timer_wakeup(1000*CONFIG_APP_SERVER_RADER_PWRON_DURATION); // 1s
    gpio_hold_en(PIN_OUTPUT);
    esp_light_sleep_start();
    esp_sleep_enable_timer_wakeup(1000000);
    while (gpio_get_level(PIN_OUTPUT)) {
        esp_light_sleep_start();
    }
    gpio_hold_dis(PIN_OUTPUT);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

/// @brief CPU 是被雷达唤醒的
static bool app_wakeup_by_rader(){
    return esp_reset_reason()==ESP_RST_DEEPSLEEP
        && esp_sleep_get_wakeup_causes()&BIT(ESP_SLEEP_WAKEUP_GPIO);
}

/// @brief 获取雷达上一次的电源状态
/// @return true 表示雷达已经上电；false 表示雷达没有上电
static bool app_rader_last_is_pwron(){
    esp_reset_reason_t rst = esp_reset_reason();
    if (rst!=ESP_RST_DEEPSLEEP){
        return false;
    }
    uint32_t wkup = esp_sleep_get_wakeup_causes();
    if (BIT(wkup)&ESP_SLEEP_WAKEUP_TIMER){
        return gpio_get_level(PIN_OUTPUT);
    } else {
        return BIT(wkup)&ESP_SLEEP_WAKEUP_GPIO;
    }
}

/// @brief 让雷达关机
static void app_rader_pwroff(){
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(PIN_OUTPUT);
    gpio_set_level(PIN_OUTPUT, 0);
}

/// @brief 让雷达开机
static void app_rader_pwron(){
    if (app_rader_last_is_pwron()){
        // 对于已经 hold 的 gpio，需要先初始化，再设置电平，最后解除 hold，就能防止电平变化
        gpio_set_level(PIN_OUTPUT, 1);
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis(PIN_OUTPUT);
    } else {
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis(PIN_OUTPUT);
        gpio_set_level(PIN_OUTPUT, 1);
        app_wait_rader_ready();
    }
}

/// @brief 做好入睡前的准备然后入睡
/// @param duration 睡眠时间（S）
/// @param gpio_wakeupable 是否可由 GPIO 唤醒
[[noreturn]] static void app_begin_sleep(uint32_t duration, bool gpio_wakeupable){
    ESP_LOGI(TAG, "begin sleep. duration=%u gpio_wakeupable=%hhu", duration, gpio_wakeupable);
    esp_sleep_enable_timer_wakeup(duration*1000000);
    if (gpio_wakeupable){
        esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL<<PIN_FUNCT, ESP_GPIO_WAKEUP_GPIO_HIGH);
    }
    gpio_hold_en(PIN_OUTPUT);
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
}

/// @brief 发送警告广告包
static void app_transmit_alarm(float vbat, int now){
    uint8_t encoded_vbat = 0;
    static struct blelib_adv_manfacturer_data mfdata = {
        .company_id = CONFIG_APP_BLE_COMPANY_ID,
        .type = ADVTYPE_SERVER_ALARM,
        .encoded_vbat = 0, // not a compile time value
        .data = {.day_sec = 0},
        CONFIG_APP_BLE_PROTOCOL_VER
    };
    mfdata.encoded_vbat = encoded_vbat;
    mfdata.data.day_sec = now;
    blelib_init();
    blelib_adv_init();
    blelib_adv_start(&mfdata, CONFIG_APP_SERVER_ADV_DURATION);
    misc_delay_ms(CONFIG_APP_SERVER_ADV_DURATION);
    blelib_adv_stop();
    blelib_deinit();
}

/// @brief 检查电池电压 太低就永久深度睡眠
static void app_check_vbat(float vbat){
#if CONFIG_APP_SERVER_AUTOPWOFF_ENABLED
    float limit = CONFIG_APP_SERVER_AUTOPWOFF_LIMIT/1000.0;
    if (vbat<=limit){
        ESP_LOGE(TAG, "low battery voltage: %f", vbat);
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis(PIN_OUTPUT);
        esp_deep_sleep_start();
    }
#endif
}

void app_main(){
    app_init();
    ESP_LOGI(TAG, "end init");
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    
    float vbat = misc_vbat_read();
    ESP_LOGI(TAG, "vbat: %f", vbat);
    app_check_vbat(vbat);

    int now = timelib_get_day_sec();
    ESP_LOGI(TAG, "now: %d", now);


    if (app_wakeup_by_rader()){
        ESP_LOGI(TAG, "wakeup by rader");
        app_transmit_alarm(vbat, now);
    }
    
    const struct timelib_slpitvl *inprog_slpitvl = timelib_find_inprog_slpitvl(now);
    if (inprog_slpitvl){
        ESP_LOGI(TAG, "found inprogress sleep interval");
        timelib_logprint_slpitvl(inprog_slpitvl);
        uint32_t sleep_duration = sec_sub(inprog_slpitvl->end, now);
        app_rader_pwroff();
        app_begin_sleep(sleep_duration, 0);
    }

    const struct timelib_slpitvl *next_slpitvl = timelib_find_next_slpitvl(now);
    if (next_slpitvl){
        ESP_LOGI(TAG, "found next sleep interval");
        timelib_logprint_slpitvl(next_slpitvl);
        uint32_t sleep_duration = sec_sub(next_slpitvl->start, now);
        app_rader_pwron();
        app_begin_sleep(sleep_duration, 1);
    }

    ESP_LOGI(TAG, "no avaliable sleep interval found.");
    uint32_t sleep_duration = sec_sub(60, now); // to 00:01
    app_rader_pwron();
    app_begin_sleep(sleep_duration, 1);
}

#endif /* CONFIG_APP_SERVER */