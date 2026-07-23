#include "config.h"
#if CONFIG_APP_SERVER

#include "misc.h"
#include "settings.h"
#include "blelib.h"
#include "repl.h"
#include "raderctl.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"

static const char* TAG = "app";

#if CONFIG_APP_SERVER_RTC_DS3231

#include "ds3231.h"

static i2c_dev_t rtc_h = {
    .cfg = {
        .master = {
            .clk_speed = CONFIG_APP_I2C_SPEED * 1000
        }
    }
};

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

    init_gpio(PIN_FUNCT, GPIO_MODE_INPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    init_gpio(PIN_OUTPUT, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
    init_gpio(PIN_LED, GPIO_MODE_OUTPUT, GPIO_FLOATING, GPIO_INTR_DISABLE);
#if CONFIG_APP_CLI_ENABLED
    init_gpio(PIN_CMDLINE, GPIO_MODE_INPUT, GPIO_PULLUP_ONLY, GPIO_INTR_DISABLE);
#endif

    init_vbat_adc();
    //init_peri_uart();

    init_nvs();

#   if CONFIG_APP_SERVER_RTC_DS3231
    ESP_ERROR_CHECK(i2cdev_init());
    
    init_rtc();
    if (rtc_is_lost_time()){
        ESP_LOGW(TAG, "RTC lost time");
    }
#   endif

    init_bluetooth();
    init_advertising();

    init_settings();
    load_settings();
    
    if (gpio_get_level(PIN_CMDLINE)==0){
        init_repl();
        init_raderctl();
        begin_repl();
    }
}

void app_main(){
    app_init();
    
    ESP_LOGI(TAG, "end init");
}

#endif /* CONFIG_APP_SERVER */