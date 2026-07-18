#include "config.h"
#if CONFIG_APP_SERVER

#include "misc.h"
#include "bluetooth.h"
#include "public.h"

#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "Main";

#if CONFIG_APP_SERVER_RTC_DS3231

#include "ds3231.h"

static i2c_dev_t rtc_h = {0};

static void init_rtc(){
    ESP_ERROR_CHECK(ds3231_init_desc(&rtc_h, I2C_NUM_0, PIN_IIC_SDA, PIN_IIC_SCL));
}

static bool rtc_is_lost_time(){
    uint8_t flags = 0;
    ESP_ERROR_CHECK(i2c_dev_take_mutex(&rtc_h));
    ESP_ERROR_CHECK(i2c_dev_read_reg(&rtc_h, 0x0f, &flags, 1));
    ESP_ERROR_CHECK(i2c_dev_give_mutex(&rtc_h));
    return (flags&0x10000000);
}

static void rtc_clear_osf(){
    uint8_t flags = 0;
    ESP_ERROR_CHECK(i2c_dev_take_mutex(&rtc_h));
    ESP_ERROR_CHECK(i2c_dev_read_reg(&rtc_h, 0x0f, &flags, 1));
    flags &= ~0x10000000;
    ESP_ERROR_CHECK(i2c_dev_write_reg(&rtc_h, 0x0f, &flags, 1));
    ESP_ERROR_CHECK(i2c_dev_give_mutex(&rtc_h));
}

#endif

void app_init(){
    println(FIRMWARE_TYPE_STRING "_" FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION);

    init_gpio(PIN_FUNCT, GPIO_MODE_INPUT, GPIO_PULLUP_ONLY, GPIO_INTR_DISABLE);
    init_gpio(PIN_OUTPUT, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    init_gpio(PIN_LED, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    init_gpio(PIN_RUNCLI, GPIO_MODE_INPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    
    init_vbat_adc();
    init_peri_uart();

    init_nvs();

    led(1);
    delay_ms(2000);

    #if CONFIG_APP_SERVER_RTC_DS3231
    ESP_ERROR_CHECK(i2cdev_init());
    init_rtc();
    if (rtc_is_lost_time()){
        ESP_LOGW(TAG, "RTC lost time");
    }
    #endif

    init_bluetooth();
    init_advertising();

    delay_ms(2000);
    led(0);
}

void app_main(){
    app_init();
    ESP_LOGI(TAG, "end init");
    struct AdvManfacturerData data = {
        .company_id = CONFIG_APP_GENERAL_BLE_COMPANY_ID,
        .type = ADVTYPE_SERVER_ALARM,
        .encoded_vbat = 0,
        .data.adv_id = 0x55555555,
        .protocol_ver = CONFIG_APP_GENERAL_BLE_PROTOCOL_VER,
    };
    println("start advertising");
    start_advertising(&data, 0);
    led(1);
    println("app_main returned");
}

#endif /* CONFIG_APP_SERVER */